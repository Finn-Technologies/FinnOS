#![deny(missing_docs)]

//! `VirtIO-GPU` hardware device driver and graphics acceleration policy.
//!
//! Implements the OASIS `VirtIO` Specification v1.2 (Device ID 16 / `0x1050`).
//! Supports 2D hardware display scanout, host memory backing attachment,
//! hardware cursor overlay planes, double-buffered page-flipping, and feature
//! negotiation for 3D `VirGL` acceleration.

/// PCI Device ID for standard `VirtIO-GPU` devices (`0x1050`).
pub const VIRTIO_GPU_DEVICE: u16 = 0x1050;

/// Bit index of the 3D `VirGL` acceleration feature (`VIRGL` = 0).
pub const VIRTIO_GPU_F_VIRGL_BIT: u32 = 0;
/// Bit index of the EDID display query feature (`EDID` = 1).
pub const VIRTIO_GPU_F_EDID_BIT: u32 = 1;

/// Feature mask for 3D `VirGL` GPU shader acceleration (`1 << 0`).
pub const VIRTIO_GPU_F_VIRGL: u32 = 1 << VIRTIO_GPU_F_VIRGL_BIT;
/// Feature mask for EDID display capability queries (`1 << 1`).
pub const VIRTIO_GPU_F_EDID: u32 = 1 << VIRTIO_GPU_F_EDID_BIT;

/// 2D Command: Query available display scanouts and native resolutions (`0x0100`).
pub const VIRTIO_GPU_CMD_GET_DISPLAY_INFO: u32 = 0x0100;
/// 2D Command: Allocate a 2D GPU surface resource (`0x0101`).
pub const VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: u32 = 0x0101;
/// 2D Command: Destroy/release a GPU surface resource (`0x0102`).
pub const VIRTIO_GPU_CMD_RESOURCE_UNREF: u32 = 0x0102;
/// 2D Command: Bind a surface resource to a display scanout (`0x0103`).
pub const VIRTIO_GPU_CMD_SET_SCANOUT: u32 = 0x0103;
/// 2D Command: Flush scanout pixels to physical display (`0x0104`).
pub const VIRTIO_GPU_CMD_RESOURCE_FLUSH: u32 = 0x0104;
/// 2D Command: Transfer pixel data from guest backing memory to host GPU (`0x0105`).
pub const VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: u32 = 0x0105;
/// 2D Command: Attach guest physical memory pages to a GPU resource (`0x0106`).
pub const VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: u32 = 0x0106;
/// 2D Command: Detach guest physical memory from a GPU resource (`0x0107`).
pub const VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING: u32 = 0x0107;

/// 3D Command: Create a 3D rendering context (`0x0200`).
pub const VIRTIO_GPU_CMD_CTX_CREATE: u32 = 0x0200;
/// 3D Command: Destroy a 3D rendering context (`0x0201`).
pub const VIRTIO_GPU_CMD_CTX_DESTROY: u32 = 0x0201;
/// 3D Command: Attach resource to 3D rendering context (`0x0202`).
pub const VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE: u32 = 0x0202;
/// 3D Command: Detach resource from 3D rendering context (`0x0203`).
pub const VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE: u32 = 0x0203;
/// 3D Command: Allocate a 3D GPU surface resource (`0x0204`).
pub const VIRTIO_GPU_CMD_RESOURCE_CREATE_3D: u32 = 0x0204;
/// 3D Command: Transfer 3D surface data to host GPU (`0x0205`).
pub const VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D: u32 = 0x0205;
/// 3D Command: Transfer rendered 3D surface data from host GPU (`0x0206`).
pub const VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D: u32 = 0x0206;
/// 3D Command: Submit accelerated drawing commands to host GPU (`0x0207`).
pub const VIRTIO_GPU_CMD_SUBMIT_3D: u32 = 0x0207;

