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

Git contains source, not compiled firmware. GitHub Actions builds two installation
files on Ubuntu with Clang/LLD 18:

- `SlopScope-fel.bin`: flash the application slot at `0x349000` using FEL.
- `Scope.update`: install using the stock firmware's USB-drive updater.

Download the artifact named `SlopScope-<commit>` from a successful Build firmware
run. It also includes the ELF, manifests, checksums, licenses, source commit and
build dependency/toolchain versions. CI only builds and uploads files; it never
connects to or flashes a scope. No stock dump, unit snapshot or secret is needed.
See [FEL installation](docs/fel-install.md) for the flash offset and readback steps.

The updater package currently targets DOS1102 stock V4.0.0 only. Check
[updater qualification](docs/stock-updater.md) before installation. Other model
names and stock versions have not been qualified. Local and CI compiler versions
can produce different application hashes; each manifest describes its own build.

## Releases from a branch

Push a version branch such as `release/0.1.0` to cut release `v0.1.0`:

```sh
git switch -c release/0.1.0
git push -u origin release/0.1.0
```

The successful build uploads `SlopScope-fel.bin`, `Scope.update`, checksums,
licenses and build metadata to GitHub Releases. CI stages the assets in a draft
and publishes only after upload verification. The tag points to the exact built
commit. Ordinary branches and pull requests only produce Actions artifacts.
No extra secret is needed: the publishing job uses GitHub's token with repository
contents permission. CI never connects to a scope.

Use one version branch per release. After publication, rerunning the same commit
leaves its assets unchanged; pushing a different commit to that version branch
fails publication. Fixes use a new version, such as `release/0.1.1`. A branch such
as `release/0.2.0-rc.1` creates a prerelease. The version after `release/` must fit
within 14 characters because the stock updater reserves one character for `V`.
The updater's destination version matches the branch, for example `V0.1.0`.

Selecting a release branch in **Run workflow** also runs the release path. A
failed draft upload can be retried for the same commit. Repository visibility
still applies: releases in a private repository require access to that repo.

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
