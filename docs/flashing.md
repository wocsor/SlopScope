# Flashing SlopScope

Download and extract a release. It contains two installation files:

| File | Use |
| --- | --- |
| `Scope.update` | Install from the stock firmware's USB-drive updater |
| `SlopScope-fel.bin` | Flash the application through FEL, including updates from SlopScope |

Keep your original recovery backup private. Check the downloaded files from the
extracted release directory:

```sh
shasum -a 256 -c SHA256SUMS
```

The target is DOS1102 hardware. The update package explicitly accepts stock
V4.0.0; other model names and versions are not qualified.

## From stock firmware

1. Copy `Scope.update` to the root of a FAT32 USB flash drive. Keep that exact filename.
2. Eject it from the PC, disconnect the scope's PC USB data cable, and insert
   the drive into the scope's USB-A host port.
3. Open the stock Utility menu's firmware-update option and follow its prompts.
4. Wait for the updater to report completion, then power-cycle the scope.

A successful stock-updater installation has been reported on a second scope.
Stock V4 switches its shared USB controller between host and PC-device roles;
it does not read the drive while maintaining a PC USB connection.
The stock updater also updates its identity/version record as part of its normal
procedure. It retains the FPGA, factory filesystem and SlopScope settings journal.

## From SlopScope, or through FEL

SlopScope currently uses FEL for firmware updates; it does not implement a USB
host or its own USB-drive updater.

1. Send `REBOOT FEL` over the [USB interface](protocol.md), consume the reply and
   close the client. If software reboot is unavailable, enter FEL through the
   board's recovery procedure instead.
2. Install `sunxi-fel` from [sunxi-tools](https://github.com/linux-sunxi/sunxi-tools).
3. From the extracted release directory, run:

```sh
sunxi-fel ver
sunxi-fel writel 0x01c05008 0x84
sunxi-fel spiflash-write 0x349000 SlopScope-fel.bin
sunxi-fel spiflash-read 0x349000 "$(wc -c < SlopScope-fel.bin)" SlopScope-readback.bin
cmp SlopScope-fel.bin SlopScope-readback.bin
```

`sunxi-fel ver` must identify the device before writing. If `cmp` succeeds with
no output, power-cycle normally. If comparison fails, keep the scope in FEL and
resolve the mismatch before rebooting.

`SlopScope-fel.bin` is an application container for **offset `0x349000`**, not a
full-flash image. This method changes application sectors only, preserving
bootloaders, logo, FPGA, identity, factory records and saved settings. It can also
be used from stock once the device is in FEL. Keep your existing recovery backup;
a new full-flash dump is not required for each update.

## After installation

The scope should reach its grid and traces, restore saved settings and enumerate
as USB `DEAF:BABE`. A brief Hanmatek logo before SlopScope is expected because the
original bootloader and its logo are retained. No recalibration is required just
to install or update SlopScope.
