// finn-boot: macOS Startup Manager-inspired UEFI GOP Graphical Bootloader
// layered directly onto the systemd-boot / UAPI Boot Loader Specification.
//
// Unity-compiled freestanding C for x86_64 and aarch64 UEFI (PE32+ Subsystem 10).
// Preserves:
//   - /loader/loader.conf & /loader/entries/*.conf (BLS Type #1)
//   - systemd-boot boot-counting (+<left>-<done>.conf)
//   - /EFI/Linux/*.efi Unified Kernel Images (BLS Type #2, .osrel + .cmdline)
//   - Automatic Windows & macOS EFI detection + UEFI Firmware Setup
//   - systemd Boot Loader Interface EFI variables (4a67b082-0a4c-41cf-b6c7-440b29bb8c4f)
//   - Linux EFI_LOAD_FILE2_PROTOCOL multi-initrd handoff
//   - Keyboard + UEFI Simple Pointer navigation + Text/Debug Console fallback

#include "uefi_abi.h"
#include "bls_parser.h"
#include "boot_ui.h"

/* Unity-include pure freestanding C modules for single-TU PE32+ linking */
#include "bls_parser.c"
#include "boot_ui.c"

/* Freestanding compiler intrinsics */
void *memset(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; ++i) p[i] = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

static EFI_SYSTEM_TABLE  *gST = NULL;
static EFI_BOOT_SERVICES *gBS = NULL;
static EFI_RUNTIME_SERVICES *gRT = NULL;

static size_t u16_len(const CHAR16 *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static void ascii_to_u16(const char *src, CHAR16 *dst, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = (CHAR16)(unsigned char)src[i];
        ++i;
    }
    dst[i] = 0;
}

static void u16_to_ascii(const CHAR16 *src, char *dst, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = (src[i] < 128) ? (char)src[i] : '?';
        ++i;
    }
    dst[i] = '\0';
}

static void normalize_efi_path(const char *in, CHAR16 *out, size_t cap) {
    if (!cap) return;
    size_t j = 0;
    if (in[0] != '\\' && in[0] != '/') {
        if (j + 1 < cap) out[j++] = (CHAR16)'\\';
    }
    for (size_t i = 0; in[i] && j + 1 < cap; ++i) {
        out[j++] = (in[i] == '/') ? (CHAR16)'\\' : (CHAR16)(unsigned char)in[i];
    }
    out[j] = 0;
}

static void con_puts(const char *s) {
    if (!gST || !gST->ConOut) return;
    CHAR16 buf[256];
    size_t i = 0;
    while (s && s[i]) {
        size_t j = 0;
        while (s[i] && j < 252) {
            if (s[i] == '\n') {
                buf[j++] = (CHAR16)'\r';
                buf[j++] = (CHAR16)'\n';
                ++i;
            } else {
                buf[j++] = (CHAR16)(unsigned char)s[i++];
            }
        }
        buf[j] = 0;
        gST->ConOut->OutputString(gST->ConOut, buf);
    }
}

static void set_loader_var_u16(const CHAR16 *name, const CHAR16 *val, UINT32 attrs) {
    if (!gRT || !gRT->SetVariable) return;
    UINTN sz = (u16_len(val) + 1) * sizeof(CHAR16);
    gRT->SetVariable(name, &gSystemdLoaderGuid, attrs, sz, val);
}

static void set_loader_var_ascii(const CHAR16 *name, const char *val) {
    CHAR16 buf[256];
    ascii_to_u16(val, buf, 256);
    set_loader_var_u16(name, buf, EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS);
}

static int get_loader_var_ascii(const CHAR16 *name, char *out, size_t cap) {
    if (!gRT || !gRT->GetVariable) return 0;
    CHAR16 buf[128];
    UINTN sz = sizeof(buf);
    UINT32 attrs = 0;
    EFI_STATUS st = gRT->GetVariable(name, &gSystemdLoaderGuid, &attrs, &sz, buf);
    if (EFI_ERROR(st) || sz < sizeof(CHAR16)) return 0;
    buf[127] = 0;
    u16_to_ascii(buf, out, cap);
    return out[0] != '\0';
}

static void delete_loader_var(const CHAR16 *name) {
    if (!gRT || !gRT->SetVariable) return;
    gRT->SetVariable(name, &gSystemdLoaderGuid,
                     EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                     0, NULL);
}

