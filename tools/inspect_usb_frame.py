"""Compare a failed bulk frame with the saved diagnostic snapshot over EP0."""

import argparse
import fcntl
import json
import os
import struct
import tempfile
import time
import zlib
from pathlib import Path

from device import PRODUCT_ID, VENDOR_ID

import usb.core
import usb.util


def describe_frame(data):
    if len(data) < 64:
        return {"bytes": len(data), "complete": False}
    words = struct.unpack_from("<16I", data)
    header_bytes, payload_bytes = words[2:4]
    complete = words[0] == 0x31435348 and len(data) == header_bytes + payload_bytes
    return {
        "bytes": len(data),
        "complete": complete,
        "version": words[1],
        "sequence": words[4],
        "ticks": words[6],
        "header_crc": words[15],
        "payload_crc": zlib.crc32(data[header_bytes:]) if complete else None,
        "crc_valid": complete and zlib.crc32(data[header_bytes:]) == words[15],
    }


def read_saved_frame(device):
    first = bytes(device.ctrl_transfer(0xC0, 0x5A, 2, 0, 124, timeout=1000))
    if len(first) < 64:
        raise RuntimeError("No saved frame or unsupported diagnostic request")
    words = struct.unpack_from("<16I", first)
    total = words[2] + words[3]
    if words[0] != 0x31435348 or not 64 <= words[2] <= 128 or not 64 <= total <= 4124:
        raise ValueError("Invalid saved frame bounds")
    data = bytearray(first)
    while len(data) < total:
        chunk = bytes(device.ctrl_transfer(0xC0, 0x5A, 2, len(data), 124, timeout=1000))
        if not chunk or len(data) + len(chunk) > total:
            raise ValueError("Saved-frame transfer did not match its bounds")
        data.extend(chunk)
    if bytes(device.ctrl_transfer(0xC0, 0x5A, 2, 0, 124, timeout=1000)) != first:
        raise RuntimeError("Saved snapshot changed during inspection; stop other bulk clients")
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--received", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--read-buffer", type=Path, help="Unreported diagnostic buffer, not valid received bytes"
    )
    parser.add_argument("--read-buffer-offset", type=int, default=0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lock = os.open(
        Path(tempfile.gettempdir()) / f"hscope-deaf-babe-{os.getuid()}.lock",
        os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW,
        0o600,
    )
    device = None
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        device = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)
        if device is None:
            raise RuntimeError("Scope is not enumerated; UART evidence remains available")
        source = read_saved_frame(device)
        received = args.received.read_bytes()
        (args.output / "saved-frame.bin").write_bytes(source)
        source_info, received_info = describe_frame(source), describe_frame(received)
        same_frame = (source_info.get("sequence"), source_info.get("ticks")) == (
            received_info.get("sequence"),
            received_info.get("ticks"),
        )
        differences = [
            index for index, (left, right) in enumerate(zip(source, received)) if left != right
        ]
        result = {
            "at": time.time(),
            "source": source_info,
            "received": received_info,
            "same_frame": same_frame,
            "different_bytes": len(differences),
            "different_bits": sum(
                (source[index] ^ received[index]).bit_count() for index in differences
            ),
            "first_differences": differences[:64],
            "received_prefix_matches": source[: len(received)] == received,
        }
        if args.read_buffer:
            unreported = args.read_buffer.read_bytes()
            prefix = next(
                (
                    i
                    for i, (left, right) in enumerate(
                        zip(source[args.read_buffer_offset :], unreported)
                    )
                    if left != right
                ),
                min(max(0, len(source) - args.read_buffer_offset), len(unreported)),
            )
            result["unreported_buffer"] = {
                "bytes": len(unreported),
                "matching_prefix": prefix,
                "frame_offset": args.read_buffer_offset,
                "note": "Buffer contents are not libusb-reported received data",
            }
        (args.output / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
    finally:
        if device is not None:
            usb.util.dispose_resources(device)
        os.close(lock)


if __name__ == "__main__":
    main()
