ENTRY(_start)
MEMORY {
 RAM (rwx) : ORIGIN = 0x80080000, LENGTH = 0x20000
 CONST (rx) : ORIGIN = 0x800b0000, LENGTH = 0x20000
 WORK (rw) : ORIGIN = 0x800d0000, LENGTH = 0x30000
}
SECTIONS {
 /* Keep the panel UI outside the text window ending at the USB mailbox. */
 .panel_text : { *(.panel_text .panel_text.*) } > CONST
 .storage_text : { *(.storage_text .storage_text.*) } > CONST
 .text : { KEEP(*(.text.entry)) *(.text .text.*) } > RAM
 .rodata : { *(.rodata .rodata.*) } > CONST
 .data : { *(.data .data.*) } > RAM
 .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; } > WORK
 /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
 ASSERT(__bss_end <= 0x80100000, "Application work area overflow")
}