static void format_guid_lower(const uint8_t sig[16], char out[37]) {
    static const char hex[] = "0123456789abcdef";
    static const int order[16] = {3,2,1,0, 5,4, 7,6, 8,9, 10,11,12,13,14,15};
    int pos = 0;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[pos++] = '-';
        uint8_t b = sig[order[i]];
        out[pos++] = hex[(b >> 4) & 0xF];
        out[pos++] = hex[b & 0xF];
    }
    out[pos] = '\0';
}

static int extract_esp_partuuid(EFI_HANDLE dev_handle, char out[37]) {
    EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
    if (EFI_ERROR(gBS->HandleProtocol(dev_handle, &gEfiDevicePathProtocolGuid, (void **)&dp)) || !dp) {
        return 0;
    }
    while (dp->Type != 0x7F) {
        if (dp->Type == 0x04 && dp->SubType == 0x01 && dp->Length >= sizeof(HARDDRIVE_DEVICE_PATH)) {
            const HARDDRIVE_DEVICE_PATH *hd = (const HARDDRIVE_DEVICE_PATH *)dp;
            if (hd->SignatureType == 0x02) {
                format_guid_lower(hd->Signature, out);
                return 1;
            }
        }
        if (dp->Length < sizeof(EFI_DEVICE_PATH_PROTOCOL)) break;
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((uint8_t *)dp + dp->Length);
    }
    return 0;
}

static int read_entire_file(EFI_FILE_PROTOCOL *root, const CHAR16 *path,
                            uint8_t **out_buf, UINTN *out_size) {
    EFI_FILE_PROTOCOL *f = NULL;
    if (EFI_ERROR(root->Open(root, &f, path, EFI_FILE_MODE_READ, 0)) || !f) return 0;

    uint8_t info_buf[512];
    UINTN info_sz = sizeof(info_buf);
    if (EFI_ERROR(f->GetInfo(f, &gEfiFileInfoGuid, &info_sz, info_buf))) {
        f->Close(f);
        return 0;
    }
    UINTN fsz = (UINTN)((const EFI_FILE_INFO *)info_buf)->FileSize;
    if (fsz == 0 || fsz > 128U * 1024U * 1024U) {
        f->Close(f);
        return 0;
    }
    void *buf = NULL;
    if (EFI_ERROR(gBS->AllocatePool(EFI_LOADER_DATA, fsz + 1, &buf)) || !buf) {
        f->Close(f);
        return 0;
    }
    UINTN rd = fsz;
    EFI_STATUS st = f->Read(f, &rd, buf);
    f->Close(f);
    if (EFI_ERROR(st)) {
        gBS->FreePool(buf);
        return 0;
    }
    ((uint8_t *)buf)[rd] = 0;
    *out_buf = (uint8_t *)buf;
    *out_size = rd;
    return 1;
}

static int file_exists(EFI_FILE_PROTOCOL *root, const CHAR16 *path) {
    EFI_FILE_PROTOCOL *f = NULL;
    if (EFI_ERROR(root->Open(root, &f, path, EFI_FILE_MODE_READ, 0)) || !f) return 0;
    f->Close(f);
    return 1;
}

static void add_auto_efi_entry(EFI_FILE_PROTOCOL *root, BootEntry *entries, int *count,
                               const char *id, const char *title, const char *efi_path,
                               BootOsIcon icon) {
    if (*count >= FINN_BOOT_MAX_ENTRIES) return;
    CHAR16 wpath[FINN_BOOT_MAX_STR];
    normalize_efi_path(efi_path, wpath, FINN_BOOT_MAX_STR);
    if (!file_exists(root, wpath)) return;

    BootEntry *e = &entries[*count];
    memset(e, 0, sizeof(*e));
    str_copy(e->id, sizeof(e->id), id, str_len(id));
    str_copy(e->title, sizeof(e->title), title, str_len(title));
    str_copy(e->efi_path, sizeof(e->efi_path), efi_path, str_len(efi_path));
    e->kind = ENTRY_KIND_EFI_CHAINLOAD;
    e->icon = icon;
    (*count)++;
}

