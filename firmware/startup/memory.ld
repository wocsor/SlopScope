ENTRY(_start)
MEMORY {
 RAM (rwx) : ORIGIN = 0x80080000, LENGTH = 0x20000
 CONST (r) : ORIGIN = 0x800b0000, LENGTH = 0x10000
 WORK (rw) : ORIGIN = 0x800c0000, LENGTH = 0x20000
}
SECTIONS {
 .text : { KEEP(*(.text.entry)) *(.text .text.*) } > RAM
 .rodata : { *(.rodata .rodata.*) } > CONST
 .data : { *(.data .data.*) } > RAM
 .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; } > WORK
 /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
 ASSERT(__bss_end <= 0x800e0000, "USB work area overflow")
}
