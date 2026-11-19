// Honest pre-flight diagnostics for native Linux binaries and desktop entries.
// Parses ELF headers, the PT_INTERP path and DT_NEEDED entries directly from
// bytes; checks the host architecture; resolves library names on standard
// paths; and reports desktop-id collisions. Pure C++20 and headless-testable.

#ifndef FINN_COMPAT_LINUX_APP_PROBE_HPP
#define FINN_COMPAT_LINUX_APP_PROBE_HPP

#include "compat/desktop_entry.hpp"
#include "compat/elf_arch.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

enum class ElfClass { Unknown, Elf32, Elf64 };
enum class ElfEndian { Unknown, Little, Big };

struct ElfInfo {
    bool valid = false;
    ElfClass elf_class = ElfClass::Unknown;
    ElfEndian endian = ElfEndian::Unknown;
    ElfArch arch = ElfArch::Unknown;
    std::string arch_name;
    std::string interpreter;          // PT_INTERP (dynamic loader) path, if any.
    std::vector<std::string> needed;  // DT_NEEDED sonames, in file order.
    std::string error;
};

struct LibraryStatus {
    std::string name;
    bool found = false;
    std::string path;                 // Empty when not found on a standard path.
};

struct LinuxAppReport {
    bool has_binary = false;
    std::string binary;
    ElfInfo elf;
    std::string host_arch;
    bool arch_compatible = false;
    std::vector<LibraryStatus> libraries;
    std::vector<std::string> missing_libraries;
    std::vector<std::string> diagnostics;
    std::string summary;
};

struct DesktopIdConflict {
    std::string id;
    std::vector<std::string> details;
};

const char* elf_arch_label(ElfArch arch);
const char* elf_class_label(ElfClass cls);

// Parses an ELF image from memory with strict bounds checks. Works for both
// 32- and 64-bit, little- and big-endian files.
ElfInfo inspect_elf_bytes(std::string_view bytes);
ElfInfo inspect_elf_file(const std::string& path);

LinuxAppReport probe_linux_binary(const std::string& path);
std::string linux_app_summary(const LinuxAppReport& report);

std::vector<DesktopIdConflict> detect_desktop_id_conflicts(
    const std::vector<DesktopEntry>& entries);
std::string desktop_conflicts_summary(const std::vector<DesktopIdConflict>& conflicts);

// Headless self-test over synthetic headers and desktop entries. Requires the
// matching .cpp at link time; prints failures to stderr and returns true when
// all checks pass.
inline bool linux_app_probe_self_test() {
    bool ok = true;
    const auto check = [&](bool condition, const char* what) {
        if (!condition) {
            std::fprintf(stderr, "linux_app_probe_self_test: %s\n", what);
            ok = false;
        }
    };

    std::string elf(64, '\0');
    elf[0] = '\x7f';
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;  // ELFCLASS64
    elf[5] = 1;  // ELFDATA2LSB
    elf[6] = 1;  // EV_CURRENT
    elf[16] = 2; // ET_EXEC
    elf[18] = 62;  // EM_X86_64
    elf[52] = 64;  // e_ehsize
    const ElfInfo info = inspect_elf_bytes(elf);
    check(info.valid, "minimal ELF accepted");
    check(info.elf_class == ElfClass::Elf64, "ELF class parsed");
    check(info.endian == ElfEndian::Little, "ELF endian parsed");
    check(info.arch == ElfArch::X86_64, "ELF architecture parsed");
    check(info.interpreter.empty() && info.needed.empty(), "no phantom fields");

    check(!inspect_elf_bytes("").valid, "empty input rejected");
    check(!inspect_elf_bytes("not an elf").valid, "junk input rejected");
    check(std::string(elf_arch_label(ElfArch::Aarch64)) == "aarch64", "arch label");
    check(std::string(elf_class_label(ElfClass::Elf32)) == "ELF32", "class label");

    DesktopEntry first;
    first.id = "linux:demo";
    first.desktop_id = "demo";
    first.name = "Demo One";
    first.exec = "/usr/bin/one";
    first.source = "desktop";
    first.desktop_file = "/usr/share/applications/demo.desktop";
    DesktopEntry second = first;
    second.name = "Demo Two";
    second.exec = "/usr/bin/two";
    second.desktop_file = "/home/finn/.local/share/applications/demo.desktop";
    const auto conflicts = detect_desktop_id_conflicts({first, second});
    check(conflicts.size() == 1, "desktop id conflict found");
    check(!desktop_conflicts_summary(conflicts).empty(), "conflict summary");

    const LinuxAppReport missing = probe_linux_binary("/no/such/binary");
    check(!missing.has_binary && !missing.diagnostics.empty(),
          "missing binary reported honestly");
    return ok;
}

}  // namespace finn::compat

#endif
