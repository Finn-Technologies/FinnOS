// Clean-room 64-bit Mach-O, Universal/Fat, dyld chained fixups, and ObjC ABI v2
// format definitions. Self-contained under finn::darwin so the loader compiles
// identically on Linux and macOS without Apple SDK header dependencies.

#ifndef FINN_DARWIN_MACHO_FORMAT_HPP
#define FINN_DARWIN_MACHO_FORMAT_HPP

#include <cstdint>

namespace finn::darwin {

// Mach-O magics
inline constexpr std::uint32_t kMhMagic64 = 0xfeedfacfu;
inline constexpr std::uint32_t kMhCigam64 = 0xcffaedfeu;
inline constexpr std::uint32_t kFatMagic = 0xcafebabeu;
inline constexpr std::uint32_t kFatCigam = 0xbebafecau;
inline constexpr std::uint32_t kFatMagic64 = 0xcafebabfu;
inline constexpr std::uint32_t kFatCigam64 = 0xbfbafecau;

// CPU types
inline constexpr std::uint32_t kCpuArchAbi64 = 0x01000000u;
inline constexpr std::uint32_t kCpuTypeX86 = 7u;
inline constexpr std::uint32_t kCpuTypeX86_64 = kCpuTypeX86 | kCpuArchAbi64;
inline constexpr std::uint32_t kCpuTypeArm = 12u;
inline constexpr std::uint32_t kCpuTypeArm64 = kCpuTypeArm | kCpuArchAbi64;

// File types
inline constexpr std::uint32_t kMhExecute = 0x2u;
inline constexpr std::uint32_t kMhDylib = 0x6u;
inline constexpr std::uint32_t kMhBundle = 0x8u;

// Load command constants
inline constexpr std::uint32_t kLcReqDyld = 0x80000000u;
inline constexpr std::uint32_t kLcSymtab = 0x2u;
inline constexpr std::uint32_t kLcUnixThread = 0x5u;
inline constexpr std::uint32_t kLcDysymtab = 0xbu;
inline constexpr std::uint32_t kLcLoadDylib = 0xcu;
inline constexpr std::uint32_t kLcIdDylib = 0xdu;
inline constexpr std::uint32_t kLcLoadWeakDylib = 0x18u | kLcReqDyld;
inline constexpr std::uint32_t kLcSegment64 = 0x19u;
inline constexpr std::uint32_t kLcReexportDylib = 0x1fu | kLcReqDyld;
inline constexpr std::uint32_t kLcLazyLoadDylib = 0x20u;
inline constexpr std::uint32_t kLcRpath = 0x1cu | kLcReqDyld;
inline constexpr std::uint32_t kLcDyldInfo = 0x22u;
inline constexpr std::uint32_t kLcDyldInfoOnly = 0x22u | kLcReqDyld;
inline constexpr std::uint32_t kLcMain = 0x28u | kLcReqDyld;
inline constexpr std::uint32_t kLcDyldExportsTrie = 0x33u | kLcReqDyld;
inline constexpr std::uint32_t kLcDyldChainedFixups = 0x34u | kLcReqDyld;

// Section types (low 8 bits of section_64::flags)
inline constexpr std::uint32_t kSectionTypeMask = 0xffu;
inline constexpr std::uint32_t kSModInitFuncPointers = 0x9u;
inline constexpr std::uint32_t kSInitFuncOffsets = 0x16u;

// VM protections
inline constexpr std::uint32_t kVmProtRead = 0x1u;
inline constexpr std::uint32_t kVmProtWrite = 0x2u;
inline constexpr std::uint32_t kVmProtExecute = 0x4u;

// Chained fixups constants
inline constexpr std::uint32_t kDyldChainedImport = 1u;
inline constexpr std::uint32_t kDyldChainedImportAddend = 2u;
inline constexpr std::uint32_t kDyldChainedImportAddend64 = 3u;

inline constexpr std::uint16_t kDyldChainedPtrArm64e = 1u;
inline constexpr std::uint16_t kDyldChainedPtr64 = 2u;
inline constexpr std::uint16_t kDyldChainedPtr32 = 3u;
inline constexpr std::uint16_t kDyldChainedPtr64Offset = 6u;
inline constexpr std::uint16_t kDyldChainedPtrArm64eUserland = 9u;
inline constexpr std::uint16_t kDyldChainedPtrArm64eUserland24 = 12u;
inline constexpr std::uint16_t kDyldChainedPtrStartNone = 0xffffu;
inline constexpr std::uint16_t kDyldChainedPtrStartMulti = 0x8000u;

// Classic dyld rebase/bind opcodes
inline constexpr std::uint8_t kRebaseOpcodeMask = 0xf0u;
inline constexpr std::uint8_t kRebaseImmediateMask = 0x0fu;
inline constexpr std::uint8_t kRebaseOpcodeDone = 0x00u;
inline constexpr std::uint8_t kRebaseOpcodeSetTypeImm = 0x10u;
inline constexpr std::uint8_t kRebaseOpcodeSetSegmentAndOffsetUleb = 0x20u;
inline constexpr std::uint8_t kRebaseOpcodeAddAddrUleb = 0x30u;
inline constexpr std::uint8_t kRebaseOpcodeAddAddrImmScaled = 0x40u;
inline constexpr std::uint8_t kRebaseOpcodeDoRebaseImmTimes = 0x50u;
inline constexpr std::uint8_t kRebaseOpcodeDoRebaseUlebTimes = 0x60u;
inline constexpr std::uint8_t kRebaseOpcodeDoRebaseAddAddrUleb = 0x70u;
inline constexpr std::uint8_t kRebaseOpcodeDoRebaseUlebTimesSkippingUleb = 0x80u;

inline constexpr std::uint8_t kBindOpcodeMask = 0xf0u;
inline constexpr std::uint8_t kBindImmediateMask = 0x0fu;
inline constexpr std::uint8_t kBindOpcodeDone = 0x00u;
inline constexpr std::uint8_t kBindOpcodeSetDylibOrdinalImm = 0x10u;
inline constexpr std::uint8_t kBindOpcodeSetDylibOrdinalUleb = 0x20u;
inline constexpr std::uint8_t kBindOpcodeSetDylibSpecialImm = 0x30u;
inline constexpr std::uint8_t kBindOpcodeSetSymbolTrailingFlagsImm = 0x40u;
inline constexpr std::uint8_t kBindOpcodeSetTypeImm = 0x50u;
inline constexpr std::uint8_t kBindOpcodeSetAddendSleb = 0x60u;
inline constexpr std::uint8_t kBindOpcodeSetSegmentAndOffsetUleb = 0x70u;
inline constexpr std::uint8_t kBindOpcodeAddAddrUleb = 0x80u;
inline constexpr std::uint8_t kBindOpcodeDoBind = 0x90u;
inline constexpr std::uint8_t kBindOpcodeDoBindAddAddrUleb = 0xa0u;
inline constexpr std::uint8_t kBindOpcodeDoBindAddAddrImmScaled = 0xb0u;
inline constexpr std::uint8_t kBindOpcodeDoBindUlebTimesSkippingUleb = 0xc0u;

struct MachHeader64 {
    std::uint32_t magic;
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
    std::uint32_t reserved;
};

struct LoadCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
};

