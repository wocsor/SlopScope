#!/usr/bin/env python3
"""Retrieve a checked RGB565 LCD snapshot and write a top-down BMP image."""

import argparse
import struct
import time
import zlib
from pathlib import Path

from device import ScopeControl


def status_fields(reply):
    return {name: int(value, 16) for name, value in
            (field.split("=", 1) for field in reply.split() if "=" in field)}


def read_chunk(scope, snapshot_id, offset):
    scope.device.write(1, f"SCREENSHOT READ {snapshot_id} {offset}\n".encode("ascii"))
    response = bytearray()
    while len(response) < 32:
        response.extend(scope.device.read(0x81, 512))
        if response.startswith(b"ERR "):
            raise RuntimeError(response.decode("ascii").strip())
    magic, reply_id, reply_offset, size, total, width, height, checksum = struct.unpack(
        "<8I", response[:32]
    )
    if (magic != 0x31534353 or reply_id != snapshot_id or reply_offset != offset or
            not 0 < size <= 448 or total != 768000 or (width, height) != (800, 480)):
        raise RuntimeError("Invalid screenshot chunk header")
    while len(response) < 32 + size:
        response.extend(scope.device.read(0x81, 512))
    if len(response) != 32 + size:
        raise RuntimeError("Unexpected trailing screenshot bytes")
    struct.pack_into("<I", response, 28, 0)
    if zlib.crc32(response) != checksum:
        raise RuntimeError("Screenshot chunk CRC mismatch")
    return response[32:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--existing", action="store_true", help="Retrieve the last physical Copy")
    args = parser.parse_args()
    with ScopeControl() as scope:
        if not args.existing:
            scope.request("SCREENSHOT CAPTURE")
        deadline = time.monotonic() + 10
        while True:
            status = status_fields(scope.request("SCREENSHOT STATUS"))
            if status["ready"]:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("No completed screenshot; press Copy or omit --existing")
            time.sleep(0.05)
        pixels = bytearray()
        while len(pixels) < status["bytes"]:
            pixels.extend(read_chunk(scope, status["id"], len(pixels)))
    pixel_offset = 14 + 40 + 12
    file_header = struct.pack("<2sIHHI", b"BM", pixel_offset + len(pixels), 0, 0, pixel_offset)
    bitmap_header = struct.pack("<IiiHHIIiiII", 40, 800, -480, 1, 16, 3, len(pixels),
                                2835, 2835, 0, 0)
    masks = struct.pack("<III", 0xF800, 0x07E0, 0x001F)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(file_header + bitmap_header + masks + pixels)
    print(f"Saved snapshot {status['id']}: {args.output}")


if __name__ == "__main__":
    main()
