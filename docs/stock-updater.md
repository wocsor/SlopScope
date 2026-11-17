# Stock updater compatibility

An application-only SlopScope update is feasible on the analyzed stock V4.0.0
firmware. A source-built package passed the original ARM updater offline and reached its
success state. The owner subsequently reported successful installation on the
other scope through the stock updater. FEL also remains available.

The stock updater reads `Scope.update` from USB storage. After decoding the
file it validates the package checksum, manifest checksum, model, version rules
and enabled payload checksums. No cryptographic signature check was found in
this path. A package can enable only the application slot, whose flash target
is `0x349000`, matching SlopScope's existing boot container.

## Recovered format

The decoded file begins with a 300-byte header. Multibyte integers are little
endian. Offsets below refer to the decoded file.

| Offset | Field |
| --- | --- |
| `0x00` | Checksum of all bytes after this word |
| `0x04` | Version string, 16-byte field |
| `0x14` | File-type label before the payload descriptors |
| `0x20` | Nine 16-byte payload descriptors: enabled, offset, length, checksum |
| `0xb4` | Manifest offset, length and checksum |
| `0xc0` | Model string, compared against the unit's model before its comma |

Descriptor index 4 selects the application. Its payload is the existing
28-byte application container header followed by LZSS data. The prototype pads
this payload to 4 KiB because the stock flash writer programs whole sectors.
Its manifest declares a destination version and an accepted source version.

The on-disk encoding subtracts repeating ASCII bytes `llp_owon` modulo 256;
the updater reverses this by addition. Checksums use polynomial `0x1021`,
initial value zero, signed-byte input and a 32-bit accumulator without a
16-bit truncation. A standard CRC-16 implementation is not interchangeable.

## Offline evidence and write behavior

Original V4 ARM code accepted the prototype and issued these simulated writes:

- Application: `0x349000`, 102,400 bytes.
- Stock identity/version area: `0x519000`, 202,000 bytes.

The second write is part of the normal stock update flow even for an
application-only package. The updater reconstructs that data from the running
unit, including its identity and stock parameter fields, and replaces its
version string. This is not the byte-preserving application-only behavior of
the FEL installer. The observed targets exclude the factory filesystem at
`0x700000`, the FPGA payload and SlopScope's journal at `0xffe000`.

Corrupted whole-file checksum, mismatched model and corrupted application
payload cases were also rejected by the original updater before any simulated
flash write.

The two preliminary commands `0x1b` and `0x19` were traced through the analyzed
V4 dispatcher; neither reached the low-level flash writer. Their status is
ignored by the updater. Do not generalize this behavior to other versions
without tracing them.

File input, UI/OS services and the low-level flash write boundary were intercepted
in the offline run. Package parsing, decoding, model/version decisions, per-section
checksumming, dispatch and identity reconstruction executed as original ARM code.
The shared checksum helper was compared against original ARM with a vector
covering all byte values before replacing it with an equivalent host routine
for the larger trace. This confirms the package path, not real USB-storage
behavior, flash programming or subsequent boot.

## Remaining qualification

Run the package through the stock UI on a backed-up unit, then compare the
application, identity, factory records and boot behavior. The V3.1.1 dump shows
a similar updater, but the prototype has only been traced against V4.0.0.
Those offline traces did not modify a scope. On 2026-11-17 the owner reported
a successful stock-updater installation on the other scope. Its original stock
version was not recorded, and a post-update application/factory readback comparison
has not been performed on that unit.

## Source-built package and CI

Build a normal cold application, then generate the file without any stock dump
or unit snapshot:

```sh
.venv/bin/python -B tools/build.py
.venv/bin/python -B tools/package_update.py
```

The outputs are `build/update/Scope.update` and `build/update/SlopScope-fel.bin`.
Both contain the same padded application payload. Copy `Scope.update` to the root
of a FAT32 USB drive. The package enables application descriptor 4 only; it does
not replace bootloaders, the FPGA, factory filesystem, settings journal or the
separate boot-logo region. The SlopScope application contains its own boot-console
artwork, but an existing BOOT1 logo can still appear before the application runs.

The packager constructs the 28-byte container header from format constants:
total container length, observed format word `0xc0043048`, the 16-byte
`OLWIOLNLCINP1U2T` marker and decompressed length. It validates the application
manifest, compression round trip and slot bounds, then creates the recovered
V4 checksums and encoding. It accepts source version V4.0.0 and model DOS1102;
there is no force-version rule or embedded identity/calibration record.

The exact November 17 package was accepted by the original V4 ARM updater offline,
with success stages 10/11 and only the expected application and identity/version
writes. Its application SHA-256 is
`cccac2e9589a0d9255ec446880d444e00deb4fa45b4495c6bddc6802c1e19e6c`.
The owner reported that the CI-built package installed successfully on the
other scope. A brief Hanmatek logo before SlopScope is expected: the package
retains BOOT1 and the existing logo region. That behavior is intentionally kept.
This report establishes a successful boot, not a full calibration/flash comparison.

The GitHub Actions workflow builds and packages from checked-in source and uploads
an explicit artifact list. It excludes unit snapshots, research, full dumps and
the stock-containing FEL DRAM helper. The artifact includes the MIT license,
third-party notices, source commit, dependency/compiler versions and SHA256SUMS.
CI builds downloadable files only. Users flash `SlopScope-fel.bin` locally using
[FEL instructions](fel-install.md), or copy `Scope.update` onto USB storage for
the stock updater. No CI step loads or flashes a device. CI upload is a build
artifact, not an automatic GitHub Release publication.