/// 3D Resource Bind: Render target surface (`1 << 1`).
pub const VIRGL_BIND_RENDER_TARGET: u32 = 1 << 1;
/// 3D Resource Bind: Sampler view / texture (`1 << 3`).
pub const VIRGL_BIND_SAMPLER_VIEW: u32 = 1 << 3;

/// Cursor Command: Update hardware cursor image, hotspot, and position (`0x0120`).
pub const VIRTIO_GPU_CMD_UPDATE_CURSOR: u32 = 0x0120;
/// Cursor Command: Move hardware cursor position without changing image (`0x0121`).
pub const VIRTIO_GPU_CMD_MOVE_CURSOR: u32 = 0x0121;

/// GPU Success Response: command completed without payload (`0x1100`).
pub const VIRTIO_GPU_RESP_OK_NODATA: u32 = 0x1100;
/// GPU Success Response: display scanout information payload (`0x1101`).
pub const VIRTIO_GPU_RESP_OK_DISPLAY_INFO: u32 = 0x1101;
/// GPU Error Response: unspecified failure (`0x1200`).
pub const VIRTIO_GPU_RESP_ERR_UNSPEC: u32 = 0x1200;
/// GPU Error Response: out of host GPU memory (`0x1201`).
pub const VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY: u32 = 0x1201;
/// GPU Error Response: invalid scanout ID (`0x1202`).
pub const VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID: u32 = 0x1202;
/// GPU Error Response: invalid resource ID (`0x1203`).
pub const VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID: u32 = 0x1203;
/// GPU Error Response: invalid context ID (`0x1204`).
pub const VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID: u32 = 0x1204;
/// GPU Error Response: invalid parameter (`0x1205`).
pub const VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER: u32 = 0x1205;

/// Color format: 32-bit BGRX (Blue, Green, Red, padding) matching UEFI GOP (`2`).
pub const VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM: u32 = 2;
/// Color format: 32-bit BGRA (Blue, Green, Red, Alpha) for cursor overlays (`1`).
pub const VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM: u32 = 1;

/// Standard maximum display scanouts supported by `VirtIO-GPU` (`16`).
pub const VIRTIO_GPU_MAX_SCANOUTS: usize = 16;

/// Standard header common to all `VirtIO-GPU` control and cursor requests.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuCtrlHdr {
    /// Command or response type (`VIRTIO_GPU_CMD_*` or `VIRTIO_GPU_RESP_*`).
    pub req_type: u32,
    /// Command flags (e.g. `VIRTIO_GPU_FLAG_FENCE`).
    pub flags: u32,
    /// Unique command fence ID for asynchronous completion synchronization.
    pub fence_id: u64,
    /// Rendering context ID (used for 3D `VirGL` rendering contexts).
    pub ctx_id: u32,
    /// Ring index for multi-queue scheduling.
    pub ring_idx: u8,
    /// Reserved zero padding for alignment.
    pub padding: [u8; 3],
}

impl VirtioGpuCtrlHdr {
    /// Create a new command header.
    #[must_use]
    pub const fn new(req_type: u32) -> Self {
        Self {
            req_type,
            flags: 0,
            fence_id: 0,
            ctx_id: 0,
            ring_idx: 0,
            padding: [0; 3],
        }
    }
}

/// 2D Rectangle representing a display region or transfer bounding box.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuRect {
    /// X coordinate of top-left corner.
    pub x: u32,
    /// Y coordinate of top-left corner.
    pub y: u32,
    /// Width of rectangle in pixels.
    pub width: u32,
    /// Height of rectangle in pixels.
    pub height: u32,
}

/// Request to create a 2D GPU surface resource.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuResourceCreate2d {
    /// Common control header.
    pub hdr: VirtioGpuCtrlHdr,
    /// Guest-assigned unique resource ID (1..N).
    pub resource_id: u32,
    /// Pixel format (`VIRTIO_GPU_FORMAT_*`).
    pub format: u32,
    /// Surface width in pixels.
    pub width: u32,
    /// Surface height in pixels.
    pub height: u32,
}

