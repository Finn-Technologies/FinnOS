#include "compat/appimage_runner.hpp"

#include "compat/desktop_entry.hpp"
#include "compat/linux_sandbox.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

// ELF headers and the first program headers are small; a Type 1 ISO9660 marker
// lives at 0x8000. A bounded prefix keeps a hostile multi-gigabyte image from
// being read into memory. Payload magic confirmation seeks separately.
constexpr std::size_t kMaxInspectedBytes = 64u * 1024u * 1024u;
constexpr std::uint64_t kType1Offset = 0x8000u;

unsigned char uc(char c) { return static_cast<unsigned char>(c); }

bool has_bytes(std::string_view b, std::size_t off, std::string_view magic) {
    return off <= b.size() && magic.size() <= b.size() - off &&
           b.substr(off, magic.size()) == magic;
}

bool is_elf_header(std::string_view b, std::size_t off) {
    return has_bytes(b, off, "\x7f"
                            "ELF");
}

std::uint16_t read_u16(std::string_view b, std::size_t off, bool little) {
    if (off > b.size() || 2 > b.size() - off) return 0;
    const auto a = static_cast<std::uint16_t>(uc(b[off]));
    const auto c = static_cast<std::uint16_t>(uc(b[off + 1]));
    return little ? static_cast<std::uint16_t>(a | (c << 8))
                  : static_cast<std::uint16_t>((a << 8) | c);
}

// The Type 2 payload offset is a 32-bit big-endian integer at byte 11.
std::uint32_t read_u32_be(std::string_view b, std::size_t off) {
    if (off > b.size() || 4 > b.size() - off) return 0;
    return (static_cast<std::uint32_t>(uc(b[off])) << 24) |
           (static_cast<std::uint32_t>(uc(b[off + 1])) << 16) |
           (static_cast<std::uint32_t>(uc(b[off + 2])) << 8) |
           static_cast<std::uint32_t>(uc(b[off + 3]));
}

std::string clean(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        out.push_back(uc(c) < 0x20 || uc(c) == 0x7f || c == '|' ? ' ' : c);
    }
    return out;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(uc(s[off + i])) != std::tolower(uc(suffix[i]))) return false;
    }
    return true;
}

std::string slug(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (std::isalnum(uc(c))) {
            out.push_back(static_cast<char>(std::tolower(uc(c))));
        } else if (c == '-' || c == '_' || c == '.') {
            out.push_back(c);
        } else {
            out.push_back('-');
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "app" : out;
}

std::string payload_magic_at(std::string_view bytes, std::uint64_t offset,
                             AppImageKind kind) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return {};
    const std::size_t off = static_cast<std::size_t>(offset);
    if (kind == AppImageKind::Type1) {
        return has_bytes(bytes, off + 1, "CD001") ? "ISO9660 (CD001)" : std::string{};
    }
    if (has_bytes(bytes, off, "hsqs") || has_bytes(bytes, off, "sqsh")) {
        return "squashfs";
    }
    return {};
}

}  // namespace

std::string appimage_host_arch() { return host_elf_arch(); }

const char* appimage_kind_label(AppImageKind kind) {
    switch (kind) {
        case AppImageKind::Type1: return "AppImage Type 1 (legacy)";
        case AppImageKind::Type2: return "AppImage Type 2";
        case AppImageKind::Type3: return "AppImage Type 3 (draft, unsupported)";
        case AppImageKind::PlainElf: return "ELF executable (not an AppImage)";
        case AppImageKind::Unknown:
        default: return "Unknown";
    }
}

FuseRuntimeStatus appimage_fuse_status() {
    FuseRuntimeStatus status;
    std::error_code ec;
    status.device = fs::exists(fs::path("/dev/fuse"), ec) && !ec;
    if (executable_exists("fusermount3")) {
        status.helper = true;
        status.helper_name = "fusermount3";
    } else if (executable_exists("fusermount")) {
        status.helper = true;
        status.helper_name = "fusermount";
    }

    if (status.device && status.helper) {
        status.diagnostics = "FUSE runtime present (/dev/fuse + " + status.helper_name +
                             ").";
    } else {
        status.diagnostics = "FUSE unavailable:";
        if (!status.device) status.diagnostics += " /dev/fuse is missing";
        if (!status.helper) status.diagnostics += " no fusermount3 or fusermount on PATH";
    }
    return status;
}

bool appimage_fuse_runtime_available() {
    const FuseRuntimeStatus status = appimage_fuse_status();
    return status.device && status.helper;
}

bool appimage_bubblewrap_available() { return executable_exists("bwrap"); }

