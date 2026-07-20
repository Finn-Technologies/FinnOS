ENTRY(_start)

SECTIONS
{
  /* QEMU virt DRAM begins at 0x40000000; leave 2 MiB for firmware data. */
  . = 0x40200000;
  __kernel_start = .;
  .text ALIGN(4K) : {
    __text_start = .;
    *(.text._start) *(.text .text.*)
    . = ALIGN(4K);
    __text_end = .;
  }
  .rodata ALIGN(4K) : {
    __rodata_start = .;
    *(.rodata .rodata.*) *(.eh_frame_hdr) *(.got .got.*)
    . = ALIGN(4K);
    __rodata_end = .;
  }
  .data ALIGN(4K) : {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(4K);
    __data_end = .;
  }
  .bss ALIGN(4K) (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.*)
    . = ALIGN(4K);
    __bss_end = .;
  }
  .kernel_stack_guard_low ALIGN(4K) (NOLOAD) : {
    __stack_guard_low_start = .;
    . += 4K;
    __stack_guard_low_end = .;
  }
  .kernel_stack ALIGN(4K) (NOLOAD) : {
    __stack_bottom = .;
    /* Fixed-capacity early memory tables are stack-resident until ARM64 owns
       its page tables and heap. Keep enough room for debug-build return
       temporaries without crossing into adjacent kernel storage. */
    . += 256K;
    __stack_top = .;
  }
  .kernel_stack_guard_high ALIGN(4K) (NOLOAD) : {
    __stack_guard_high_start = .;
    . += 4K;
    __stack_guard_high_end = .;
  }
  __kernel_end = .;
  /DISCARD/ : { *(.eh_frame) *(.comment) }
}