/// Request to bind a GPU surface resource to a display scanout (hardware page flip).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuSetScanout {
    /// Common control header.
    pub hdr: VirtioGpuCtrlHdr,
    /// Destination rectangle on scanout.
    pub rect: VirtioGpuRect,
    /// Display scanout index (0 for primary display).
    pub scanout_id: u32,
    /// Resource ID to scan out (0 to disable scanout).
    pub resource_id: u32,
}

/// Request to flush updated GPU surface contents to the physical screen.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuResourceFlush {
    /// Common control header.
    pub hdr: VirtioGpuCtrlHdr,
    /// Screen rectangle to flush.
    pub rect: VirtioGpuRect,
    /// Resource ID being flushed.
    pub resource_id: u32,
    /// Reserved zero padding.
    pub padding: u32,
}

/// Request to transfer modified pixel data from guest RAM to host GPU VRAM.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuTransferToHost2d {
    /// Common control header.
    pub hdr: VirtioGpuCtrlHdr,
    /// Bounding rectangle of modified pixels.
    pub rect: VirtioGpuRect,
    /// Byte offset in guest backing memory.
    pub offset: u64,
    /// Target resource ID.
    pub resource_id: u32,
    /// Reserved zero padding.
    pub padding: u32,
}

/// Guest physical memory chunk descriptor for attaching backing storage.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuMemEntry {
    /// Guest physical start address.
    pub addr: u64,
    /// Length of memory region in bytes.
    pub length: u32,
    /// Reserved zero padding.
    pub padding: u32,
}

/// Request to attach guest physical memory pages as backing for a GPU resource.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuResourceAttachBacking {
    /// Common control header.
    pub hdr: VirtioGpuCtrlHdr,
    /// Target resource ID.
    pub resource_id: u32,
    /// Number of contiguous memory entries following this header.
    pub nr_entries: u32,
}

/// Hardware cursor position coordinate.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuCursorPos {
    /// Scanout index the cursor belongs to.
    pub scanout_id: u32,
    /// Screen X coordinate of cursor.
    pub x: u32,
    /// Screen Y coordinate of cursor.
    pub y: u32,
    /// Reserved zero padding.
    pub padding: u32,
}

/// Request to update the hardware cursor plane image and coordinates.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuUpdateCursor {
    /// Common control header.
    pub hdr: VirtioGpuCtrlHdr,
    /// Cursor position on screen.
    pub pos: VirtioGpuCursorPos,
    /// Resource ID containing cursor 64x64 RGBA bitmap (0 to hide).
    pub resource_id: u32,
    /// Hotspot X offset within cursor image.
    pub hot_x: u32,
    /// Hotspot Y offset within cursor image.
    pub hot_y: u32,
    /// Reserved zero padding.
    pub padding: u32,
}

/// Request to create a 3D GPU rendering context.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuCtxCreate {
    /// Common control header (`ctx_id` specifies context).
    pub hdr: VirtioGpuCtrlHdr,
    /// Length of context name string.
    pub nlen: u32,
    /// Reserved zero padding.
    pub padding: u32,
    /// Human-readable context name string (up to 64 bytes).
    pub debug_name: [u8; 64],
}

/// Request to destroy a 3D GPU rendering context.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuCtxDestroy {
    /// Common control header (`ctx_id` specifies context to destroy).
    pub hdr: VirtioGpuCtrlHdr,
}

/// Request to attach a GPU resource to a rendering context.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuCtxAttachResource {
    /// Common control header (`ctx_id` specifies target context).
    pub hdr: VirtioGpuCtrlHdr,
    /// Resource ID to attach.
    pub resource_id: u32,
    /// Reserved zero padding.
    pub padding: u32,
}

