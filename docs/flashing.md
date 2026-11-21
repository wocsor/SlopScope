# Installing SlopScope

## Which file do I need?

A release has two installation files. Pick the one for your update route:

| File | What it's for |
| --- | --- |
| `Scope.update` | Installing through the stock firmware's USB-drive updater |
| `SlopScope-fel.bin` | Writing the application through FEL, including updates from SlopScope |

The supported target is the HANMATEK DOS1102. `Scope.update` accepts stock V4.0.0;
other model names and stock versions haven't been qualified.

Download your installer and `SHA256SUMS` from the same release. For example:

```sh
shasum -a 256 Scope.update
```

Compare that hash with the `Scope.update` entry in `SHA256SUMS`. Use
`SlopScope-fel.bin` instead if you're installing through FEL. Actions artifacts
come as a ZIP; after extracting the full bundle, you can check everything with
`shasum -a 256 -c SHA256SUMS`.

Keep your original recovery backup somewhere safe and private. You don't need to
make another full-flash dump for every SlopScope update.

## If you're still on stock

1. Copy `Scope.update` to the root of a FAT32 USB flash drive. Keep that exact filename.
2. Eject the drive from the PC. Disconnect the scope's PC USB data cable, then put
   the drive in the scope's USB-A port.
3. Open the firmware-update option in the stock Utility menu and follow the prompts.
4. Wait for it to finish, then power-cycle the scope.

This route has worked on the other scope. The package installs the SlopScope
application; the stock updater also updates its identity/version record as part
of its normal procedure. The FPGA and factory filesystem stay in place.

## If you're on SlopScope, or want to use FEL

SlopScope updates currently go through FEL. It doesn't have its own USB-drive updater.
The host/device role details are in
[the USB host/device notes](status.md#what-about-the-two-usb-ports).

1. Send `REBOOT FEL` through the [USB interface](https://github.com/wocsor/SlopScope/blob/HEAD/docs/protocol.md), read the reply, and
   close the USB client. If that isn't available, use the board's hardware recovery
   procedure to enter FEL.
2. Install `sunxi-fel` from [sunxi-tools](https://github.com/linux-sunxi/sunxi-tools).
3. Connect one scope at a time. From the directory containing your downloaded installer, run:

```sh
sunxi-fel ver
sunxi-fel writel 0x01c05008 0x84
sunxi-fel spiflash-write 0x349000 SlopScope-fel.bin
sunxi-fel spiflash-read 0x349000 "$(wc -c < SlopScope-fel.bin)" SlopScope-readback.bin
cmp SlopScope-fel.bin SlopScope-readback.bin
```

`sunxi-fel ver` needs to identify the device before you write anything. `cmp`
returns no output when the readback matches. Once it matches, power-cycle normally.
If it doesn't match, keep the scope in FEL and resolve that before rebooting.

`SlopScope-fel.bin` is an application container written at **`0x349000`**, not a
full-flash image. This route writes the application sectors and leaves the
bootloaders, logo, FPGA, identity, factory records and saved settings in place.
You can also use it from stock once the scope is in FEL.

## What happens to settings and calibration?

SlopScope reads the factory calibration records already in your scope. You don't
need to recalibrate just because you've installed or updated it. That isn't an
accuracy certification; the [status doc](status.md#what-still-needs-checking)
explains the remaining calibration limits.

Saved settings use two 4 KiB sectors at `0x517000` and `0x518000`. Release payloads
end before `0x517000`. The repository's guarded FEL installer also checks that the
settings sectors stay intact; the manual commands above verify the application readback.

The first settings save only initializes erased space or verified unused zero
padding beyond the installed application. Unknown content is protected. Older
SlopScope settings can be read from their legacy location and migrated without
changing the stock filesystem.

Export a setup before restoring stock or installing unrelated firmware. Those
images don't have to reserve SlopScope's settings space.

## What should I see afterward?

The scope should reach the grid and traces, restore saved scope settings, and show
up on USB as `DEAF:BABE`. You may still see a brief Hanmatek logo at the start,
since these files leave the existing bootloader and logo in place. See
[status](status.md) for session-only
cursor/Math settings and the current limitations.