AppImageInfo inspect_appimage_bytes(std::string_view bytes, std::string_view path) {
    AppImageInfo info;
    info.path = std::string(path);
    info.host_arch = appimage_host_arch();

    const auto done = [&]() {
        if (info.summary.empty()) {
            info.summary = clean(info.error.empty() ? "unrecognized binary" : info.error);
        } else {
            info.summary = clean(info.summary);
        }
        return info;
    };

    if (!is_elf_header(bytes, 0)) {
        info.error = "not an ELF executable (no AppImage magic found)";
        return done();
    }
    if (bytes.size() < 20) {
        info.error = "truncated ELF header";
        return done();
    }
    const unsigned char elf_class = uc(bytes[4]);
    const unsigned char elf_data = uc(bytes[5]);
    if (elf_class != 1 && elf_class != 2) {
        info.error = "unsupported ELF class (expected ELF32 or ELF64)";
        return done();
    }
    if (elf_data != 1 && elf_data != 2) {
        info.error = "unsupported ELF byte order";
        return done();
    }

    const bool has_ai = bytes.size() >= 11 && bytes[8] == 'A' && bytes[9] == 'I';
    if (has_ai) {
        switch (bytes[10]) {
            case 1:
                info.kind = AppImageKind::Type1;
                info.payload_offset = kType1Offset;
                info.fuse_required = true;
                break;
            case 2:
                info.kind = AppImageKind::Type2;
                break;
            case 3:
                info.kind = AppImageKind::Type3;
                info.error =
                    "AppImage Type 3 is an unfinalized draft; FinnOS v1 has no runtime "
                    "for it and will not claim it can run";
                return done();
            default:
                info.error = "unrecognized AppImage magic version byte";
                return done();
        }
    } else if (has_bytes(bytes, static_cast<std::size_t>(kType1Offset) + 1, "CD001")) {
        // The legacy spec says the AI\x01 marker is recommended, not required.
        // An ELF stub plus an ISO9660 volume descriptor at sector 16 is a real
        // Type 1 image even when the marker was omitted by an old tool.
        info.kind = AppImageKind::Type1;
        info.payload_offset = kType1Offset;
        info.fuse_required = true;
    } else {
        info.kind = AppImageKind::PlainElf;
        info.error = "ELF executable without AppImage magic";
        return done();
    }

    if (info.kind == AppImageKind::Type2) {
        if (bytes.size() < 15) {
            info.error = "truncated Type 2 AppImage offset field";
            return done();
        }
        info.payload_offset = read_u32_be(bytes, 11);
        if (info.payload_offset < 64) {
            info.error = "invalid Type 2 payload offset (overlaps the ELF runtime)";
            return done();
        }
    }

    info.payload_magic = payload_magic_at(bytes, info.payload_offset, info.kind);
    info.payload_offset_valid = !info.payload_magic.empty();

    const bool little = elf_data == 1;
    const std::uint16_t machine = read_u16(bytes, 18, little);
    info.arch = arch_from_machine(machine);
    info.arch_name = elf_arch_name(info.arch);
    info.arch_compatible =
        info.arch != ElfArch::Unknown && info.arch_name == info.host_arch;
    info.valid = true;

    info.summary = std::string(appimage_kind_label(info.kind)) + " for " +
                   info.arch_name + " (host " + info.host_arch + "), payload offset " +
                   std::to_string(info.payload_offset) +
                   (info.payload_offset_valid ? " (" + info.payload_magic + ")"
                                              : " (magic not confirmed)") +
                   "; " + (info.fuse_required ? "requires FUSE" : "FUSE optional") +
                   "; " + (info.arch_compatible ? "architecture matches"
                                                : "architecture does not match");
    return done();
}

AppImageInfo inspect_appimage_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        AppImageInfo info;
        info.path = path;
        info.host_arch = appimage_host_arch();
        info.error = "cannot open file: " + path;
        info.summary = clean(info.error);
        return info;
    }

    std::string bytes;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size > 0) {
        const std::size_t wanted = std::min<std::size_t>(
            static_cast<std::size_t>(size), kMaxInspectedBytes);
        bytes.resize(wanted);
        in.seekg(0, std::ios::beg);
        in.read(bytes.data(), static_cast<std::streamsize>(wanted));
        bytes.resize(static_cast<std::size_t>(in.gcount()));
    }
    AppImageInfo info = inspect_appimage_bytes(bytes, path);

    // The payload can start beyond the bounded prefix. Seek there explicitly so
    // confirmation is based on real bytes, never on an assumed offset.
    if (info.valid && !info.payload_offset_valid &&
        info.payload_offset <= static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(info.payload_offset), std::ios::beg);
        if (in) {
            char buf[6] = {};
            in.read(buf, info.kind == AppImageKind::Type1 ? 6 : 4);
            const std::string_view got(buf, static_cast<std::size_t>(in.gcount()));
            if (info.kind == AppImageKind::Type1 && got.size() >= 6 &&
                got.substr(1, 5) == "CD001") {
                info.payload_magic = "ISO9660 (CD001)";
                info.payload_offset_valid = true;
            } else if (info.kind == AppImageKind::Type2 &&
                       (has_bytes(got, 0, "hsqs") || has_bytes(got, 0, "sqsh"))) {
                info.payload_magic = "squashfs";
                info.payload_offset_valid = true;
            }
            if (info.payload_offset_valid) {
                info.summary = clean(info.summary + "; payload magic confirmed at offset");
            }
        }
    }
    return info;
}