static void scan_bls_entries(EFI_FILE_PROTOCOL *root, BootEntry *entries, int *count) {
    CHAR16 dir_path[64];
    ascii_to_u16("\\loader\\entries", dir_path, 64);
    EFI_FILE_PROTOCOL *dir = NULL;
    if (EFI_ERROR(root->Open(root, &dir, dir_path, EFI_FILE_MODE_READ, 0)) || !dir) return;

    uint8_t info_buf[512];
    for (;;) {
        if (*count >= FINN_BOOT_MAX_ENTRIES) break;
        UINTN sz = sizeof(info_buf);
        if (EFI_ERROR(dir->Read(dir, &sz, info_buf)) || sz == 0) break;
        const EFI_FILE_INFO *fi = (const EFI_FILE_INFO *)info_buf;
        if (fi->Attribute & EFI_FILE_DIRECTORY) continue;

        char fname[FINN_BOOT_MAX_STR];
        u16_to_ascii(fi->FileName, fname, sizeof(fname));
        size_t flen = str_len(fname);
        if (flen < 6 || !str_eq_ci(fname + flen - 5, ".conf")) continue;

        uint8_t *raw = NULL;
        UINTN raw_sz = 0;
        if (read_entire_file(dir, fi->FileName, &raw, &raw_sz)) {
            BootEntry cand;
            if (bls_parse_entry_conf(fname, (const char *)raw, raw_sz, &cand)) {
                entries[*count] = cand;
                (*count)++;
            }
            gBS->FreePool(raw);
        }
    }
    dir->Close(dir);
}

static void scan_uki_entries(EFI_FILE_PROTOCOL *root, BootEntry *entries, int *count) {
    CHAR16 dir_path[64];
    ascii_to_u16("\\EFI\\Linux", dir_path, 64);
    EFI_FILE_PROTOCOL *dir = NULL;
    if (EFI_ERROR(root->Open(root, &dir, dir_path, EFI_FILE_MODE_READ, 0)) || !dir) return;

    uint8_t info_buf[512];
    for (;;) {
        if (*count >= FINN_BOOT_MAX_ENTRIES) break;
        UINTN sz = sizeof(info_buf);
        if (EFI_ERROR(dir->Read(dir, &sz, info_buf)) || sz == 0) break;
        const EFI_FILE_INFO *fi = (const EFI_FILE_INFO *)info_buf;
        if (fi->Attribute & EFI_FILE_DIRECTORY) continue;

        char fname[FINN_BOOT_MAX_STR];
        u16_to_ascii(fi->FileName, fname, sizeof(fname));
        size_t flen = str_len(fname);
        if (flen < 5 || !str_eq_ci(fname + flen - 4, ".efi")) continue;

        char full_path[FINN_BOOT_MAX_STR];
        str_copy(full_path, sizeof(full_path), "\\EFI\\Linux\\", 11);
        str_copy(full_path + 11, sizeof(full_path) - 11, fname, flen);

        /* Read first 256 KiB header/sections to inspect .osrel and .cmdline */
        EFI_FILE_PROTOCOL *f = NULL;
        if (!EFI_ERROR(dir->Open(dir, &f, fi->FileName, EFI_FILE_MODE_READ, 0)) && f) {
            UINTN cap = 256 * 1024;
            void *buf = NULL;
            if (!EFI_ERROR(gBS->AllocatePool(EFI_LOADER_DATA, cap, &buf)) && buf) {
                UINTN rd = cap;
                if (!EFI_ERROR(f->Read(f, &rd, buf)) && rd > 0x80) {
                    BootEntry cand;
                    if (bls_parse_uki_pe(fname, full_path, (const uint8_t *)buf, rd, &cand)) {
                        entries[*count] = cand;
                        (*count)++;
                    }
                }
                gBS->FreePool(buf);
            }
            f->Close(f);
        }
    }
    dir->Close(dir);
}

/* Linux EFI stub LoadFile2 initrd provider */
typedef struct {
    EFI_LOAD_FILE2_PROTOCOL proto;
    uint8_t *data;
    UINTN    size;
} LinuxInitrdProvider;

static EFI_STATUS EFIAPI initrd_load_file(EFI_LOAD_FILE2_PROTOCOL *This,
                                          EFI_DEVICE_PATH_PROTOCOL *FilePath,
                                          BOOLEAN BootPolicy,
                                          UINTN *BufferSize,
                                          void *Buffer) {
    (void)FilePath;
    if (BootPolicy) return EFI_UNSUPPORTED;
    LinuxInitrdProvider *prov = (LinuxInitrdProvider *)This;
    if (!BufferSize) return EFI_INVALID_PARAMETER;
    if (!Buffer || *BufferSize < prov->size) {
        *BufferSize = prov->size;
        return EFI_BUFFER_TOO_SMALL;
    }
    memcpy(Buffer, prov->data, prov->size);
    *BufferSize = prov->size;
    return EFI_SUCCESS;
}

