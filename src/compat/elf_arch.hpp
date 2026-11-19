// One copy of the ELF architecture vocabulary shared by the AppImage runner
// and the native Linux binary probe. Header-only: no build order dependency.

#ifndef FINN_COMPAT_ELF_ARCH_HPP
#define FINN_COMPAT_ELF_ARCH_HPP

#include <cstdint>
#include <string>

namespace finn::compat {

enum class ElfArch {
    Unknown,
    X86,
    X86_64,
    Arm,
    Aarch64,
    Riscv64,
    Other,
};

inline ElfArch arch_from_machine(std::uint16_t machine) {
    switch (machine) {
        case 3: return ElfArch::X86;         // EM_386
        case 40: return ElfArch::Arm;        // EM_ARM
        case 62: return ElfArch::X86_64;     // EM_X86_64
        case 183: return ElfArch::Aarch64;   // EM_AARCH64
        case 243: return ElfArch::Riscv64;   // EM_RISCV
        default: return ElfArch::Other;
    }
}

inline const char* elf_arch_name(ElfArch arch) {
    switch (arch) {
        case ElfArch::X86: return "x86";
        case ElfArch::X86_64: return "x86_64";
        case ElfArch::Arm: return "arm";
        case ElfArch::Aarch64: return "aarch64";
        case ElfArch::Riscv64: return "riscv64";
        case ElfArch::Other: return "other";
        case ElfArch::Unknown:
        default: return "unknown";
    }
}

inline std::string host_elf_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__)
    return "arm";
#elif defined(__riscv) && __riscv_xlen == 64
    return "riscv64";
#else
    return "unknown";
#endif
}

}  // namespace finn::compat

#endif
