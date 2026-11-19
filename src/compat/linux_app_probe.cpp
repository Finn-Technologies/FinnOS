#include "compat/linux_app_probe.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

constexpr std::size_t kMaxElfBytes = 64u * 1024u * 1024u;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtDynamic = 2;
constexpr std::uint32_t kPtInterp = 3;
constexpr std::uint64_t kDtNull = 0;
constexpr std::uint64_t kDtNeeded = 1;
constexpr std::uint64_t kDtStrtab = 5;
constexpr std::uint64_t kDtStrsz = 10;
constexpr std::size_t kMaxEntries = 4096;

unsigned char uc(char c) { return static_cast<unsigned char>(c); }

template <class T>
T read_int(std::string_view b, std::size_t off, bool little) {
    T value = 0;
    if (off > b.size() || sizeof(T) > b.size() - off) return 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const std::size_t shift_index = little ? i : (sizeof(T) - 1 - i);
        value = static_cast<T>(value |
                               (static_cast<T>(uc(b[off + i])) << (8 * shift_index)));
    }
    return value;
}

std::string sanitize(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        out.push_back(uc(c) < 0x20 || uc(c) == 0x7f ? ' ' : c);
    }
    return out;
}

std::string bounded_string(std::string_view b, std::size_t off, std::size_t limit) {
    if (off >= limit || limit > b.size()) return {};
    std::size_t end = off;
    while (end < limit && b[end] != '\0') ++end;
    return std::string(b.substr(off, end - off));
}

struct Segment {
    std::uint64_t offset = 0;
    std::uint64_t vaddr = 0;
    std::uint64_t filesz = 0;
};

std::uint64_t vaddr_to_offset(const std::vector<Segment>& loads, std::uint64_t addr,
                              bool& ok) {
    ok = false;
    for (const Segment& seg : loads) {
        if (seg.filesz > 0 && addr >= seg.vaddr && addr - seg.vaddr < seg.filesz) {
            ok = true;
            return seg.offset + (addr - seg.vaddr);
        }
    }
    return 0;
}

std::vector<std::string> library_search_dirs(const std::string& host) {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("LD_LIBRARY_PATH"); env && *env) {
        std::string value = env;
        std::size_t start = 0;
        while (start <= value.size()) {
            const std::size_t end = value.find(':', start);
            const std::string dir = value.substr(
                start, end == std::string::npos ? std::string::npos : end - start);
            if (!dir.empty()) dirs.push_back(dir);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    for (const char* dir : {"/usr/local/lib", "/usr/lib", "/lib", "/usr/local/lib64",
                            "/usr/lib64", "/lib64"}) {
        dirs.emplace_back(dir);
    }
    if (host == "x86_64") {
        dirs.emplace_back("/usr/lib/x86_64-linux-gnu");
        dirs.emplace_back("/lib/x86_64-linux-gnu");
        dirs.emplace_back("/usr/lib32");
    } else if (host == "aarch64") {
        dirs.emplace_back("/usr/lib/aarch64-linux-gnu");
        dirs.emplace_back("/lib/aarch64-linux-gnu");
    } else if (host == "riscv64") {
        dirs.emplace_back("/usr/lib/riscv64-linux-gnu");
        dirs.emplace_back("/lib/riscv64-linux-gnu");
    } else if (host == "x86") {
        dirs.emplace_back("/usr/lib/i386-linux-gnu");
        dirs.emplace_back("/lib/i386-linux-gnu");
    }

    std::set<std::string> unique(dirs.begin(), dirs.end());
    return std::vector<std::string>(unique.begin(), unique.end());
}

std::string find_library(const std::string& name,
                         const std::vector<std::string>& dirs) {
    if (name.empty() || name.find('/') != std::string::npos) return {};
    for (const std::string& dir : dirs) {
        const fs::path candidate = fs::path(dir) / name;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec) && !ec) return candidate.string();
    }
    return {};
}

std::string describe_definition(const DesktopEntry& entry) {
    return entry.source + ":" +
           (entry.desktop_file.empty() ? "<no file>" : entry.desktop_file) +
           " name='" + sanitize(entry.name) + "' exec='" + sanitize(entry.exec) +
           "' icon='" + sanitize(entry.icon) + "'";
}

