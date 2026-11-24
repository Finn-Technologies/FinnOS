/* Pure freestanding C UAPI Boot Loader Spec (BLS Type #1 & UKI Type #2),
 * loader.conf parser, boot-counting parser, and OS icon classifier.
 * Shared between finn-bootx64.efi/finn-bootaa64.efi and host unit tests.
 */

#ifndef FINN_BOOT_BLS_PARSER_H
#define FINN_BOOT_BLS_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FINN_BOOT_MAX_ENTRIES  16
#define FINN_BOOT_MAX_INITRDS  4
#define FINN_BOOT_MAX_STR      128
#define FINN_BOOT_MAX_OPTS     384

typedef enum {
    OS_ICON_FINNOS = 0,
    OS_ICON_WINDOWS,
    OS_ICON_MACOS,
    OS_ICON_LINUX,
    OS_ICON_RECOVERY,
    OS_ICON_FIRMWARE,
} BootOsIcon;

typedef enum {
    ENTRY_KIND_BLS_LINUX = 0,
    ENTRY_KIND_EFI_CHAINLOAD,
    ENTRY_KIND_UKI,
    ENTRY_KIND_FIRMWARE_SETUP,
} BootEntryKind;

typedef struct {
    char default_pattern[FINN_BOOT_MAX_STR];
    int  timeout_sec;       /* -1 = menu-force, 0 = instant, >0 = countdown */
    int  editor_allowed;    /* 1 = allow 'e' cmdline editor, 0 = disabled */
    int  auto_entries;      /* 1 = auto-detect Windows/macOS/UKIs */
    int  auto_firmware;     /* 1 = add UEFI Firmware Setup if supported */
    int  console_text_only; /* 1 = force text fallback */
} BootLoaderConfig;

typedef struct {
    char id[FINN_BOOT_MAX_STR];         /* e.g. 01-finnos.conf or auto-windows */
    char title[FINN_BOOT_MAX_STR];
    char version[FINN_BOOT_MAX_STR];
    char sort_key[FINN_BOOT_MAX_STR];
    char linux_path[FINN_BOOT_MAX_STR];
    char initrd_paths[FINN_BOOT_MAX_INITRDS][FINN_BOOT_MAX_STR];
    int  initrd_count;
    char efi_path[FINN_BOOT_MAX_STR];
    char options[FINN_BOOT_MAX_OPTS];
    BootOsIcon    icon;
    BootEntryKind kind;
    int  has_boot_count;
    int  tries_left;
    int  tries_done;
} BootEntry;

void bls_config_init(BootLoaderConfig *cfg);
void bls_parse_loader_conf(const char *text, size_t len, BootLoaderConfig *cfg);
int  bls_parse_entry_conf(const char *id, const char *text, size_t len, BootEntry *out);

/* Parse systemd-boot boot-counting filename (<stem>+<left>[-<done>].conf). */
int  bls_parse_boot_count(const char *filename, int *tries_left, int *tries_done);
int  bls_format_decremented_filename(const char *filename, char *out, size_t out_cap);

/* Parse PE32+ UKI sections (.osrel and .cmdline) from an in-memory buffer. */
int  bls_parse_uki_pe(const char *id, const char *efi_path,
                      const uint8_t *pe_buf, size_t pe_len, BootEntry *out);

/* Classify OS icon from entry id, title, efi path, and kernel options. */
BootOsIcon bls_classify_icon(const BootEntry *entry);

/* Simple glob match (* wildcard) used for loader.conf `default` patterns. */
int  bls_glob_match(const char *pattern, const char *text);

/* Sort entries (FinnOS primary first, then sort-key / id, Recovery & Firmware last). */
void bls_sort_entries(BootEntry *entries, int count);

#ifdef __cplusplus
}
#endif

#endif /* FINN_BOOT_BLS_PARSER_H */
