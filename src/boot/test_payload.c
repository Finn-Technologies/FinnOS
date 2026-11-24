/* Minimal UEFI verification payload for QEMU + OVMF test harness.
 * Reads systemd Boot Loader Interface EFI variables (4a67b082-0a4c-41cf-b6c7-440b29bb8c4f)
 * and LoadOptions passed by finn-boot, prints them over UEFI ConOut (serial),
 * and cleanly shuts down the VM so automated verification finishes promptly.
 */

#include "uefi_abi.h"

static void put_ascii(EFI_SYSTEM_TABLE *st, const char *s) {
    CHAR16 buf[256];
    size_t i = 0;
    while (s && s[i]) {
        size_t j = 0;
        while (s[i] && j < 252) {
            if (s[i] == '\n') { buf[j++] = '\r'; buf[j++] = '\n'; ++i; }
            else buf[j++] = (CHAR16)(unsigned char)s[i++];
        }
        buf[j] = 0;
        st->ConOut->OutputString(st->ConOut, buf);
    }
}

static void dump_var(EFI_SYSTEM_TABLE *st, const char *label, const CHAR16 *name) {
    CHAR16 buf[256];
    UINTN sz = sizeof(buf) - sizeof(CHAR16);
    UINT32 attrs = 0;
    put_ascii(st, label);
    put_ascii(st, "=");
    if (!EFI_ERROR(st->RuntimeServices->GetVariable(name, &gSystemdLoaderGuid, &attrs, &sz, buf))) {
        UINTN chars = sz / sizeof(CHAR16);
        for (UINTN i = 0; i < chars; ++i) {
            if (buf[i] == 0) buf[i] = (i + 1 < chars) ? (CHAR16)',' : 0;
        }
        buf[chars] = 0;
        st->ConOut->OutputString(st->ConOut, buf);
    } else {
        put_ascii(st, "<absent>");
    }
    put_ascii(st, "\n");
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    put_ascii(SystemTable, "\nFINN_UEFI_HANDOFF_OK\n");
    dump_var(SystemTable, "LoaderInfo", L"LoaderInfo");
    dump_var(SystemTable, "LoaderEntrySelected", L"LoaderEntrySelected");
    dump_var(SystemTable, "LoaderEntries", L"LoaderEntries");
    dump_var(SystemTable, "LoaderDevicePartUUID", L"LoaderDevicePartUUID");
    dump_var(SystemTable, "LoaderEntryOneShot", L"LoaderEntryOneShot");

    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    if (!EFI_ERROR(SystemTable->BootServices->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void **)&li)) &&
        li && li->LoadOptions && li->LoadOptionsSize >= sizeof(CHAR16)) {
        put_ascii(SystemTable, "LoadOptions=");
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (const CHAR16 *)li->LoadOptions);
        put_ascii(SystemTable, "\n");
    }

    SystemTable->BootServices->Stall(200000);
    SystemTable->RuntimeServices->ResetSystem(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL);
    return EFI_SUCCESS;
}
