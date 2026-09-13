//! Capability-style kernel objects and per-table handle translation.
//!
//! This module provides the allocation-free policy half of Phase 3 part 1:
//! typed [`KernelObject`]s, unforgeable [`HandleId`]s with index plus
//! generation tags, bitmask [`Rights`], and a bounded [`HandleTable`] with
//! explicit duplication and transfer semantics.
//!
//! The design is architecture-neutral and shared by x86-64 and ARM64. It uses
//! only fixed-size arrays, performs no allocation, and executes no privileged
//! instructions, so it can be exercised by host unit tests. It does not touch
//! the `QEMU` harness, syscall dispatch, `IPC` endpoints, or `VirtIO` drivers.
//!
//! # Handle encoding
//!
//! A handle packs a table index in the low 8 bits and a non-zero generation
//! in the high 24 bits. The generation is bumped on every insertion, so a
//! stale handle that names a recycled slot is rejected with
//! [`ObjectError::StaleHandle`] instead of aliasing the new occupant. An
//! index outside [`MAX_HANDLES`] or a zero/out-of-range generation is
//! rejected with [`ObjectError::InvalidHandle`].
//!
//! # Rights
//!
//! Rights are a bitmask validated against [`RIGHT_ALL`]. Duplication and
//! transfer may only attenuate rights. Transfer additionally requires
//! [`RIGHT_TRANSFER`] on the source handle. [`HandleTable::remove`] requires
//! [`RIGHT_DESTROY`]; [`HandleTable::close`] is the owner teardown path and
//! performs no rights check beyond handle validity.

/// Capability rights bitmask type.
pub type Rights = u32;

/// Permit read-like observation of an object.
pub const RIGHT_READ: Rights = 1;
/// Permit write-like mutation through an object.
pub const RIGHT_WRITE: Rights = 2;
/// Permit execution through an object.
pub const RIGHT_EXECUTE: Rights = 4;
/// Permit transferring a handle to another table or owner.
pub const RIGHT_TRANSFER: Rights = 8;
/// Permit destroying the handle via the checked remove path.
pub const RIGHT_DESTROY: Rights = 16;
/// Mask of all valid rights bits.
pub const RIGHT_ALL: Rights =
    RIGHT_READ | RIGHT_WRITE | RIGHT_EXECUTE | RIGHT_TRANSFER | RIGHT_DESTROY;

/// Maximum number of live handles per table.
pub const MAX_HANDLES: usize = 64;

/// Number of low bits in a raw handle reserved for the table index.
const INDEX_BITS: u32 = 8;
/// Mask for the table index within a raw handle.
const INDEX_MASK: u32 = 0xFF;
/// Largest generation value that fits in the high bits of a raw handle.
const MAX_GENERATION: u32 = 0x00FF_FFFF;

/// Opaque handle naming one entry in a [`HandleTable`].
///
/// The inner word packs the table index in the low 8 bits and a non-zero
/// generation in the high 24 bits. Handles are `Copy` so they can move across
/// kernel boundaries without allocation; validity is always rechecked by the
/// owning table.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub struct HandleId(u32);