bool definitions_differ(const DesktopEntry& a, const DesktopEntry& b) {
    return a.name != b.name || a.exec != b.exec || a.icon != b.icon ||
           a.source != b.source || a.desktop_file != b.desktop_file;
}

}  // namespace

const char* elf_arch_label(ElfArch arch) { return elf_arch_name(arch); }

const char* elf_class_label(ElfClass cls) {
    switch (cls) {
        case ElfClass::Elf32: return "ELF32";
        case ElfClass::Elf64: return "ELF64";
        case ElfClass::Unknown:
        default: return "unknown";
    }
}

ElfInfo inspect_elf_bytes(std::string_view bytes) {
    ElfInfo info;
    if (bytes.size() < 16) {
        info.error = "truncated ELF identification";
        return info;
    }
    if (bytes[0] != '\x7f' || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
        info.error = "not an ELF executable";
        return info;
    }
    const unsigned char elf_class = uc(bytes[4]);
    const unsigned char elf_data = uc(bytes[5]);
    if (elf_class == 1) info.elf_class = ElfClass::Elf32;
    else if (elf_class == 2) info.elf_class = ElfClass::Elf64;
    else {
        info.error = "unsupported ELF class";
        return info;
    }
    if (elf_data == 1) info.endian = ElfEndian::Little;
    else if (elf_data == 2) info.endian = ElfEndian::Big;
    else {
        info.error = "unsupported ELF byte order";
        return info;
    }
    if (uc(bytes[6]) != 1) {
        info.error = "unsupported ELF identification version";
        return info;
    }

    const bool is64 = info.elf_class == ElfClass::Elf64;
    const bool little = info.endian == ElfEndian::Little;
    if (bytes.size() < (is64 ? 64u : 52u)) {
        info.error = "truncated ELF header";
        return info;
    }

    const std::uint16_t machine = read_int<std::uint16_t>(bytes, 18, little);
    info.arch = arch_from_machine(machine);
    info.arch_name = elf_arch_name(info.arch);

    const std::uint64_t phoff = is64 ? read_int<std::uint64_t>(bytes, 32, little)
                                     : read_int<std::uint32_t>(bytes, 28, little);
    const std::uint16_t phentsize = read_int<std::uint16_t>(bytes, is64 ? 54 : 42, little);
    const std::uint16_t phnum = read_int<std::uint16_t>(bytes, is64 ? 56 : 44, little);
    const std::size_t expected_phdr = is64 ? 56u : 32u;
    if (phnum > 0 && phentsize < expected_phdr) {
        info.error = "invalid e_phentsize";
        return info;
    }

    std::vector<Segment> loads;
    std::vector<Segment> dynamics;
    const std::size_t ph_count = std::min<std::size_t>(phnum, kMaxEntries);
    for (std::size_t i = 0; i < ph_count; ++i) {
        const std::uint64_t off = phoff + static_cast<std::uint64_t>(i) * phentsize;
        if (off > bytes.size() || expected_phdr > bytes.size() - off) break;
        const std::size_t p = static_cast<std::size_t>(off);
        const std::uint32_t type = read_int<std::uint32_t>(bytes, p, little);
        Segment seg;
        if (is64) {
            seg.offset = read_int<std::uint64_t>(bytes, p + 8, little);
            seg.vaddr = read_int<std::uint64_t>(bytes, p + 16, little);
            seg.filesz = read_int<std::uint64_t>(bytes, p + 32, little);
        } else {
            seg.offset = read_int<std::uint32_t>(bytes, p + 4, little);
            seg.vaddr = read_int<std::uint32_t>(bytes, p + 8, little);
            seg.filesz = read_int<std::uint32_t>(bytes, p + 16, little);
        }
        if (type == kPtInterp && seg.filesz > 0 && seg.offset <= bytes.size() &&
            seg.filesz <= bytes.size() - seg.offset) {
            const std::size_t start = static_cast<std::size_t>(seg.offset);
            info.interpreter =
                sanitize(bounded_string(bytes, start, start + static_cast<std::size_t>(
                                                                  seg.filesz)));
        } else if (type == kPtLoad) {
            loads.push_back(seg);
        } else if (type == kPtDynamic) {
            dynamics.push_back(seg);
        }
    }

    std::uint64_t strtab_addr = 0;
    std::uint64_t strtab_size = 0;
    bool have_strtab = false;
    bool have_strtab_size = false;
    std::vector<std::uint64_t> needed_offsets;
    const std::size_t dyn_entry = is64 ? 16u : 8u;
    for (const Segment& seg : dynamics) {
        const std::size_t count = std::min<std::size_t>(
            static_cast<std::size_t>(std::min<std::uint64_t>(seg.filesz, bytes.size()) /
                                     dyn_entry),
            kMaxEntries);
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint64_t off = seg.offset + static_cast<std::uint64_t>(i) * dyn_entry;
            if (off > bytes.size() || dyn_entry > bytes.size() - off) break;
            const std::size_t d = static_cast<std::size_t>(off);
            const std::uint64_t tag = is64 ? read_int<std::uint64_t>(bytes, d, little)
                                           : read_int<std::uint32_t>(bytes, d, little);
            const std::uint64_t value = is64 ? read_int<std::uint64_t>(bytes, d + 8, little)
                                             : read_int<std::uint32_t>(bytes, d + 4, little);
            if (tag == kDtNull) break;
            if (tag == kDtNeeded) needed_offsets.push_back(value);
            else if (tag == kDtStrtab) {
                strtab_addr = value;
                have_strtab = true;
            } else if (tag == kDtStrsz) {
                strtab_size = value;
                have_strtab_size = true;
            }
        }
    }

    if (have_strtab && have_strtab_size && strtab_size > 0 &&
        strtab_size <= bytes.size()) {
        bool mapped = false;
        const std::uint64_t table_off = vaddr_to_offset(loads, strtab_addr, mapped);
        if (mapped && table_off <= bytes.size() &&
            strtab_size <= bytes.size() - table_off) {
            const std::size_t base = static_cast<std::size_t>(table_off);
            const std::size_t limit = base + static_cast<std::size_t>(strtab_size);
            for (std::uint64_t needed_off : needed_offsets) {
                if (needed_off >= strtab_size) continue;
                const std::string name =
                    bounded_string(bytes, base + static_cast<std::size_t>(needed_off), limit);
                if (!name.empty()) info.needed.push_back(sanitize(name));
            }
        }
    }

    info.valid = true;
    return info;
}

