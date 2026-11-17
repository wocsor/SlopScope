# Hardware and boot

The application targets the ARM926EJ-S CPU in the Allwinner F1C200s. The connected
unit has 64 MiB DRAM. The FPGA and analog frontend are initialized using recovered
stock sequences. `board.h` holds the board addresses and display geometry.

| Region | Purpose |
| --- | --- |
| `0x80080000` | Application entry |
| `0x800c0000` onward | Application BSS, bounded by the linker script |
| `0x81000000` | CSI sample buffer |
| `0x81010000` | Record workspace |
| `0x817230c0` | FPGA configuration image loaded from the unit |
| `0x81c00000`, `0x81cc0000` | LCD framebuffers |

The FEL helper runs in lower SRAM and restores the occupied range after return.
Its optional memory diagnostic covers 32 MiB; that diagnostic size is not a
claim that the board has only 32 MiB. Normal installation retains stock boot
loaders and the unit's FPGA payload.

The maintenance tools permit updates only to the logo (`0x00c000..0x0c8000`)
and application (`0x349000..0x519000`) areas. Settings use the two sectors at
`0xffe000` and `0xfff000`. Factory and identity regions stay in place.

The optional development pulse source uses ESP32 GPIO16 for CH1 and GPIO17 for
CH2, with a shared ground. Its firmware and serial controller are maintained
outside this repository. The deployed unit uses PC controls; physical dials
and buttons are not installed.