struct SegmentCommand64 {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    char segname[16];
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    std::uint32_t maxprot;
    std::uint32_t initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

struct Section64 {
    char sectname[16];
    char segname[16];
    std::uint64_t addr;
    std::uint64_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
    std::uint32_t reserved3;
};

struct EntryPointCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint64_t entryoff;
    std::uint64_t stacksize;
};

struct DylibCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t name_offset;
    std::uint32_t timestamp;
    std::uint32_t current_version;
    std::uint32_t compatibility_version;
};

struct SymtabCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t symoff;
    std::uint32_t nsyms;
    std::uint32_t stroff;
    std::uint32_t strsize;
};

struct DyldInfoCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t rebase_off;
    std::uint32_t rebase_size;
    std::uint32_t bind_off;
    std::uint32_t bind_size;
    std::uint32_t weak_bind_off;
    std::uint32_t weak_bind_size;
    std::uint32_t lazy_bind_off;
    std::uint32_t lazy_bind_size;
    std::uint32_t export_off;
    std::uint32_t export_size;
};

struct LinkeditDataCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t dataoff;
    std::uint32_t datasize;
};

struct Nlist64 {
    std::uint32_t n_strx;
    std::uint8_t n_type;
    std::uint8_t n_sect;
    std::uint16_t n_desc;
    std::uint64_t n_value;
};

struct DyldChainedFixupsHeader {
    std::uint32_t fixups_version;
    std::uint32_t starts_offset;
    std::uint32_t imports_offset;
    std::uint32_t symbols_offset;
    std::uint32_t imports_count;
    std::uint32_t imports_format;
    std::uint32_t symbols_format;
};

struct DyldChainedStartsInSegment {
    std::uint32_t size;
    std::uint16_t page_size;
    std::uint16_t pointer_format;
    std::uint64_t segment_offset;
    std::uint32_t max_valid_pointer;
    std::uint16_t page_count;
    std::uint16_t page_start[1];
};

}  // namespace finn::darwin

#endif