#pragma pack(push, 1)
typedef struct {
    VENDOR_DEVICE_PATH       vendor;
    EFI_DEVICE_PATH_PROTOCOL end;
} InitrdDevicePath;
#pragma pack(pop)

static EFI_STATUS boot_selected_entry(EFI_HANDLE image_handle,
                                      EFI_FILE_PROTOCOL *esp_root,
                                      const BootEntry *entry) {
    /* Publish LoaderEntrySelected before handoff */
    set_loader_var_ascii(L"LoaderEntrySelected", entry->id);

    if (entry->kind == ENTRY_KIND_FIRMWARE_SETUP) {
        UINT64 ind = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
        gRT->SetVariable(L"OsIndications", &gEfiGlobalVariableGuid,
                         EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                         sizeof(ind), &ind);
        gRT->ResetSystem(EFI_RESET_WARM, EFI_SUCCESS, 0, NULL);
        return EFI_SUCCESS;
    }

    /* Decrement boot-counting filename (+<left>-<done>.conf) before StartImage */
    if (entry->has_boot_count && entry->tries_left > 0) {
        char next_name[FINN_BOOT_MAX_STR];
        if (bls_format_decremented_filename(entry->id, next_name, sizeof(next_name))) {
            CHAR16 old_path[FINN_BOOT_MAX_STR];
            char full_old[FINN_BOOT_MAX_STR];
            str_copy(full_old, sizeof(full_old), "\\loader\\entries\\", 16);
            str_copy(full_old + 16, sizeof(full_old) - 16, entry->id, str_len(entry->id));
            ascii_to_u16(full_old, old_path, FINN_BOOT_MAX_STR);
            EFI_FILE_PROTOCOL *ef = NULL;
            if (!EFI_ERROR(esp_root->Open(esp_root, &ef, old_path,
                                          EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0)) && ef) {
                uint8_t info_buf[512];
                UINTN info_sz = sizeof(info_buf);
                if (!EFI_ERROR(ef->GetInfo(ef, &gEfiFileInfoGuid, &info_sz, info_buf))) {
                    EFI_FILE_INFO *fi = (EFI_FILE_INFO *)info_buf;
                    ascii_to_u16(next_name, fi->FileName, 128);
                    fi->Size = sizeof(EFI_FILE_INFO) + (u16_len(fi->FileName) + 1) * sizeof(CHAR16);
                    ef->SetInfo(ef, &gEfiFileInfoGuid, (UINTN)fi->Size, fi);
                }
                ef->Close(ef);
            }
        }
    }

    const char *target_path = (entry->kind == ENTRY_KIND_BLS_LINUX) ? entry->linux_path : entry->efi_path;
    CHAR16 wtarget[FINN_BOOT_MAX_STR];
    normalize_efi_path(target_path, wtarget, FINN_BOOT_MAX_STR);

    uint8_t *img_buf = NULL;
    UINTN img_sz = 0;
    if (!read_entire_file(esp_root, wtarget, &img_buf, &img_sz)) {
        return EFI_NOT_FOUND;
    }

    /* Optional Linux EFI stub multi-initrd LoadFile2 registration */
    EFI_HANDLE initrd_handle = NULL;
    LinuxInitrdProvider initrd_prov;
    InitrdDevicePath initrd_dp;
    uint8_t *combined_initrd = NULL;
    if (entry->kind == ENTRY_KIND_BLS_LINUX && entry->initrd_count > 0) {
        UINTN total_rd = 0;
        uint8_t *parts[FINN_BOOT_MAX_INITRDS] = {0};
        UINTN part_sz[FINN_BOOT_MAX_INITRDS] = {0};
        for (int i = 0; i < entry->initrd_count; ++i) {
            CHAR16 wrd[FINN_BOOT_MAX_STR];
            normalize_efi_path(entry->initrd_paths[i], wrd, FINN_BOOT_MAX_STR);
            if (read_entire_file(esp_root, wrd, &parts[i], &part_sz[i])) {
                total_rd += part_sz[i];
            }
        }
        if (total_rd > 0 && !EFI_ERROR(gBS->AllocatePool(EFI_LOADER_DATA, total_rd, (void **)&combined_initrd))) {
            UINTN off = 0;
            for (int i = 0; i < entry->initrd_count; ++i) {
                if (parts[i]) {
                    memcpy(combined_initrd + off, parts[i], part_sz[i]);
                    off += part_sz[i];
                    gBS->FreePool(parts[i]);
                }
            }
            initrd_prov.proto.LoadFile = initrd_load_file;
            initrd_prov.data = combined_initrd;
            initrd_prov.size = total_rd;

            initrd_dp.vendor.Header.Type = 0x04;     /* MEDIA_DEVICE_PATH */
            initrd_dp.vendor.Header.SubType = 0x03;  /* MEDIA_VENDOR_DP */
            initrd_dp.vendor.Header.Length = (UINT16)sizeof(VENDOR_DEVICE_PATH);
            initrd_dp.vendor.VendorGuid = gLinuxInitrdMediaGuid;
            initrd_dp.end.Type = 0x7F;
            initrd_dp.end.SubType = 0xFF;
            initrd_dp.end.Length = (UINT16)sizeof(EFI_DEVICE_PATH_PROTOCOL);

            gBS->InstallMultipleProtocolInterfaces(
                &initrd_handle,
                &gEfiDevicePathProtocolGuid, &initrd_dp,
                &gEfiLoadFile2ProtocolGuid, &initrd_prov,
                NULL);
        }
    }

    EFI_HANDLE child_handle = NULL;
    EFI_STATUS st = gBS->LoadImage(0, image_handle, NULL, img_buf, img_sz, &child_handle);
    if (!EFI_ERROR(st) && child_handle) {
        if (entry->options[0] != '\0') {
            EFI_LOADED_IMAGE_PROTOCOL *child_li = NULL;
            if (!EFI_ERROR(gBS->HandleProtocol(child_handle, &gEfiLoadedImageProtocolGuid, (void **)&child_li)) && child_li) {
                CHAR16 *wopts = NULL;
                size_t olen = str_len(entry->options);
                if (!EFI_ERROR(gBS->AllocatePool(EFI_LOADER_DATA, (olen + 1) * sizeof(CHAR16), (void **)&wopts)) && wopts) {
                    ascii_to_u16(entry->options, wopts, olen + 1);
                    child_li->LoadOptions = wopts;
                    child_li->LoadOptionsSize = (UINT32)((olen + 1) * sizeof(CHAR16));
                }
            }
        }
        st = gBS->StartImage(child_handle, NULL, NULL);
    }

    if (initrd_handle) {
        gBS->UninstallMultipleProtocolInterfaces(
            initrd_handle,
            &gEfiDevicePathProtocolGuid, &initrd_dp,
            &gEfiLoadFile2ProtocolGuid, &initrd_prov,
            NULL);
    }
    if (combined_initrd) gBS->FreePool(combined_initrd);
    gBS->FreePool(img_buf);
    return st;
}