ElfInfo inspect_elf_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ElfInfo info;
        info.error = "cannot open file: " + path;
        return info;
    }
    std::string bytes;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size > 0) {
        const std::size_t wanted = std::min<std::size_t>(
            static_cast<std::size_t>(size), kMaxElfBytes);
        bytes.resize(wanted);
        in.seekg(0, std::ios::beg);
        in.read(bytes.data(), static_cast<std::streamsize>(wanted));
        bytes.resize(static_cast<std::size_t>(in.gcount()));
    }
    return inspect_elf_bytes(bytes);
}

LinuxAppReport probe_linux_binary(const std::string& path) {
    LinuxAppReport report;
    report.binary = path;
    report.host_arch = host_elf_arch();

    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec) || ec) {
        report.diagnostics.push_back("binary does not exist or is not a regular file: " +
                                     path);
        report.summary = linux_app_summary(report);
        return report;
    }
    report.has_binary = true;
    report.elf = inspect_elf_file(path);
    if (!report.elf.valid) {
        report.diagnostics.push_back("not a parseable ELF binary: " +
                                     (report.elf.error.empty() ? "unknown error"
                                                               : report.elf.error));
        report.summary = linux_app_summary(report);
        return report;
    }

    report.arch_compatible = report.elf.arch_name == report.host_arch;
    if (!report.arch_compatible) {
        report.diagnostics.push_back("architecture " + report.elf.arch_name +
                                     " does not match this " + report.host_arch +
                                     " host; FinnOS v1 does not emulate CPUs");
    }

    if (report.elf.interpreter.empty()) {
        report.diagnostics.push_back(
            "no PT_INTERP found: static or self-contained binary, no loader dependency "
            "reported");
    } else {
        std::error_code interp_ec;
        const bool present = fs::is_regular_file(report.elf.interpreter, interp_ec) &&
                             !interp_ec;
        if (present) {
            report.diagnostics.push_back("interpreter present: " +
                                         report.elf.interpreter);
        } else {
            report.diagnostics.push_back("interpreter missing: " +
                                         report.elf.interpreter);
        }
    }

    const std::vector<std::string> dirs = library_search_dirs(report.host_arch);
    for (const std::string& name : report.elf.needed) {
        LibraryStatus status;
        status.name = name;
        status.path = find_library(name, dirs);
        status.found = !status.path.empty();
        if (!status.found) report.missing_libraries.push_back(name);
        report.libraries.push_back(std::move(status));
    }
    if (report.elf.needed.empty()) {
        report.diagnostics.push_back("no DT_NEEDED entries found");
    } else {
        report.diagnostics.push_back(
            std::to_string(report.elf.needed.size()) + " needed librar" +
            (report.elf.needed.size() == 1 ? "y" : "ies") + ", " +
            std::to_string(report.missing_libraries.size()) + " missing");
        for (const std::string& name : report.missing_libraries) {
            report.diagnostics.push_back("missing library " + name +
                                         " (searched standard and multiarch paths)");
        }
    }

    report.summary = linux_app_summary(report);
    return report;
}

