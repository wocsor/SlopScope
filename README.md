# SlopScope

Replacement application firmware for the HANMATEK DOS1102 hardware used in this
project: Allwinner F1C200s, FPGA acquisition, 800×480 LCD and 16 MiB SPI flash.
SlopScope implementation code is [MIT-licensed](LICENSE). Vendored components
retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
See [release status](RELEASE_READINESS.md)
for the verified build and remaining limitations.

Two channels, live LCD traces, USB streaming, rolling previews, 1K/10K records,
voltage/time controls, Edge triggering and saved settings are implemented.
Factory calibration is read from the connected unit in place. Optional zero and
external-reference corrections are stored separately in its settings journal.

## PC control

SlopScope is the firmware project. The Python control library (`hscope`) and
web UI each have their own repositories. The web UI can stream waveforms and
control the scope directly through WebUSB, or through its optional Python bridge.

Building and installing SlopScope requires neither PC project. Its USB identity
is `DEAF:BABE`. Close other scope clients before using maintenance tools; one
client owns the USB interface at a time. A USB symbol in the LCD’s upper-right
corner indicates an active viewer, bridge or library session, rather than just a
USB cable. Separate TX (green, scope to PC) and RX (amber, PC to scope) lamps
hold activity for 250 ms. Disconnecting a client clears the indicators; a lost
client expires after three seconds without consuming a reply.

## Build

Install Clang with ARM target support and LLVM `ld.lld`, then:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -B tools/build.py
```

The default produces a high-speed capable cold-boot application in `build/cold/`.
`scope.bin`, `scope.elf`, `scope.map` and `manifest.json` are generated there.
`--full-speed` selects full-speed USB. `ARM_CLANG` and `ARM_LD_LLD` override
compiler discovery. A usable data cable is needed for high-speed negotiation.

## CI binaries

Git contains source, not compiled firmware. GitHub Actions builds the cold-boot
application and `Scope.update` on Ubuntu with Clang/LLD 18. Download the artifact
named `SlopScope-<commit>` from a successful Build firmware run. It includes the
application, updater file, manifests, checksums, licenses, source commit and build
dependency/toolchain versions. CI needs no stock dump, unit snapshot or secret.

The updater package currently targets DOS1102 stock V4.0.0 only. Check
[updater qualification](docs/stock-updater.md) before installation. Other model
names and stock versions have not been qualified. Local and CI compiler versions
can produce different application hashes; each manifest describes its own build.

## RAM development

The application builds entirely from source. The FEL DRAM helper additionally
extracts two checked BOOT0 instruction blocks from your own full-flash backup.
It is a separate loader artifact, not part of the installed CFW application.
Install `sunxi-fel` or set `SUNXI_FEL` to its executable.

```sh
.venv/bin/python -B tools/build_dram.py --backup /path/to/own.full.bin
.venv/bin/python -B tools/build.py --ram
# With this scope in FEL:
.venv/bin/python -B tools/load.py
```

RAM builds use volatile settings. The loader verifies uploads before execution;
it does not program flash. `--cold-image` loads the cold entry for development.

## Install

The [stock USB-drive updater](docs/stock-updater.md) has accepted a prototype
package in offline execution of the original firmware. Hardware installation
through that path still needs qualification; the verified method below uses FEL.

Keep the original full recovery backup once. Convert it to reusable compact unit
state, then prepare and install subsequent updates from that directory:

```sh
# Once, from this unit's existing backup:
.venv/bin/python -B tools/unit_state.py --backup /path/to/own.full.bin --output build/unit-state
# For each update:
.venv/bin/python -B tools/prepare_install.py --unit-state build/unit-state
.venv/bin/python -B tools/install.py
# With this scope in FEL:
.venv/bin/python -B tools/install.py --write --restart
```

`unit_state.py --factory-record /path/to/factory-record.bin` additionally retains
an already validated `FACTORY TABLE` export. This is the decoded calibration
file, not a raw filesystem recovery image. The snapshot contains the container
header, stock identity/configuration and settings at backup time, with checksums.
It is reused without overwriting. `prepare_install.py --backup` also accepts an
existing full backup and creates compact unit state automatically.

The installer verifies the connected unit's identity, reads only the candidate
application/logo sectors and current 8 KiB settings journal, and backs up those
regions before changing anything. Only differing sectors are written and read
back; the unused final sector bytes are preserved. Bootloaders, identity, FPGA,
factory records and the live journal are never update targets. Ordinary updates
need no new full-flash dump. The settings journal may change with user settings,
so its old snapshot is not used to replace the live one. Keep unit snapshots,
region backups and full recovery images private.

See [architecture](docs/architecture.md), [USB interface](docs/protocol.md),
[USB failure capture](docs/usb-diagnostics.md),
[hardware](docs/hardware.md) and [source conventions](docs/style.md).
LittleFS is vendored with its [BSD license](vendor/littlefs/LICENSE.md) and
[integration notice](vendor/littlefs/README.scope.md).

See [third-party notices](THIRD_PARTY_NOTICES.md) and the
[code provenance audit](docs/provenance.md) before distributing source or binaries.
Build and update preparation copy the notices beside their outputs; include
them with binary packages. ESP32 fixture firmware stays outside this repository.