AppImageLaunchPlan build_appimage_launch_plan(const std::string& path,
                                              const AppImageLaunchOptions& options) {
    AppImageLaunchPlan plan;
    plan.display_name = fs::path(path).stem().string();
    const AppImageInfo info = inspect_appimage_file(path);
    if (!info.valid) {
        plan.error = info.error.empty() ? "not a supported AppImage" : info.error;
        plan.diagnostics = info.summary;
        return plan;
    }
    if (!info.arch_compatible) {
        plan.error = "AppImage architecture " + info.arch_name +
                     " does not match this " + info.host_arch + " host";
        plan.diagnostics = "Refusing to launch a foreign-architecture image: FinnOS v1 "
                           "does not emulate other CPU architectures.";
        return plan;
    }

    const FuseRuntimeStatus fuse = appimage_fuse_status();
    const bool fuse_available = options.check_runtime ? (fuse.device && fuse.helper) : true;
    const bool use_fuse = fuse_available && !options.prefer_extract_and_run;

    if (use_fuse) {
        plan.argv = {path};
        plan.diagnostics = std::string(appimage_kind_label(info.kind)) +
                           " runs through its embedded runtime using FUSE.";
    } else if (info.kind == AppImageKind::Type2) {
        plan.argv = {path, "--appimage-extract-and-run"};
        plan.env.emplace_back("APPIMAGE_EXTRACT_AND_RUN", "1");
        plan.diagnostics = fuse.diagnostics +
                           " This Type 2 image extracts itself into a temporary "
                           "directory on every launch. That is slower and uses disk "
                           "space, but it is a real execution path, not a simulated one.";
    } else {
        plan.error = "legacy Type 1 AppImage needs /dev/fuse and fusermount to mount "
                     "its ISO9660 payload, which are not available; refusing to claim "
                     "it can run here";
        plan.diagnostics = "FUSE status: " + fuse.diagnostics;
        return plan;
    }

    plan.diagnostics +=
        " FinnOS v1 has no enforced per-app sandbox: this program runs with your "
        "user authority.";
    if (options.use_sandbox_if_available) {
        if (appimage_bubblewrap_available()) {
            const std::vector<std::string> wrapped = build_bwrap_argv(plan.argv, {});
            if (!wrapped.empty()) {
                plan.argv = wrapped;
                plan.diagnostics +=
                    " A bubblewrap namespace wrapper was added; it is not an enforced "
                    "permission sandbox.";
            }
        } else {
            plan.diagnostics +=
                " A sandbox was requested, but bwrap is not installed; no isolation "
                "was added.";
        }
    } else if (appimage_bubblewrap_available()) {
        plan.diagnostics +=
            " bubblewrap is installed but was not requested for this launch.";
    }
    plan.ok = true;
    return plan;
}

AppImageLaunchPlan build_appimage_extract_plan(const std::string& path) {
    AppImageLaunchPlan plan;
    plan.display_name = fs::path(path).stem().string();
    const AppImageInfo info = inspect_appimage_file(path);
    if (!info.valid) {
        plan.error = info.error.empty() ? "not a supported AppImage" : info.error;
        plan.diagnostics = info.summary;
        return plan;
    }
    if (info.kind != AppImageKind::Type2) {
        plan.error = "legacy Type 1 runtimes do not implement --appimage-extract; "
                     "mount the ISO9660 payload or use an ISO extractor instead";
        plan.diagnostics = "Refusing to advertise an extraction verb the runtime "
                           "does not have.";
        return plan;
    }
    plan.argv = {path, "--appimage-extract"};
    plan.diagnostics =
        "Extracts the image into ./squashfs-root in the current directory. "
        "Run ./squashfs-root/AppRun afterwards; extraction does not start the app.";
    plan.ok = true;
    return plan;
}

std::vector<AppImageCandidate> discover_appimages(const std::string& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec) || ec) return {};

    std::vector<AppImageCandidate> out;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        if (!ends_with_ci(entry.path().extension().string(), ".appimage")) continue;
        const std::string path = entry.path().string();
        AppImageCandidate candidate;
        candidate.path = path;
        candidate.display_name = entry.path().stem().string();
        candidate.info = inspect_appimage_file(path);
        if (!candidate.info.valid || !candidate.info.arch_compatible) continue;
        candidate.id = "appimage:" + slug(candidate.display_name);
        out.push_back(std::move(candidate));
    }
    std::sort(out.begin(), out.end(), [](const AppImageCandidate& a,
                                         const AppImageCandidate& b) {
        return a.display_name < b.display_name;
    });
    return out;
}

std::vector<AppImageCandidate> discover_appimages(const std::vector<std::string>& dirs) {
    std::vector<AppImageCandidate> out;
    for (const std::string& dir : dirs) {
        for (auto& candidate : discover_appimages(dir)) out.push_back(std::move(candidate));
    }
    std::sort(out.begin(), out.end(), [](const AppImageCandidate& a,
                                         const AppImageCandidate& b) {
        return a.display_name < b.display_name;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const AppImageCandidate& a,
                                                     const AppImageCandidate& b) {
                  return a.path == b.path;
              }),
              out.end());
    return out;
}

}  // namespace finn::compat