impl HandleId {
    /// Creates a handle from an explicit index and generation.
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidHandle`] when `index` is outside
    /// [`MAX_HANDLES`] or when `generation` is zero or exceeds the packed
    /// generation range.
    pub const fn new(index: u32, generation: u32) -> Result<Self, ObjectError> {
        if (index as usize) >= MAX_HANDLES {
            return Err(ObjectError::InvalidHandle);
        }
        if generation == 0 || generation > MAX_GENERATION {
            return Err(ObjectError::InvalidHandle);
        }
        Ok(Self((generation << INDEX_BITS) | index))
    }

    /// Wraps a raw word without validation.
    ///
    /// Validation is deferred to [`HandleTable`] lookup so tests and IPC
    /// peers can present forged words for rejection.
    #[must_use]
    pub const fn from_raw(raw: u32) -> Self {
        Self(raw)
    }

    /// Returns the raw packed word.
    #[must_use]
    pub const fn raw(self) -> u32 {
        self.0
    }

    /// Returns the table index encoded in this handle.
    #[must_use]
    pub const fn index(self) -> usize {
        (self.0 & INDEX_MASK) as usize
    }

    /// Returns the generation encoded in this handle.
    #[must_use]
    pub const fn generation(self) -> u32 {
        self.0 >> INDEX_BITS
    }
}

/// Typed kernel object nameable by a handle.
///
/// Payloads are plain integers so the table stays allocation-free and `Copy`.
/// Richer kernel state (channels, processes, VMOs) is represented by an ID or
/// size here; later phases resolve those IDs to live kernel structures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum KernelObject {
    /// Placeholder object with no associated resource.
    Null,
    /// Communication endpoint identified by a channel ID.
    Channel(u32),
    /// Process identified by a process ID.
    Process(u32),
    /// Thread identified by a thread ID.
    Thread(u32),
    /// Virtual memory object with a byte size.
    Vmo {
        /// Byte size of the memory object.
        size: u64,
    },
    /// Physical or virtual device identified by vendor and device IDs.
    Device {
        /// Vendor identifier (for example, a PCI vendor ID).
        vendor: u16,
        /// Device identifier (for example, a PCI device ID).
        device: u16,
    },
}

/// Live entry stored in a [`HandleTable`] slot.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HandleEntry {
    /// Object named by this entry.
    pub object: KernelObject,
    /// Rights granted through this entry, always a subset of [`RIGHT_ALL`].
    pub rights: Rights,
    /// Generation assigned at insertion; matches the owning [`HandleId`].
    pub generation: u32,
}

/// Failures from handle table bookkeeping.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ObjectError {
    /// Handle index is out of range or the generation is malformed.
    InvalidHandle,
    /// Handle names a recycled or closed slot.
    StaleHandle,
    /// Required rights are not held by the entry.
    RightsMismatch,
    /// Table holds [`MAX_HANDLES`] live entries.
    CapacityExhausted,
    /// Rights word contains bits outside [`RIGHT_ALL`].
    InvalidRights,
    /// Transfer was attempted without [`RIGHT_TRANSFER`] on the source.
    CannotTransfer,
}

/// Bounded per-owner table translating [`HandleId`]s to [`HandleEntry`]s.
///
/// The table retains the last issued generation per slot even after the slot
/// is closed, so stale handles are reported as [`ObjectError::StaleHandle`]
/// rather than aliasing a recycled occupant. The monotonic `next_generation`
/// counter supplies fresh generations; it wraps to `1` (skipping zero) after
/// [`MAX_GENERATION`]. A full 24-bit wrap could theoretically reissue a live
/// generation for the same slot after ~16M insertions into that slot; that
/// residual aliasing window is documented and accepted for Phase 3 part 1.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HandleTable {
    entries: [Option<HandleEntry>; MAX_HANDLES],
    generations: [u32; MAX_HANDLES],
    next_generation: u32,
    count: usize,
}

/// Validates that every set bit is within [`RIGHT_ALL`].
const fn validate_rights(rights: Rights) -> Result<(), ObjectError> {
    if rights & !RIGHT_ALL != 0 {
        return Err(ObjectError::InvalidRights);
    }
    Ok(())
}

impl HandleTable {
    /// Creates an empty table with the first generation set to one.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            entries: [None; MAX_HANDLES],
            generations: [0; MAX_HANDLES],
            next_generation: 1,
            count: 0,
        }
    }

    /// Returns the number of live handles.
    #[must_use]
    pub const fn len(&self) -> usize {
        self.count
    }

    /// Returns whether the table holds no live handles.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Inserts an object with the given rights and returns its handle.
    ///
    /// The lowest vacant slot is used and stamped with a fresh generation.
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidRights`] when `rights` contains bits
    /// outside [`RIGHT_ALL`], or [`ObjectError::CapacityExhausted`] when the
    /// table already holds [`MAX_HANDLES`] live entries.
    pub fn insert(
        &mut self,
        object: KernelObject,
        rights: Rights,
    ) -> Result<HandleId, ObjectError> {
        validate_rights(rights)?;
        let slot = self
            .entries
            .iter()
            .position(Option::is_none)
            .ok_or(ObjectError::CapacityExhausted)?;
        let generation = self.alloc_generation();
        let index_u32 = u32::try_from(slot).map_err(|_| ObjectError::CapacityExhausted)?;
        self.generations
            .get_mut(slot)
            .ok_or(ObjectError::CapacityExhausted)?;
        self.generations[slot] = generation;
        let entry = HandleEntry {
            object,
            rights,
            generation,
        };
        *self
            .entries
            .get_mut(slot)
            .ok_or(ObjectError::CapacityExhausted)? = Some(entry);
        self.count = self.count.saturating_add(1);
        HandleId::new(index_u32, generation)
    }

    /// Resolves a handle to its live entry.
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidHandle`] when the index is out of range
    /// or the generation is malformed, or [`ObjectError::StaleHandle`] when
    /// the slot is vacant or holds a newer generation.
    pub fn get(&self, id: HandleId) -> Result<&HandleEntry, ObjectError> {
        let index = id.index();
        if index >= MAX_HANDLES {
            return Err(ObjectError::InvalidHandle);
        }
        let generation = id.generation();
        if generation == 0 || generation > MAX_GENERATION {
            return Err(ObjectError::InvalidHandle);
        }
        let slot = self.entries.get(index).ok_or(ObjectError::InvalidHandle)?;
        match slot {
            Some(entry) if entry.generation == generation => Ok(entry),
            _ => Err(ObjectError::StaleHandle),
        }
    }

    /// Resolves a handle and checks that it carries every required right.
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidRights`] when `required` contains bits
    /// outside [`RIGHT_ALL`], [`ObjectError::InvalidHandle`] or
    /// [`ObjectError::StaleHandle`] from [`HandleTable::get`], or
    /// [`ObjectError::RightsMismatch`] when the entry lacks a required bit.
    pub fn get_with_rights(
        &self,
        id: HandleId,
        required: Rights,
    ) -> Result<&HandleEntry, ObjectError> {
        validate_rights(required)?;
        let entry = self.get(id)?;
        if entry.rights & required != required {
            return Err(ObjectError::RightsMismatch);
        }
        Ok(entry)
    }

    /// Duplicates a handle with attenuated rights, retaining the source.
    ///
    /// The new handle names the same object but carries `new_rights`, which
    /// must be a subset of the source rights. Duplication does not require
    /// [`RIGHT_TRANSFER`].
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidRights`] for unknown bits,
    /// [`ObjectError::InvalidHandle`] or [`ObjectError::StaleHandle`] for a
    /// bad source, [`ObjectError::RightsMismatch`] when `new_rights`
    /// escalates beyond the source, or [`ObjectError::CapacityExhausted`]
    /// when no vacant slot remains.
    pub fn duplicate(&mut self, id: HandleId, new_rights: Rights) -> Result<HandleId, ObjectError> {
        validate_rights(new_rights)?;
        let (object, source_rights) = {
            let source = self.get(id)?;
            (source.object, source.rights)
        };
        if new_rights & !source_rights != 0 {
            return Err(ObjectError::RightsMismatch);
        }
        self.insert(object, new_rights)
    }

    /// Moves a handle with attenuated rights, closing the source.
    ///
    /// The source must carry [`RIGHT_TRANSFER`] and `new_rights` must be a
    /// subset of the source rights. On success the source is closed and a
    /// fresh handle is returned. When insertion fails the source is retained.
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidRights`] for unknown bits,
    /// [`ObjectError::InvalidHandle`] or [`ObjectError::StaleHandle`] for a
    /// bad source, [`ObjectError::CannotTransfer`] when the source lacks
    /// [`RIGHT_TRANSFER`], [`ObjectError::RightsMismatch`] when `new_rights`
    /// escalates beyond the source, or [`ObjectError::CapacityExhausted`]
    /// when no vacant slot remains.
    pub fn transfer(&mut self, id: HandleId, new_rights: Rights) -> Result<HandleId, ObjectError> {
        validate_rights(new_rights)?;
        let (object, source_rights) = {
            let source = self.get(id)?;
            (source.object, source.rights)
        };
        if source_rights & RIGHT_TRANSFER == 0 {
            return Err(ObjectError::CannotTransfer);
        }
        if new_rights & !source_rights != 0 {
            return Err(ObjectError::RightsMismatch);
        }
        let moved = self.insert(object, new_rights)?;
        if let Err(err) = self.close(id) {
            let _ = self.close(moved);
            return Err(err);
        }
        Ok(moved)
    }

    /// Closes a handle without a rights check (owner teardown path).
    ///
    /// The slot is freed but its generation is retained so the closed handle
    /// reports [`ObjectError::StaleHandle`] on later use.
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidHandle`] for a malformed handle or
    /// [`ObjectError::StaleHandle`] for a vacant or recycled slot.
    pub fn close(&mut self, id: HandleId) -> Result<KernelObject, ObjectError> {
        let index = id.index();
        if index >= MAX_HANDLES {
            return Err(ObjectError::InvalidHandle);
        }
        let generation = id.generation();
        if generation == 0 || generation > MAX_GENERATION {
            return Err(ObjectError::InvalidHandle);
        }
        let slot = self
            .entries
            .get_mut(index)
            .ok_or(ObjectError::InvalidHandle)?;
        match slot {
            Some(entry) if entry.generation == generation => {
                let object = entry.object;
                *slot = None;
                self.count = self.count.saturating_sub(1);
                Ok(object)
            }
            _ => Err(ObjectError::StaleHandle),
        }
    }

    /// Destroys a handle after checking [`RIGHT_DESTROY`].
    ///
    /// Unlike [`HandleTable::close`], this checked path refuses handles that
    /// lack the destroy right.
    ///
    /// # Errors
    ///
    /// Returns [`ObjectError::InvalidHandle`] or [`ObjectError::StaleHandle`]
    /// for a bad handle, or [`ObjectError::RightsMismatch`] when the entry
    /// lacks [`RIGHT_DESTROY`].
    pub fn remove(&mut self, id: HandleId) -> Result<KernelObject, ObjectError> {
        let rights = self.get(id)?.rights;
        if rights & RIGHT_DESTROY == 0 {
            return Err(ObjectError::RightsMismatch);
        }
        self.close(id)
    }

    /// Allocates the next fresh generation, wrapping past the maximum to one.
    const fn alloc_generation(&mut self) -> u32 {
        let generation = self.next_generation;
        let mut next = generation.wrapping_add(1);
        if next == 0 || next > MAX_GENERATION {
            next = 1;
        }
        self.next_generation = next;
        generation
    }
}

impl Default for HandleTable {
    /// Creates an empty table, mirroring [`HandleTable::new`].
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_and_get_roundtrip() {
        let mut table = HandleTable::new();
        let id = table
            .insert(KernelObject::Null, RIGHT_READ | RIGHT_WRITE)
            .unwrap();
        let entry = table.get(id).unwrap();
        assert_eq!(entry.object, KernelObject::Null);
        assert_eq!(entry.rights, RIGHT_READ | RIGHT_WRITE);
        assert_eq!(entry.generation, id.generation());
        assert_eq!(table.len(), 1);
        assert!(!table.is_empty());
    }

    #[test]
    fn stale_after_close_and_reuse_bumps_generation() {
        let mut table = HandleTable::new();
        let id = table.insert(KernelObject::Null, RIGHT_ALL).unwrap();
        table.close(id).unwrap();
        assert_eq!(table.get(id), Err(ObjectError::StaleHandle));
        let reused = table.insert(KernelObject::Process(7), RIGHT_READ).unwrap();
        assert_eq!(reused.index(), id.index());
        assert_ne!(reused.generation(), id.generation());
        assert_eq!(table.get(id), Err(ObjectError::StaleHandle));
        assert_eq!(table.get(reused).unwrap().object, KernelObject::Process(7));
    }

    #[test]
    fn forged_handles_are_rejected() {
        let mut table = HandleTable::new();
        let _live = table.insert(KernelObject::Null, RIGHT_READ).unwrap();
        let out_of_range = HandleId::from_raw(0x00FF_FFFF);
        assert_eq!(table.get(out_of_range), Err(ObjectError::InvalidHandle));
        let bad_generation = HandleId::from_raw(0x00AB_0000);
        assert_eq!(table.get(bad_generation), Err(ObjectError::StaleHandle));
        let zero_generation = HandleId::from_raw(0);
        assert_eq!(table.get(zero_generation), Err(ObjectError::InvalidHandle));
        assert_eq!(
            HandleId::new(u32::try_from(MAX_HANDLES).unwrap(), 1),
            Err(ObjectError::InvalidHandle)
        );
        assert_eq!(HandleId::new(0, 0), Err(ObjectError::InvalidHandle));
    }

    #[test]
    fn rights_subset_is_enforced() {
        let mut table = HandleTable::new();
        let id = table
            .insert(KernelObject::Vmo { size: 4096 }, RIGHT_READ)
            .unwrap();
        assert!(table.get_with_rights(id, RIGHT_READ).is_ok());
        assert_eq!(
            table.get_with_rights(id, RIGHT_WRITE),
            Err(ObjectError::RightsMismatch)
        );
        assert_eq!(
            table.get_with_rights(id, RIGHT_READ | RIGHT_WRITE),
            Err(ObjectError::RightsMismatch)
        );
    }

    #[test]
    fn transfer_requires_transfer_right() {
        let mut table = HandleTable::new();
        let plain = table
            .insert(KernelObject::Channel(1), RIGHT_READ | RIGHT_WRITE)
            .unwrap();
        assert_eq!(
            table.transfer(plain, RIGHT_READ),
            Err(ObjectError::CannotTransfer)
        );
        assert!(table.get(plain).is_ok());

        let transferable = table
            .insert(KernelObject::Channel(2), RIGHT_READ | RIGHT_TRANSFER)
            .unwrap();
        let moved = table.transfer(transferable, RIGHT_READ).unwrap();
        assert_eq!(table.get(transferable), Err(ObjectError::StaleHandle));
        let entry = table.get(moved).unwrap();
        assert_eq!(entry.object, KernelObject::Channel(2));
        assert_eq!(entry.rights, RIGHT_READ);
    }

    #[test]
    fn transfer_rejects_rights_escalation_and_retains_source() {
        let mut table = HandleTable::new();
        let id = table
            .insert(KernelObject::Channel(3), RIGHT_READ | RIGHT_TRANSFER)
            .unwrap();
        assert_eq!(
            table.transfer(id, RIGHT_READ | RIGHT_WRITE),
            Err(ObjectError::RightsMismatch)
        );
        assert!(table.get(id).is_ok());
    }

    #[test]
    fn capacity_exhausted_when_table_is_full() {
        let mut table = HandleTable::new();
        for index in 0..MAX_HANDLES {
            table
                .insert(
                    KernelObject::Thread(u32::try_from(index).unwrap()),
                    RIGHT_READ,
                )
                .unwrap();
        }
        assert_eq!(table.len(), MAX_HANDLES);
        assert_eq!(
            table.insert(KernelObject::Null, RIGHT_READ),
            Err(ObjectError::CapacityExhausted)
        );
    }

    #[test]
    fn invalid_rights_are_rejected() {
        let mut table = HandleTable::new();
        assert_eq!(
            table.insert(KernelObject::Null, RIGHT_ALL | 0x20),
            Err(ObjectError::InvalidRights)
        );
        assert_eq!(
            table.insert(KernelObject::Null, 1 << 31),
            Err(ObjectError::InvalidRights)
        );
        let id = table.insert(KernelObject::Null, RIGHT_READ).unwrap();
        assert_eq!(
            table.get_with_rights(id, 0x40),
            Err(ObjectError::InvalidRights)
        );
        assert_eq!(table.duplicate(id, 0x80), Err(ObjectError::InvalidRights));
        assert_eq!(table.len(), 1);
    }

    #[test]
    fn duplicate_with_reduced_rights_succeeds() {
        let mut table = HandleTable::new();
        let id = table
            .insert(KernelObject::Process(42), RIGHT_READ | RIGHT_WRITE)
            .unwrap();
        let copy = table.duplicate(id, RIGHT_READ).unwrap();
        assert_ne!(copy.raw(), id.raw());
        assert!(table.get(id).is_ok());
        let entry = table.get(copy).unwrap();
        assert_eq!(entry.object, KernelObject::Process(42));
        assert_eq!(entry.rights, RIGHT_READ);
    }

    #[test]
    fn duplicate_with_escalated_rights_fails() {
        let mut table = HandleTable::new();
        let id = table.insert(KernelObject::Process(1), RIGHT_READ).unwrap();
        assert_eq!(
            table.duplicate(id, RIGHT_READ | RIGHT_WRITE),
            Err(ObjectError::RightsMismatch)
        );
        assert_eq!(table.len(), 1);
    }

    #[test]
    fn remove_requires_destroy_but_close_does_not() {
        let mut table = HandleTable::new();
        let plain = table.insert(KernelObject::Null, RIGHT_READ).unwrap();
        assert_eq!(table.remove(plain), Err(ObjectError::RightsMismatch));
        assert!(table.get(plain).is_ok());
        table.close(plain).unwrap();
        assert_eq!(table.get(plain), Err(ObjectError::StaleHandle));

        let destroyable = table
            .insert(
                KernelObject::Device {
                    vendor: 0x1234,
                    device: 0x5678,
                },
                RIGHT_READ | RIGHT_DESTROY,
            )
            .unwrap();
        let object = table.remove(destroyable).unwrap();
        assert_eq!(
            object,
            KernelObject::Device {
                vendor: 0x1234,
                device: 0x5678
            }
        );
        assert_eq!(table.get(destroyable), Err(ObjectError::StaleHandle));
    }

    #[test]
    fn object_variants_roundtrip_through_table() {
        let mut table = HandleTable::new();
        let channel = table.insert(KernelObject::Channel(9), RIGHT_ALL).unwrap();
        let thread = table.insert(KernelObject::Thread(3), RIGHT_READ).unwrap();
        let vmo = table
            .insert(KernelObject::Vmo { size: 8192 }, RIGHT_READ | RIGHT_WRITE)
            .unwrap();
        assert_eq!(table.get(channel).unwrap().object, KernelObject::Channel(9));
        assert_eq!(table.get(thread).unwrap().object, KernelObject::Thread(3));
        assert_eq!(
            table.get(vmo).unwrap().object,
            KernelObject::Vmo { size: 8192 }
        );
        assert_eq!(table.len(), 3);
    }
}
