ENTRY(_start)

SECTIONS
{
  /* OVMF reserves the 16 MiB range in this QEMU configuration. */
  . = 0x02000000;
  __kernel_start = .;
  .text ALIGN(4K) : { *(.text .text.*) }
  .rodata ALIGN(4K) : { *(.rodata .rodata.*) }
  .data ALIGN(4K) : { *(.data .data.*) }
  .bss ALIGN(4K) : { *(.bss .bss.*) }
  . = ALIGN(4K);
  __stack_bottom = .;
  . += 64K;
  __stack_top = .;
  __kernel_end = .;
  /DISCARD/ : { *(.eh_frame) *(.comment) }
}
