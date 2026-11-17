Vendored from littlefs-project/littlefs tag v2.9.3:
https://github.com/littlefs-project/littlefs/tree/v2.9.3

Files are retained upstream sources with their BSD-3-Clause license. The scope
build defines LFS_READONLY, LFS_NO_MALLOC, LFS_NO_ASSERT, LFS_NO_DEBUG,
LFS_NO_WARN and LFS_NO_ERROR. The integration supplies static caches and a bounded
SPI-flash read callback. No program/erase/sync callback is installed, and no
format or filesystem-write entry point is linked. It opens only table0/table1
at startup and checks exact length, stock checksum, model and coefficient bounds.

The five upstream files were byte-for-byte verified on 2026-11-17. `UPSTREAM.json`
records their SHA-256 hashes and source URLs. Keep the vendor files unmodified;
integration changes belong in the scope storage adapter. A separate fork is not
needed while no upstream changes are maintained.

The root `THIRD_PARTY_NOTICES.md` reproduces the full license for binary release
packages. Build and update preparation tools copy it beside their outputs;
distribute it with those outputs. Do not distribute a firmware binary alone.
