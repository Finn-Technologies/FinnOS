// AppImage inspection and honest launch planning for Linux app compatibility.
// Type 1 is detected from the ELF header at offset 0 with the "AI\x01" magic at
// offset 8 and an ISO9660 volume descriptor at 0x8000; Type 2 from the ELF
// header with "AI\x02" at offset 8 and a big-endian payload offset at 11;
// Type 3 from the draft "AI\x03" marker, which FinnOS v1 detects but does not
// claim to run. Nothing here guesses a format from a filename and nothing
// executes a shell. Pure C++20 so finn-session, Finn Store, Finn Files and
// headless tests share one implementation.

#ifndef FINN_COMPAT_APPIMAGE_RUNNER_HPP
#define FINN_COMPAT_APPIMAGE_RUNNER_HPP

#include "compat/elf_arch.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace finn::compat {

enum class AppImageKind {
    Unknown,
    Type1,           // Legacy: ELF runtime + ISO9660 payload at 0x8000.
    Type2,           // ELF runtime + squashfs payload at a recorded offset.
    Type3,           // Unfinalized draft marker: detected, never claimed runnable.
    PlainElf,        // A real ELF executable that is not an AppImage.
};

struct AppImageInfo {
    bool valid = false;
    AppImageKind kind = AppImageKind::Unknown;
    std::string path;
    std::string error;
    ElfArch arch = ElfArch::Unknown;
    std::string arch_name;      // "x86_64", "aarch64", ... or "unknown".
    std::string host_arch;
    bool arch_compatible = false;
    bool fuse_required = false;
    std::uint64_t payload_offset = 0;      // Parsed offset of the filesystem image.
    bool payload_offset_valid = false;     // True when a payload magic was confirmed.
    std::string payload_magic;             // "squashfs (hsqs)", "ISO9660 (CD001)", ...
    std::string summary;
};

struct FuseRuntimeStatus {
    bool device = false;    // /dev/fuse exists.
    bool helper = false;    // fusermount3 or fusermount is on PATH.
    std::string helper_name;
    std::string diagnostics;
};

struct AppImageLaunchOptions {
    bool prefer_extract_and_run = false;  // Skip FUSE even when it is available.
    bool check_runtime = true;            // Probe /dev/fuse + fusermount(3).
    bool use_sandbox_if_available = false;  // Wrap in bwrap only when bwrap exists.
};

struct AppImageLaunchPlan {
    bool ok = false;
    std::vector<std::string> argv;                       // Never uses a shell.
    std::vector<std::pair<std::string, std::string>> env;
    std::string display_name;
    std::string diagnostics;
    std::string error;
};

struct AppImageCandidate {
    std::string path;
    std::string display_name;
    std::string id;
    AppImageInfo info;
};

std::string appimage_host_arch();
const char* appimage_kind_label(AppImageKind kind);

// Checks the actual runtime rather than assuming the base image has FUSE.
FuseRuntimeStatus appimage_fuse_status();
bool appimage_fuse_runtime_available();
bool appimage_bubblewrap_available();

AppImageInfo inspect_appimage_bytes(std::string_view bytes, std::string_view path = {});
AppImageInfo inspect_appimage_file(const std::string& path);

AppImageLaunchPlan build_appimage_launch_plan(const std::string& path,
                                              const AppImageLaunchOptions& options = {});
// Plans the side-effecting --appimage-extract verb. Type 2 runtimes implement
// it; legacy Type 1 images do not, and are refused rather than faked.
AppImageLaunchPlan build_appimage_extract_plan(const std::string& path);

// Ensures the executable bit (+x) is set on a local AppImage.
bool ensure_appimage_executable(const std::string& path);

// Copies an AppImage into ~/.local/share/finn/appimages (or dest_dir), marks it
// executable, and returns the installed path (empty on failure).
std::string integrate_appimage(const std::string& path,
                               const std::string& dest_dir = {},
                               std::string* error = nullptr);

std::vector<AppImageCandidate> discover_appimages(const std::string& dir);
std::vector<AppImageCandidate> discover_appimages(const std::vector<std::string>& dirs);

// Headless self-test over synthetic magic bytes. Requires the matching .cpp at
// link time. Returns true when every check passes and prints failures to stderr.
inline bool appimage_runner_self_test() {
    bool ok = true;
    const auto check = [&](bool condition, const char* what) {
        if (!condition) {
            std::fprintf(stderr, "appimage_runner_self_test: %s\n", what);
            ok = false;
        }
    };

    const auto make_elf = [](unsigned char ai_type, std::uint32_t payload_off) {
        std::string bytes(64, '\0');
        bytes[0] = '\x7f';
        bytes[1] = 'E';
        bytes[2] = 'L';
        bytes[3] = 'F';
        bytes[4] = 2;  // ELFCLASS64
        bytes[5] = 1;  // ELFDATA2LSB
        bytes[6] = 1;  // EV_CURRENT
        if (ai_type != 0) {
            bytes[8] = 'A';
            bytes[9] = 'I';
            bytes[10] = static_cast<char>(ai_type);
        }
        for (int i = 0; i < 4; ++i) {
            bytes[11 + i] = static_cast<char>((payload_off >> (24 - 8 * i)) & 0xffu);
        }
        bytes[18] = 62;  // e_machine, EM_X86_64 (little-endian)
        return bytes;
    };

    auto type2 = make_elf(2, 0x1000);
    type2.resize(0x1004, '\0');
    type2[0x1000] = 'h';
    type2[0x1001] = 's';
    type2[0x1002] = 'q';
    type2[0x1003] = 's';
    const AppImageInfo i2 = inspect_appimage_bytes(type2, "synthetic.AppImage");
    check(i2.kind == AppImageKind::Type2, "type 2 kind");
    check(i2.valid, "type 2 valid");
    check(i2.arch == ElfArch::X86_64, "type 2 architecture");
    check(i2.payload_offset == 0x1000, "type 2 offset parsing");
    check(i2.payload_offset_valid, "type 2 payload magic");
    check(!i2.fuse_required, "type 2 FUSE optional");

    auto type1 = make_elf(1, 0);
    type1.resize(0x8006, '\0');
    type1[0x8001] = 'C';
    type1[0x8002] = 'D';
    type1[0x8003] = '0';
    type1[0x8004] = '0';
    type1[0x8005] = '1';
    const AppImageInfo i1 = inspect_appimage_bytes(type1);
    check(i1.kind == AppImageKind::Type1, "type 1 kind");
    check(i1.valid && i1.fuse_required, "type 1 valid and FUSE-required");
    check(i1.payload_offset == 0x8000 && i1.payload_offset_valid,
          "type 1 ISO9660 offset");

    const AppImageInfo i3 = inspect_appimage_bytes(make_elf(3, 0));
    check(i3.kind == AppImageKind::Type3, "type 3 draft detected");
    check(!i3.valid && !i3.error.empty(), "type 3 never claimed runnable");

    const AppImageInfo plain = inspect_appimage_bytes(make_elf(0, 0));
    check(plain.kind == AppImageKind::PlainElf && !plain.valid, "plain ELF rejected");

    const AppImageInfo junk = inspect_appimage_bytes("not an elf at all");
    check(junk.kind == AppImageKind::Unknown && !junk.error.empty(), "junk rejected");

    check(appimage_kind_label(AppImageKind::Type2) != nullptr, "kind label");
    const AppImageLaunchPlan missing = build_appimage_launch_plan("/no/such/image.AppImage");
    check(!missing.ok && !missing.error.empty(), "missing image is an honest error");
    return ok;
}

}  // namespace finn::compat

#endif