/// Request to allocate a 3D GPU surface resource.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuResourceCreate3d {
    /// Common control header.
    pub hdr: VirtioGpuCtrlHdr,
    /// Guest-assigned unique resource ID.
    pub resource_id: u32,
    /// Surface target (e.g. 2 for 2D texture, 0 for buffer).
    pub target: u32,
    /// Pixel format.
    pub format: u32,
    /// Resource bind flags (`VIRGL_BIND_*`).
    pub bind: u32,
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
    /// Depth in pixels (1 for 2D).
    pub depth: u32,
    /// Array size (1 for single texture).
    pub array_size: u32,
    /// Last mipmap level (0 for base level only).
    pub last_level: u32,
    /// Number of multisample samples (0 for none).
    pub nr_samples: u32,
    /// Memory flags.
    pub flags: u32,
    /// Reserved zero padding.
    pub padding: u32,
}

/// Request to submit 3D `VirGL` GPU hardware acceleration commands.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtioGpuSubmit3d {
    /// Common control header (`ctx_id` specifies rendering context).
    pub hdr: VirtioGpuCtrlHdr,
    /// Size of command buffer payload in bytes.
    pub size: u32,
    /// Number of command words encoded.
    pub num_words: u32,
}

/// Maximum number of 32-bit command words in a GPU command batch.
pub const GPU_CMD_STREAM_CAPACITY: usize = 64;

/// `VirGL` GPU Command Opcode: Clear render surface (`0x01`).
pub const VIRGL_CCMD_CLEAR: u32 = 0x01;
/// `VirGL` GPU Command Opcode: Set surface viewport (`0x02`).
pub const VIRGL_CCMD_SET_VIEWPORT: u32 = 0x02;
/// `VirGL` GPU Command Opcode: Draw rectangle / box (`0x03`).
pub const VIRGL_CCMD_DRAW_RECT: u32 = 0x03;
/// `VirGL` GPU Command Opcode: Blit texture / surface transfer (`0x04`).
pub const VIRGL_CCMD_BLIT: u32 = 0x04;
/// `VirGL` GPU Command Opcode: Set active framebuffer (`0x05`).
pub const VIRGL_CCMD_SET_FRAMEBUFFER: u32 = 0x05;

/// Structured GPU Hardware Acceleration Command Stream Builder.
#[derive(Clone, Copy, Debug)]
pub struct GpuCommandStream {
    /// Target context ID.
    pub ctx_id: u32,
    /// Command words buffer.
    pub words: [u32; GPU_CMD_STREAM_CAPACITY],
    /// Number of command words currently written.
    pub len: usize,
}

impl GpuCommandStream {
    /// Create a new GPU command stream for the given context.
    #[must_use]
    pub const fn new(ctx_id: u32) -> Self {
        Self {
            ctx_id,
            words: [0; GPU_CMD_STREAM_CAPACITY],
            len: 0,
        }
    }

    /// Append a single 32-bit word to the stream.
    pub const fn push(&mut self, word: u32) -> bool {
        if self.len < GPU_CMD_STREAM_CAPACITY {
            self.words[self.len] = word;
            self.len += 1;
            true
        } else {
            false
        }
    }

    /// Encode a hardware framebuffer bind command.
    pub const fn cmd_set_framebuffer(&mut self, resource_id: u32, width: u32, height: u32) -> bool {
        if self.len + 4 <= GPU_CMD_STREAM_CAPACITY {
            let _ = self.push(VIRGL_CCMD_SET_FRAMEBUFFER);
            let _ = self.push(resource_id);
            let _ = self.push(width);
            let _ = self.push(height);
            true
        } else {
            false
        }
    }

    /// Encode a hardware viewport configuration command.
    pub const fn cmd_set_viewport(&mut self, width: u32, height: u32) -> bool {
        if self.len + 3 <= GPU_CMD_STREAM_CAPACITY {
            let _ = self.push(VIRGL_CCMD_SET_VIEWPORT);
            let _ = self.push(width);
            let _ = self.push(height);
            true
        } else {
            false
        }
    }

