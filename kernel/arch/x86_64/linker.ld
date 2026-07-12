ENTRY(_start)

SECTIONS
{
  /* OVMF reserves the 16 MiB range in this QEMU configuration. */
  . = 0x02000000;
  __kernel_start = .;
  .text ALIGN(4K) : { __text_start = .; *(.text .text.*) . = ALIGN(4K); __text_end = .; }
  .rodata ALIGN(4K) : { __rodata_start = .; *(.rodata .rodata.*) *(.eh_frame_hdr) *(.got .got.*) . = ALIGN(4K); __rodata_end = .; }
  .data ALIGN(4K) : { __data_start = .; *(.data .data.*) . = ALIGN(4K); __data_end = .; }
  .bss ALIGN(4K) : { __bss_start = .; *(.bss .bss.*) . = ALIGN(4K); __bss_end = .; }
  .kernel_reserved ALIGN(4K) (NOLOAD) : {
    /* Keep the established early-stack address stable after moving descriptor storage above it.
       This reserved gap is not mapped and is protected by the kernel image reservation. */
    . += 0x13000;
    __stack_guard_low_start = .;
    . += 4K;
    __stack_guard_low_end = .;
    __stack_bottom = .;
    . += 64K;
    __stack_top = .;
    __stack_guard_high_start = .;
    . += 4K;
    __stack_guard_high_end = .;
  }
  .kernel_after_stack ALIGN(4K) (NOLOAD) : {
    __kernel_after_stack_start = .;
    *(.kernel_after_stack .kernel_after_stack.*)
    . = ALIGN(4K);
    __kernel_after_stack_end = .;
    __kernel_end = .;
  }
  /DISCARD/ : { *(.eh_frame) *(.comment) }
}
