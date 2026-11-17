# Code provenance and redistribution inventory

Updated 2026-11-17 for SlopScope only. The owner selected MIT for the project's
implementation code; the root `LICENSE` contains the terms. Vendored material
retains its own licenses. The separate Python library and web UI need their own
inventories. A private source repository is being prepared for CI builds; the physical
stock-updater installation remains under qualification.

## Included third-party source and data

| Component | Source and local use | License and verification |
| --- | --- | --- |
| LittleFS v2.9.3 | Four files in `vendor/littlefs/`; read-only factory filesystem adapter in `firmware/storage/factory.c` | BSD-3-Clause; all four source/header files and the license match upstream byte-for-byte; hashes and URLs in `UPSTREAM.json` |
| Adafruit classic font | Pinned `glcdfont.c` in `vendor/adafruit-gfx-font/`; generated ASCII row-mask subset in `firmware/tables/hud_font.h` | BSD-2-Clause; original source/license retained unchanged; commit and hashes in `UPSTREAM.json`; conversion documented in `README.scope.md` |

The unidentified hand-coded HUD glyph table has been replaced. Only the Adafruit
font data is used, not its rendering implementation. LittleFS remains vendored
and unmodified; there is no maintained fork. Scope-specific adapters stay outside
the vendored files.

The full copyright notices, terms and disclaimers for both components are in
`THIRD_PARTY_NOTICES.md`. Application builds and update preparation copy it and
the project's MIT `LICENSE` beside their outputs. Carry both with distributed
binary packages. Attribution links alone do not replace the required notices.

## Stock-derived interoperability data

The earlier audit grouped all recovered constants and algorithms as unresolved
redistribution blockers. That was too broad. Recovering a register interface,
hardware behavior or functional lookup does not by itself establish that the
replacement implementation copies copyrightable vendor expression. Conversely,
being readable from a chip does not establish unrestricted redistribution rights.
For background, the [U.S. Copyright Office's Circular 33](https://www.copyright.gov/circs/circ33.pdf)
distinguishes methods/systems from their expressive descriptions; this is not a
jurisdiction-independent legal clearance of this particular firmware.

Retain these origin notes without inventing a vendor open-source license:

- `firmware/tables/adc_setup.h`, `adc_normal_setup.h`, `analog_setup.h`,
  `channel_plans.h`, `rate_plans.h`, `sample_rates.h` and `selftest_setup.h` record
  hardware writes, values and delays recovered from stock execution traces.
- `firmware/tables/factory_adc.h` contains the numeric stock ADC scale/trim lookup
  arrays. These are extracted lookup data, not executable vendor instructions.
- `firmware/tables/control_calibration.h` retains unit-derived zero/slope fallback
  values. `scope_position_dac()` replaces them with live factory values where
  available. They should not be described as independently measured generic
  defaults or confused with optional reference corrections in the settings journal.
- `firmware/include/capture_registers.h`, `timer_rounding.h`,
  `firmware/acquisition/trigger_plan.c`, `firmware/hardware/fpga_bus.c` and related
  acquisition/calibration modules document stock addresses used to recover behavior.
  Those annotations are provenance, not evidence by themselves of literal copying.

This inventory records the reverse-engineering basis rather than claiming a
clean-room history. A newly selected MIT license does not relicense vendor code
if any is subsequently identified. No stock application executable, Ghidra output,
FPGA bitstream, bootloader or factory filesystem image is checked into this repo.

## Application packaging codec and runtime support

`tools/codec/lzss.c` is the project's host-side LZSS application-container
compressor/decompressor. It implements the BOOT1-compatible stream format described
in its header: a 4096-byte window, literal flags and length/offset matches. It
is not a signal, image, USB or third-party multimedia codec. An earlier version
exists in this project's research workspace; no external implementation or
upstream copyright notice was identified during this review. Generic LZSS format
similarities are not a basis for inventing an upstream attribution or demanding
permission for the algorithm itself. It falls under the project's MIT license;
retain any actual upstream notices if later evidence of copied code appears.

`firmware/libc/` contains small freestanding implementations and ARM ABI helpers.
No compiler-runtime archive is linked. Standard ABI symbols and common algorithms
alone do not identify an external implementation. These sources fall under the
project license, subject to any specific provenance evidence discovered later.

## Generated artifacts kept outside release contents

`tools/build_dram.py` reads a user-supplied flash backup and copies actual BOOT0
instructions from `0x7ec..0x146b` and `0x2ac0..0x2c43`, patching two branches.
Its `build/dram/stock_dram.bin`, assembly, ELF and loader segments contain stock
executable code. They are excluded from Git and are not release binaries. This is
a concrete binary-copy distinction from the functional numeric tables above.

The FEL installer's `tools/prepare_install.py` retains a 28-byte container header
from the unit snapshot. The CI packager, `tools/package_update.py`, instead
constructs that header from documented format constants and requires no unit
snapshot or stock executable. Both install containers differ from the plain
source-linked `scope.bin`; their manifests identify the exact packaged build. Full dumps,
identity/calibration/settings snapshots and readback backups remain private under
ignored build directories or in research. Ordinary updates reuse the FPGA and
factory records already installed on the user's scope.

## External tools and technical references

`THIRD_PARTY_NOTICES.md` lists Python packages, libusb, compiler/linker and FEL
tools with upstream license links. They are separately installed dependencies;
no wheels, environments, toolchain binaries or caches are release contents.
Bundling them would require their own complete component notices.

ESP32 fixture firmware, its controller and dependent sampled-Pulse observation
script live in research, outside this repository. No Arduino/ESP-IDF framework or
ESP32 binary is part of SlopScope. Optional host UART logging uses pySerial.

Linux USB/display/pin/clock drivers, libusb and ARM register/ABI documentation
are technical references cited in architecture/diagnostic notes. No Linux, Apple
SDK, libusb or ARM runtime implementation was identified as vendored source.

## Audit limits

Review covered source license headers, attribution annotations, URLs, generated
tables, external dependency use, embedded instruction extraction and the proposed
Git file set. No other copied library was identified. There is no complete
historical authorship trail, so this is an inventory of evidence, not a legal
certification of every line. Review the exact archive and its notices before
publishing; do not upload ignored research artifacts as release attachments.