    /// Encode a hardware clear command.
    pub const fn cmd_clear(&mut self, color_rgba: u32) -> bool {
        if self.len + 2 <= GPU_CMD_STREAM_CAPACITY {
            let _ = self.push(VIRGL_CCMD_CLEAR);
            let _ = self.push(color_rgba);
            true
        } else {
            false
        }
    }

    /// Encode a hardware rectangle fill command.
    pub const fn cmd_draw_rect(&mut self, x: u32, y: u32, w: u32, h: u32, color_rgba: u32) -> bool {
        if self.len + 6 <= GPU_CMD_STREAM_CAPACITY {
            let _ = self.push(VIRGL_CCMD_DRAW_RECT);
            let _ = self.push(x);
            let _ = self.push(y);
            let _ = self.push(w);
            let _ = self.push(h);
            let _ = self.push(color_rgba);
            true
        } else {
            false
        }
    }

    /// Encode a hardware surface blit / texture transfer command.
    pub const fn cmd_blit(
        &mut self,
        src_res: u32,
        dst_res: u32,
        x: u32,
        y: u32,
        w: u32,
        h: u32,
    ) -> bool {
        if self.len + 7 <= GPU_CMD_STREAM_CAPACITY {
            let _ = self.push(VIRGL_CCMD_BLIT);
            let _ = self.push(src_res);
            let _ = self.push(dst_res);
            let _ = self.push(x);
            let _ = self.push(y);
            let _ = self.push(w);
            let _ = self.push(h);
            true
        } else {
            false
        }
    }

    /// Build a VirtIO-GPU submit packet for this command stream.
    #[must_use]
    #[allow(clippy::cast_possible_truncation)]
    pub const fn build_submit_packet(&self) -> VirtioGpuSubmit3d {
        let mut hdr = VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_SUBMIT_3D);
        hdr.ctx_id = self.ctx_id;
        VirtioGpuSubmit3d {
            hdr,
            size: (self.len * core::mem::size_of::<u32>()) as u32,
            num_words: self.len as u32,
        }
    }
}

/// Hardware Display and GPU Scanout Manager.
///
/// Coordinates double-buffered surface allocation, page-flipping, and hardware
/// cursor updates with zero CPU pixel copying.
#[derive(Clone, Debug)]
pub struct GpuDisplayManager {
    /// Active screen width in pixels.
    pub width: u32,
    /// Active screen height in pixels.
    pub height: u32,
    /// Front buffer resource ID (currently displayed).
    pub front_resource_id: u32,
    /// Back buffer resource ID (currently rendered into).
    pub back_resource_id: u32,
    /// Hardware cursor resource ID.
    pub cursor_resource_id: u32,
    /// Current hardware cursor position.
    pub cursor_pos: (i32, i32),
    /// Whether the hardware cursor is visible.
    pub cursor_visible: bool,
    /// 3D `VirGL` rendering context ID.
    pub ctx_id: u32,
}

impl GpuDisplayManager {
    /// Initialize a new GPU display manager.
    #[must_use]
    pub const fn new(width: u32, height: u32) -> Self {
        Self {
            width,
            height,
            front_resource_id: 1,
            back_resource_id: 2,
            cursor_resource_id: 3,
            cursor_pos: (0, 0),
            cursor_visible: true,
            ctx_id: 1,
        }
    }