std::string linux_app_summary(const LinuxAppReport& report) {
    return "binary=" + sanitize(report.binary) +
           " present=" + (report.has_binary ? "yes" : "no") +
           " arch=" + (report.elf.valid ? report.elf.arch_name : "unknown") +
           " host=" + report.host_arch +
           " compatible=" + (report.arch_compatible ? "yes" : "no") +
           " interpreter=" + (report.elf.interpreter.empty()
                                  ? "none"
                                  : sanitize(report.elf.interpreter)) +
           " libraries=" + std::to_string(report.libraries.size()) +
           " missing=" + std::to_string(report.missing_libraries.size());
}

std::vector<DesktopIdConflict> detect_desktop_id_conflicts(
    const std::vector<DesktopEntry>& entries) {
    std::map<std::string, std::vector<const DesktopEntry*>> by_id;
    std::map<std::string, std::vector<const DesktopEntry*>> by_desktop;
    for (const DesktopEntry& entry : entries) {
        if (!entry.id.empty()) by_id[entry.id].push_back(&entry);
        if (!entry.desktop_id.empty()) by_desktop[entry.desktop_id].push_back(&entry);
    }

    std::vector<DesktopIdConflict> out;
    std::set<std::string> emitted;
    const auto add = [&](const std::string& id,
                         const std::vector<const DesktopEntry*>& group) {
        if (group.size() < 2) return;
        bool differs = false;
        for (std::size_t i = 1; i < group.size() && !differs; ++i) {
            differs |= definitions_differ(*group[0], *group[i]);
        }
        if (!differs) return;

        std::vector<std::string> files;
        for (const DesktopEntry* entry : group) files.push_back(entry->desktop_file);
        std::sort(files.begin(), files.end());
        std::string fingerprint;
        for (const std::string& file : files) fingerprint += file + "\n";
        if (!emitted.insert(fingerprint).second) return;

        DesktopIdConflict conflict;
        conflict.id = id;
        for (const DesktopEntry* entry : group) {
            conflict.details.push_back(describe_definition(*entry));
        }
        out.push_back(std::move(conflict));
    };

    for (const auto& [id, group] : by_id) add("id:" + id, group);
    for (const auto& [desktop_id, group] : by_desktop) {
        add("desktop-id:" + desktop_id, group);
    }
    return out;
}

std::string desktop_conflicts_summary(const std::vector<DesktopIdConflict>& conflicts) {
    if (conflicts.empty()) return "No desktop-id conflicts found.";
    std::string out = std::to_string(conflicts.size()) + " desktop-id conflict(s):";
    for (const DesktopIdConflict& conflict : conflicts) {
        out += " " + sanitize(conflict.id) + " (" +
               std::to_string(conflict.details.size()) + " definitions)";
    }
    return out;
}

}  // namespace finn::compat
