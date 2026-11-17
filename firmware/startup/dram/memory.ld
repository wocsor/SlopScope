ENTRY(_start)
MEMORY {
 CODE (rx) : ORIGIN = 0x2000, LENGTH = 0x0800
 DRAMCODE (rx) : ORIGIN = 0x2800, LENGTH = 0x2000
}
SECTIONS {
 .text : { KEEP(*(.text.entry)) *(.text .text.*) *(.rodata .rodata.*) } > CODE
 .stock_dram : { *(.stock_dram) } > DRAMCODE
 .data : { *(.data .data.*) } > CODE
 .bss (NOLOAD) : { *(.bss .bss.* COMMON) } > CODE
 /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
 ASSERT(SIZEOF(.data)==0 && SIZEOF(.bss)==0, "Unexpected globals")
 ASSERT(ADDR(.text)+SIZEOF(.text)<=0x2800, "C code overlaps DDR helper")
 ASSERT(ADDR(.stock_dram)+SIZEOF(.stock_dram)<=0x4800, "DDR code overlaps mailbox")
}