    /// Build a 2D surface creation packet for the given resource ID.
    #[must_use]
    pub const fn create_surface_packet(&self, resource_id: u32) -> VirtioGpuResourceCreate2d {
        VirtioGpuResourceCreate2d {
            hdr: VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D),
            resource_id,
            format: VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
            width: self.width,
            height: self.height,
        }
    }

    /// Build a hardware cursor creation packet (standard 64x64 RGBA overlay).
    #[must_use]
    pub const fn create_cursor_packet(&self) -> VirtioGpuResourceCreate2d {
        VirtioGpuResourceCreate2d {
            hdr: VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D),
            resource_id: self.cursor_resource_id,
            format: VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
            width: 64,
            height: 64,
        }
    }

    /// Build a scanout binding packet for the given resource (hardware flip).
    #[must_use]
    pub const fn set_scanout_packet(&self, resource_id: u32) -> VirtioGpuSetScanout {
        VirtioGpuSetScanout {
            hdr: VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_SET_SCANOUT),
            rect: VirtioGpuRect {
                x: 0,
                y: 0,
                width: self.width,
                height: self.height,
            },
            scanout_id: 0,
            resource_id,
        }
    }

    /// Build a transfer packet to move modified pixels into host GPU memory.
    #[must_use]
    pub const fn transfer_to_host_packet(
        &self,
        resource_id: u32,
        damage: VirtioGpuRect,
    ) -> VirtioGpuTransferToHost2d {
        VirtioGpuTransferToHost2d {
            hdr: VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D),
            rect: damage,
            offset: 0,
            resource_id,
            padding: 0,
        }
    }

    /// Build a screen flush packet to trigger physical display presentation.
    #[must_use]
    pub const fn flush_packet(
        &self,
        resource_id: u32,
        damage: VirtioGpuRect,
    ) -> VirtioGpuResourceFlush {
        VirtioGpuResourceFlush {
            hdr: VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_RESOURCE_FLUSH),
            rect: damage,
            resource_id,
            padding: 0,
        }
    }

    /// Build a hardware cursor move packet.
    #[must_use]
    pub fn move_cursor_packet(&mut self, x: i32, y: i32) -> VirtioGpuUpdateCursor {
        self.cursor_pos = (x, y);
        VirtioGpuUpdateCursor {
            hdr: VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_MOVE_CURSOR),
            pos: VirtioGpuCursorPos {
                scanout_id: 0,
                x: x.max(0).cast_unsigned(),
                y: y.max(0).cast_unsigned(),
                padding: 0,
            },
            resource_id: if self.cursor_visible {
                self.cursor_resource_id
            } else {
                0
            },
            hot_x: 0,
            hot_y: 0,
            padding: 0,
        }
    }

    /// Swap front and back buffer identifiers (double buffering page flip).
    pub const fn swap_buffers(&mut self) {
        let temp = self.front_resource_id;
        self.front_resource_id = self.back_resource_id;
        self.back_resource_id = temp;
    }

    /// Build a 3D `VirGL` context creation packet.
    #[must_use]
    #[allow(clippy::cast_possible_truncation)]
    pub fn create_context_packet(&self, name: &[u8]) -> VirtioGpuCtxCreate {
        let mut hdr = VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_CTX_CREATE);
        hdr.ctx_id = self.ctx_id;
        let mut debug_name = [0u8; 64];
        let copy_len = name.len().min(63);
        let mut i = 0;
        while i < copy_len {
            debug_name[i] = name[i];
            i += 1;
        }
        VirtioGpuCtxCreate {
            hdr,
            nlen: copy_len as u32,
            padding: 0,
            debug_name,
        }
    }

    /// Build a context resource attachment packet.
    #[must_use]
    pub const fn attach_resource_packet(&self, resource_id: u32) -> VirtioGpuCtxAttachResource {
        let mut hdr = VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE);
        hdr.ctx_id = self.ctx_id;
        VirtioGpuCtxAttachResource {
            hdr,
            resource_id,
            padding: 0,
        }
    }

    /// Build a 3D GPU surface resource allocation packet.
    #[must_use]
    pub const fn create_surface_3d_packet(&self, resource_id: u32) -> VirtioGpuResourceCreate3d {
        VirtioGpuResourceCreate3d {
            hdr: VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_RESOURCE_CREATE_3D),
            resource_id,
            target: 2,
            format: VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
            bind: VIRGL_BIND_RENDER_TARGET | VIRGL_BIND_SAMPLER_VIEW,
            width: self.width,
            height: self.height,
            depth: 1,
            array_size: 1,
            last_level: 0,
            nr_samples: 0,
            flags: 0,
            padding: 0,
        }
    }

    /// Create a new command stream for this GPU display manager context.
    #[must_use]
    pub const fn create_command_stream(&self) -> GpuCommandStream {
        GpuCommandStream::new(self.ctx_id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn virtio_gpu_hdr_initialization() {
        let hdr = VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
        assert_eq!(hdr.req_type, 0x0101);
        assert_eq!(hdr.flags, 0);
        assert_eq!(hdr.fence_id, 0);
    }

    #[test]
    fn display_manager_packet_generation() {
        let mut mgr = GpuDisplayManager::new(1280, 800);
        let create = mgr.create_surface_packet(1);
        assert_eq!(create.resource_id, 1);
        assert_eq!(create.width, 1280);
        assert_eq!(create.height, 800);
        assert_eq!(create.format, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM);

        let scanout = mgr.set_scanout_packet(1);
        assert_eq!(scanout.scanout_id, 0);
        assert_eq!(scanout.rect.width, 1280);

        let cursor = mgr.move_cursor_packet(150, 200);
        assert_eq!(cursor.hdr.req_type, VIRTIO_GPU_CMD_MOVE_CURSOR);
        assert_eq!(cursor.pos.x, 150);
        assert_eq!(cursor.pos.y, 200);

        mgr.swap_buffers();
        assert_eq!(mgr.front_resource_id, 2);
        assert_eq!(mgr.back_resource_id, 1);
    }

    #[test]
    fn hardware_cursor_packet_validation() {
        let mgr = GpuDisplayManager::new(1024, 768);
        let cursor = mgr.create_cursor_packet();
        assert_eq!(cursor.width, 64);
        assert_eq!(cursor.height, 64);
        assert_eq!(cursor.format, VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM);
    }

    #[test]
    #[allow(clippy::cast_possible_truncation)]
    fn virgl_3d_context_and_command_stream() {
        let mgr = GpuDisplayManager::new(1280, 800);
        let ctx = mgr.create_context_packet(b"FinnOS-Compositor");
        assert_eq!(ctx.hdr.req_type, VIRTIO_GPU_CMD_CTX_CREATE);
        assert_eq!(ctx.hdr.ctx_id, 1);
        assert_eq!(ctx.nlen, 17);
        assert_eq!(&ctx.debug_name[..17], b"FinnOS-Compositor");

        let attach = mgr.attach_resource_packet(1);
        assert_eq!(attach.hdr.req_type, VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE);
        assert_eq!(attach.resource_id, 1);

        let res_3d = mgr.create_surface_3d_packet(2);
        assert_eq!(res_3d.hdr.req_type, VIRTIO_GPU_CMD_RESOURCE_CREATE_3D);
        assert_eq!(res_3d.width, 1280);
        assert_eq!(res_3d.height, 800);
        assert_eq!(
            res_3d.bind,
            VIRGL_BIND_RENDER_TARGET | VIRGL_BIND_SAMPLER_VIEW
        );

        let mut stream = mgr.create_command_stream();
        assert_eq!(stream.ctx_id, 1);
        assert!(stream.cmd_set_framebuffer(1, 1280, 800));
        assert!(stream.cmd_set_viewport(1280, 800));
        assert!(stream.cmd_clear(0xFF18_1825)); // Catppuccin Mantle dark
        assert!(stream.cmd_draw_rect(40, 60, 520, 340, 0xFF28_283D));
        assert!(stream.cmd_blit(1, 2, 0, 0, 1280, 800));

        let submit = stream.build_submit_packet();
        assert_eq!(submit.hdr.req_type, VIRTIO_GPU_CMD_SUBMIT_3D);
        assert_eq!(submit.hdr.ctx_id, 1);
        assert_eq!(submit.num_words, stream.len as u32);
        assert_eq!(submit.size, (stream.len * 4) as u32);
    }
}
