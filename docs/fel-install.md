# Flashing the CI binary through FEL

Download a successful GitHub Actions build and extract it. `SlopScope-fel.bin`
is a sector-padded, compressed application container for flash offset `0x349000`.
It is not a full-flash image or an image to execute directly from RAM. The
uncompressed `build/cold/scope.bin` used by development tools is a different file.

Keep the scope's original recovery backup and unit configuration/calibration
snapshot. Close other scope clients, enter FEL and verify the downloaded files
against the accompanying SHA256SUMS. With `sunxi-fel` installed:

```sh
sunxi-fel ver
sunxi-fel writel 0x01c05008 0x84
sunxi-fel spiflash-write 0x349000 SlopScope-fel.bin
sunxi-fel spiflash-read 0x349000 "$(wc -c < SlopScope-fel.bin)" SlopScope-readback.bin
cmp SlopScope-fel.bin SlopScope-readback.bin
```

Power-cycle normally after readback matches. This writes only application sectors;
bootloaders, FPGA, logo, identity, factory records and the settings journal remain
outside that write range. The manifest supplies `application_flash_offset`,
`application_flash_bytes` and `fel_image_sha256` for the exact binary.

The pre-application stock logo can briefly appear because BOOT1 and its logo
region are retained. SlopScope supplies its own boot-console artwork afterward.

FEL flashing is performed locally by the user. CI builds both installation files
from the same application payload and uploads them with licenses and checksums;
there is no deployment or device-access step in the workflow.
