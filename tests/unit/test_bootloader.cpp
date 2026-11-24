// ASan/UBSan unit tests for FinnOS graphical UEFI bootloader pure-C core:
// loader.conf, UAPI BLS entries, boot-counting decrement, UKI PE section
// extraction (.osrel + .cmdline), OS icon classification, hit-testing, and
// GOP Startup Manager framebuffer rendering.

#include "boot/bls_parser.h"
#include "boot/boot_ui.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> make_synthetic_uki(const std::string& osrel, const std::string& cmdline) {
    // Minimal PE32+ image with 2 sections: .osrel and .cmdline
    std::vector<uint8_t> pe(0x600, 0);
    pe[0] = 'M'; pe[1] = 'Z';
    uint32_t pe_off = 0x80;
    std::memcpy(&pe[0x3C], &pe_off, 4);
    pe[pe_off] = 'P'; pe[pe_off + 1] = 'E';
    uint16_t nsecs = 2;
    uint16_t optsz = 240;
    std::memcpy(&pe[pe_off + 6], &nsecs, 2);
    std::memcpy(&pe[pe_off + 20], &optsz, 2);

    size_t st = pe_off + 24 + optsz;
    auto write_sec = [&](size_t idx, const char* name, uint32_t roff, const std::string& body) {
        uint8_t* sh = &pe[st + idx * 40];
        std::memcpy(sh, name, std::strlen(name));
        uint32_t sz = static_cast<uint32_t>(body.size());
        std::memcpy(sh + 8, &sz, 4);   // VirtualSize
        std::memcpy(sh + 16, &sz, 4);  // SizeOfRawData
        std::memcpy(sh + 20, &roff, 4);
        std::memcpy(&pe[roff], body.data(), body.size());
    };
    write_sec(0, ".osrel", 0x300, osrel);
    write_sec(1, ".cmdline", 0x480, cmdline);
    return pe;
}

void test_loader_conf_and_bls() {
    BootLoaderConfig cfg;
    bls_config_init(&cfg);
    const char* lconf =
        "# FinnOS loader\n"
        "default finnos-*.conf\n"
        "timeout 5\n"
        "editor no\n"
        "auto-entries yes\n";
    bls_parse_loader_conf(lconf, std::strlen(lconf), &cfg);
    assert(std::string(cfg.default_pattern) == "finnos-*.conf");
    assert(cfg.timeout_sec == 5);
    assert(cfg.editor_allowed == 0);
    assert(cfg.auto_entries == 1);
    assert(bls_glob_match(cfg.default_pattern, "finnos-a.conf") == 1);
    assert(bls_glob_match(cfg.default_pattern, "ubuntu.conf") == 0);

    const char* entry_txt =
        "title FinnOS (A)\n"
        "version 1.0.0\n"
        "sort-key finnos\n"
        "linux /finnos/a/vmlinuz\n"
        "initrd /intel-ucode.img\n"
        "initrd /finnos/a/initramfs.img\n"
        "options root=PARTLABEL=FINNOS_A rw quiet splash\n";
    BootEntry e;
    assert(bls_parse_entry_conf("finnos-a+3-0.conf", entry_txt, std::strlen(entry_txt), &e) == 1);
    assert(std::string(e.title) == "FinnOS (A)");
    assert(e.initrd_count == 2);
    assert(e.icon == OS_ICON_FINNOS);
    assert(e.has_boot_count == 1 && e.tries_left == 3 && e.tries_done == 0);

    char dec[64];
    assert(bls_format_decremented_filename("finnos-a+3-0.conf", dec, sizeof(dec)) == 1);
    assert(std::string(dec) == "finnos-a+2-1.conf");
}

void test_uki_pe_and_sorting() {
    auto uki = make_synthetic_uki(
        "ID=finnos\nPRETTY_NAME=\"FinnOS UKI\"\nVERSION_ID=1.0\n",
        "root=UUID=deadbeef rw quiet splash\n");
    BootEntry ue;
    assert(bls_parse_uki_pe("finnos-uki.efi", "\\EFI\\Linux\\finnos-uki.efi",
                            uki.data(), uki.size(), &ue) == 1);
    assert(std::string(ue.title) == "FinnOS UKI");
    assert(std::string(ue.version) == "1.0");
    assert(ue.icon == OS_ICON_FINNOS);
    assert(ue.kind == ENTRY_KIND_UKI);

    BootEntry list[4]{};
    std::strcpy(list[0].id, "recovery.conf"); std::strcpy(list[0].title, "FinnOS Recovery"); list[0].icon = OS_ICON_RECOVERY;
    std::strcpy(list[1].id, "ubuntu.conf");   std::strcpy(list[1].title, "Ubuntu");          list[1].icon = OS_ICON_LINUX;
    std::strcpy(list[2].id, "auto-windows");  std::strcpy(list[2].title, "Windows");         list[2].icon = OS_ICON_WINDOWS;
    list[3] = ue;
    bls_sort_entries(list, 4);
    assert(list[0].icon == OS_ICON_FINNOS);
    assert(list[1].icon == OS_ICON_WINDOWS);
    assert(list[2].icon == OS_ICON_LINUX);
    assert(list[3].icon == OS_ICON_RECOVERY);
}

void test_gop_render_and_hit_test() {
    BootEntry entries[5]{};
    const char* titles[5] = {"FinnOS", "Windows", "macOS", "Ubuntu", "Recovery"};
    const BootOsIcon icons[5] = {OS_ICON_FINNOS, OS_ICON_WINDOWS, OS_ICON_MACOS, OS_ICON_LINUX, OS_ICON_RECOVERY};
    for (int i = 0; i < 5; ++i) {
        std::strcpy(entries[i].id, titles[i]);
        std::strcpy(entries[i].title, titles[i]);
        entries[i].icon = icons[i];
    }

    constexpr int W = 1024, H = 768;
    std::vector<uint32_t> fb(W * H, 0);
    BootUiState st{};
    st.selected_index = 1;
    st.hover_index = 2;
    st.countdown_ms = 2400;
    st.countdown_total_ms = 3000;
    st.pointer_active = 1;
    st.pointer_x = W / 2;
    st.pointer_y = H / 2;
    boot_ui_render(fb.data(), W, H, entries, 5, &st);

    // Verify non-uniform rendered pixels across cards and header.
    size_t non_bg = 0;
    for (uint32_t px : fb) {
        if (px != fb[0]) ++non_bg;
    }
    assert(non_bg > 40000);

    BootCardLayout layout[5];
    boot_ui_compute_layout(W, H, 5, layout);
    int hit_arrow = 0;
    assert(boot_ui_hit_test(W, H, 5, 1, layout[3].x + 20, layout[3].y + 20, &hit_arrow) == 3);
    assert(hit_arrow == 0);
    assert(boot_ui_hit_test(W, H, 5, 1, layout[1].arrow_cx, layout[1].arrow_cy, &hit_arrow) == 1);
    assert(hit_arrow == 1);
}

}  // namespace

int main() {
    test_loader_conf_and_bls();
    test_uki_pe_and_sorting();
    test_gop_render_and_hit_test();
    std::puts("test_bootloader: ok");
    return 0;
}
