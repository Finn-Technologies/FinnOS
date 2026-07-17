ENTRY(_start)

SECTIONS
{
  /* QEMU virt DRAM begins at 0x40000000; leave 2 MiB for firmware data. */
  . = 0x40200000;
  __kernel_start = .;
  .text ALIGN(4K) : { *(.text._start) *(.text .text.*) . = ALIGN(4K); }
  .rodata ALIGN(4K) : { *(.rodata .rodata.*) *(.eh_frame_hdr) *(.got .got.*) . = ALIGN(4K); }
  .data ALIGN(4K) : { *(.data .data.*) . = ALIGN(4K); }
  .bss ALIGN(4K) (NOLOAD) : { *(.bss .bss.*) . = ALIGN(4K); }
  .kernel_stack ALIGN(16) (NOLOAD) : {
    __stack_bottom = .;
    . += 64K;
    __stack_top = .;
  }
  __kernel_end = .;
  /DISCARD/ : { *(.eh_frame) *(.comment) }
}
