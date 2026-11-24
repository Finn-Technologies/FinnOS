# Build macOS-style Graphical UEFI Bootloader (finn-bootx64.efi & finn-bootaa64.efi)
# plus UEFI QEMU/OVMF verification stub (test-stubx64.efi).

CLANG     ?= clang
PYTHON    ?= python3
COFF2EFI  ?= ../../tools/coff2efi.py
LLD_LINK  := $(shell command -v lld-link 2>/dev/null)

EFI_COMMON_CFLAGS = -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fshort-wchar -fno-jump-tables \
                    -fno-asynchronous-unwind-tables -fno-unwind-tables -O2 -Wall -Wextra -Werror
EFI_X64_CFLAGS    = --target=x86_64-unknown-windows -mno-red-zone $(EFI_COMMON_CFLAGS)
EFI_AA64_CFLAGS   = --target=aarch64-unknown-windows $(EFI_COMMON_CFLAGS)

DEPS = uefi_abi.h bls_parser.h bls_parser.c boot_ui.h boot_ui.c efi_main.c

.PHONY: all clean

all: finn-bootx64.efi finn-bootaa64.efi test-stubx64.efi

finn-bootx64.obj: $(DEPS)
	$(CLANG) $(EFI_X64_CFLAGS) -c efi_main.c -o $@

finn-bootx64.efi: finn-bootx64.obj
ifneq ($(LLD_LINK),)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /out:$@ $<
else
	$(PYTHON) $(COFF2EFI) $< $@ efi_main
endif

finn-bootaa64.obj: $(DEPS)
	$(CLANG) $(EFI_AA64_CFLAGS) -c efi_main.c -o $@

finn-bootaa64.efi: finn-bootaa64.obj
ifneq ($(LLD_LINK),)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /out:$@ $<
else
	$(PYTHON) $(COFF2EFI) $< $@ efi_main
endif

test-stubx64.obj: uefi_abi.h test_payload.c
	$(CLANG) $(EFI_X64_CFLAGS) -c test_payload.c -o $@

test-stubx64.efi: test-stubx64.obj
ifneq ($(LLD_LINK),)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /out:$@ $<
else
	$(PYTHON) $(COFF2EFI) $< $@ efi_main
endif

clean:
	rm -f *.obj *.efi