static void render_text_fallback(const BootEntry *entries, int count, int selected, int countdown_sec) {
    if (!gST || !gST->ConOut) return;
    gST->ConOut->ClearScreen(gST->ConOut);
    con_puts("FinnOS Bootloader (systemd-boot compatible - Text/Debug Mode)\n");
    con_puts("-------------------------------------------------------------\n\n");
    for (int i = 0; i < count; ++i) {
        con_puts(i == selected ? "  > " : "    ");
        con_puts(entries[i].title);
        if (entries[i].version[0]) {
            con_puts("  (");
            con_puts(entries[i].version);
            con_puts(")");
        }
        con_puts("  [");
        con_puts(entries[i].id);
        con_puts("]\n");
    }
    con_puts("\nUp/Down or Left/Right: Select | Enter: Boot | T: Graphical Mode\n");
    if (countdown_sec > 0) {
        char num[4];
        num[0] = (char)('0' + (countdown_sec % 10));
        num[1] = '\0';
        con_puts("Auto-boot in ");
        con_puts(num);
        con_puts("s...\n");
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    gRT = SystemTable->RuntimeServices;

    if (gBS && gBS->SetWatchdogTimer) {
        gBS->SetWatchdogTimer(0, 0, 0, NULL);
    }

    /* 1. Locate our own LoadedImage & ESP filesystem */
    EFI_LOADED_IMAGE_PROTOCOL *loaded_img = NULL;
    if (EFI_ERROR(gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void **)&loaded_img)) || !loaded_img) {
        con_puts("finn-boot: LoadedImage protocol unavailable\n");
        return EFI_LOAD_ERROR;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    EFI_FILE_PROTOCOL *esp_root = NULL;
    if (EFI_ERROR(gBS->HandleProtocol(loaded_img->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **)&sfs)) ||
        !sfs || EFI_ERROR(sfs->OpenVolume(sfs, &esp_root)) || !esp_root) {
        con_puts("finn-boot: ESP SimpleFileSystem unavailable\n");
        return EFI_LOAD_ERROR;
    }

    /* 2. Publish systemd Boot Loader Interface metadata EFI variables */
    set_loader_var_ascii(L"LoaderInfo", "systemd-boot 257 (finn-boot 1.0)");
    set_loader_var_ascii(L"LoaderFirmwareType", "UEFI 2.10");
    if (gST->FirmwareVendor) {
        set_loader_var_u16(L"LoaderFirmwareInfo", gST->FirmwareVendor,
                           EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS);
    }
    set_loader_var_ascii(L"LoaderImageIdentifier", "\\EFI\\BOOT\\BOOTX64.EFI");
    {
        UINT64 features = EFI_LOADER_FEATURE_CONFIG_TIMEOUT |
                          EFI_LOADER_FEATURE_CONFIG_TIMEOUT_ONE_SHOT |
                          EFI_LOADER_FEATURE_ENTRY_DEFAULT |
                          EFI_LOADER_FEATURE_ENTRY_ONESHOT |
                          EFI_LOADER_FEATURE_BOOT_COUNTING |
                          EFI_LOADER_FEATURE_SORT_KEY |
                          EFI_LOADER_FEATURE_SAVED_ENTRY;
        gRT->SetVariable(L"LoaderFeatures", &gSystemdLoaderGuid,
                         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                         sizeof(features), &features);
    }
    {
        char partuuid[37];
        if (extract_esp_partuuid(loaded_img->DeviceHandle, partuuid)) {
            set_loader_var_ascii(L"LoaderDevicePartUUID", partuuid);
        }
    }

    /* 3. Load /loader/loader.conf */
    BootLoaderConfig cfg;
    bls_config_init(&cfg);
    {
        CHAR16 lpath[64];
        ascii_to_u16("\\loader\\loader.conf", lpath, 64);
        uint8_t *raw = NULL;
        UINTN raw_sz = 0;
        if (read_entire_file(esp_root, lpath, &raw, &raw_sz)) {
            bls_parse_loader_conf((const char *)raw, raw_sz, &cfg);
            gBS->FreePool(raw);
        }
    }

    /* 4. Discover BLS Type #1 entries, UKIs, Windows, macOS, and UEFI Firmware Setup */
    static BootEntry entries[FINN_BOOT_MAX_ENTRIES];
    int entry_count = 0;
    scan_bls_entries(esp_root, entries, &entry_count);
    scan_uki_entries(esp_root, entries, &entry_count);
    if (cfg.auto_entries) {
        add_auto_efi_entry(esp_root, entries, &entry_count,
                           "auto-windows", "Windows", "\\EFI\\Microsoft\\Boot\\bootmgfw.efi", OS_ICON_WINDOWS);
        add_auto_efi_entry(esp_root, entries, &entry_count,
                           "auto-macos", "macOS", "\\System\\Library\\CoreServices\\boot.efi", OS_ICON_MACOS);
        add_auto_efi_entry(esp_root, entries, &entry_count,
                           "auto-macos-efi", "macOS", "\\EFI\\Apple\\Boot\\boot.efi", OS_ICON_MACOS);
    }
    if (cfg.auto_firmware && entry_count < FINN_BOOT_MAX_ENTRIES) {
        UINT64 os_ind_sup = 0;
        UINTN ind_sz = sizeof(os_ind_sup);
        if (!EFI_ERROR(gRT->GetVariable(L"OsIndicationsSupported", &gEfiGlobalVariableGuid, NULL, &ind_sz, &os_ind_sup)) &&
            (os_ind_sup & EFI_OS_INDICATIONS_BOOT_TO_FW_UI)) {
            BootEntry *fe = &entries[entry_count++];
            memset(fe, 0, sizeof(*fe));
            str_copy(fe->id, sizeof(fe->id), "auto-reboot-to-firmware-setup", 29);
            str_copy(fe->title, sizeof(fe->title), "Firmware", 8);
            fe->kind = ENTRY_KIND_FIRMWARE_SETUP;
            fe->icon = OS_ICON_FIRMWARE;
        }
    }

    if (entry_count == 0) {
        con_puts("finn-boot: no boot entries found on ESP\n");
        gBS->Stall(3000000);
        return EFI_NOT_FOUND;
    }
    bls_sort_entries(entries, entry_count);

    /* 5. Publish LoaderEntries (packed NUL-separated UTF-16LE list) */
    {
        CHAR16 packed[1024];
        size_t pos = 0;
        for (int i = 0; i < entry_count && pos + 64 < 1024; ++i) {
            for (size_t k = 0; entries[i].id[k] && pos + 2 < 1024; ++k) {
                packed[pos++] = (CHAR16)(unsigned char)entries[i].id[k];
            }
            packed[pos++] = 0;
        }
        gRT->SetVariable(L"LoaderEntries", &gSystemdLoaderGuid,
                         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                         pos * sizeof(CHAR16), packed);
    }

    /* 6. Honor LoaderEntryOneShot (consumed immediately!) and LoaderEntryDefault */
    int selected = 0;
    char oneshot[FINN_BOOT_MAX_STR];
    if (get_loader_var_ascii(L"LoaderEntryOneShot", oneshot, sizeof(oneshot))) {
        delete_loader_var(L"LoaderEntryOneShot");
        for (int i = 0; i < entry_count; ++i) {
            if (bls_glob_match(oneshot, entries[i].id)) {
                return boot_selected_entry(ImageHandle, esp_root, &entries[i]);
            }
        }
    }

    char def_var[FINN_BOOT_MAX_STR];
    const char *def_pat = cfg.default_pattern;
    if (get_loader_var_ascii(L"LoaderEntryDefault", def_var, sizeof(def_var))) {
        def_pat = def_var;
    }
    if (def_pat && def_pat[0]) {
        for (int i = 0; i < entry_count; ++i) {
            if (bls_glob_match(def_pat, entries[i].id)) {
                selected = i;
                break;
            }
        }
    }

    if (cfg.timeout_sec == 0) {
        return boot_selected_entry(ImageHandle, esp_root, &entries[selected]);
    }

    /* 7. Locate GOP and optional Simple Pointer Protocol */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    if (!cfg.console_text_only) {
        gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void **)&gop);
    }
    EFI_SIMPLE_POINTER_PROTOCOL *pointer = NULL;
    gBS->LocateProtocol(&gEfiSimplePointerProtocolGuid, NULL, (void **)&pointer);

    int text_mode = (gop == NULL || gop->Mode == NULL || gop->Mode->Info == NULL);
    int fb_w = text_mode ? 0 : (int)gop->Mode->Info->HorizontalResolution;
    int fb_h = text_mode ? 0 : (int)gop->Mode->Info->VerticalResolution;
    uint32_t *backbuf = NULL;
    if (!text_mode && fb_w > 0 && fb_h > 0) {
        if (EFI_ERROR(gBS->AllocatePool(EFI_LOADER_DATA, (UINTN)fb_w * (UINTN)fb_h * 4U, (void **)&backbuf)) || !backbuf) {
            text_mode = 1;
        }
    }

    BootUiState ui;
    memset(&ui, 0, sizeof(ui));
    ui.selected_index = selected;
    ui.hover_index = -1;
    ui.countdown_total_ms = (cfg.timeout_sec > 0) ? (cfg.timeout_sec * 1000) : 0;
    ui.countdown_ms = ui.countdown_total_ms;
    ui.pointer_x = fb_w / 2;
    ui.pointer_y = fb_h / 2;

    int prev_left_btn = 0;
    int dirty = 1;
    char status_buf[FINN_BOOT_MAX_STR];
    status_buf[0] = '\0';

    /* 8. Interactive 60Hz Startup Manager Loop */
    for (;;) {
        if (dirty) {
            if (text_mode) {
                render_text_fallback(entries, entry_count, ui.selected_index,
                                     (ui.countdown_ms + 999) / 1000);
            } else {
                ui.status_banner = status_buf[0] ? status_buf : NULL;
                boot_ui_render(backbuf, fb_w, fb_h, entries, entry_count, &ui);
                gop->Blt(gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)backbuf,
                         EfiBltBufferToVideo, 0, 0, 0, 0, (UINTN)fb_w, (UINTN)fb_h, 0);
            }
            dirty = 0;
        }

        /* Poll keyboard */
        EFI_INPUT_KEY key;
        if (gST->ConIn && !EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &key))) {
            ui.countdown_ms = 0;
            dirty = 1;
            if (key.ScanCode == 0x03 || key.ScanCode == 0x01 || key.UnicodeChar == '\t') {
                /* Right / Up / Tab */
                ui.selected_index = (ui.selected_index + 1) % entry_count;
            } else if (key.ScanCode == 0x04 || key.ScanCode == 0x02) {
                /* Left / Down */
                ui.selected_index = (ui.selected_index + entry_count - 1) % entry_count;
            } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n' || key.UnicodeChar == ' ') {
                if (backbuf) gBS->FreePool(backbuf);
                return boot_selected_entry(ImageHandle, esp_root, &entries[ui.selected_index]);
            } else if (key.UnicodeChar == 't' || key.UnicodeChar == 'T' ||
                       key.UnicodeChar == 'd' || key.UnicodeChar == 'D') {
                if (backbuf) text_mode = !text_mode;
            } else if (key.UnicodeChar == 'r' || key.UnicodeChar == 'R') {
                for (int i = 0; i < entry_count; ++i) {
                    if (entries[i].icon == OS_ICON_RECOVERY) { ui.selected_index = i; break; }
                }
            } else if ((key.UnicodeChar == 'e' || key.UnicodeChar == 'E') && cfg.editor_allowed) {
                str_copy(status_buf, sizeof(status_buf),
                         entries[ui.selected_index].options[0] ? entries[ui.selected_index].options : "(no kernel options)",
                         FINN_BOOT_MAX_STR - 1);
            } else if (key.UnicodeChar >= '1' && key.UnicodeChar <= '9') {
                int idx = (int)(key.UnicodeChar - '1');
                if (idx < entry_count) ui.selected_index = idx;
            }
        }

        /* Poll UEFI Simple Pointer (mouse/trackpad) */
        if (!text_mode && pointer) {
            EFI_SIMPLE_POINTER_STATE ps;
            if (!EFI_ERROR(pointer->GetState(pointer, &ps))) {
                if (ps.RelativeMovementX != 0 || ps.RelativeMovementY != 0) {
                    ui.pointer_active = 1;
                    ui.countdown_ms = 0;
                    ui.pointer_x += (int)(ps.RelativeMovementX / 16);
                    ui.pointer_y += (int)(ps.RelativeMovementY / 16);
                    if (ui.pointer_x < 0) ui.pointer_x = 0;
                    if (ui.pointer_y < 0) ui.pointer_y = 0;
                    if (ui.pointer_x >= fb_w) ui.pointer_x = fb_w - 1;
                    if (ui.pointer_y >= fb_h) ui.pointer_y = fb_h - 1;
                    int hit_arrow = 0;
                    ui.hover_index = boot_ui_hit_test(fb_w, fb_h, entry_count, ui.selected_index,
                                                      ui.pointer_x, ui.pointer_y, &hit_arrow);
                    dirty = 1;
                }
                if (ps.LeftButton && !prev_left_btn && ui.pointer_active) {
                    int hit_arrow = 0;
                    int hit = boot_ui_hit_test(fb_w, fb_h, entry_count, ui.selected_index,
                                               ui.pointer_x, ui.pointer_y, &hit_arrow);
                    if (hit >= 0) {
                        if (hit_arrow || hit == ui.selected_index) {
                            if (backbuf) gBS->FreePool(backbuf);
                            return boot_selected_entry(ImageHandle, esp_root, &entries[hit]);
                        }
                        ui.selected_index = hit;
                        dirty = 1;
                    }
                }
                prev_left_btn = ps.LeftButton ? 1 : 0;
            }
        }

        /* Step countdown timer */
        gBS->Stall(16000);
        if (ui.countdown_ms > 0) {
            ui.countdown_ms -= 16;
            if (ui.countdown_ms <= 0) {
                if (backbuf) gBS->FreePool(backbuf);
                return boot_selected_entry(ImageHandle, esp_root, &entries[ui.selected_index]);
            }
            if ((ui.countdown_ms % 80) < 16) dirty = 1;
        }
    }
}
