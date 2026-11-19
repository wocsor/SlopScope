#!/usr/bin/env python3
"""Build FEL and stock-updater installation files from a source-built application."""

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

from flash_layout import APPLICATION_END, APPLICATION_START, SECTOR_BYTES

ROOT = Path(__file__).resolve().parents[1]
ENCODING_KEY = b"llp_owon"
CONTAINER_TAG = b"OLWIOLNLCINP1U2T"
CONTAINER_FORMAT_WORD = 0xC0043048


def stock_checksum(data):
    checksum = 0
    for byte in data:
        checksum ^= ((byte if byte < 128 else byte - 256) << 8) & 0xFFFFFFFF
        for _ in range(8):
            checksum = ((checksum << 1) ^ (0x1021 if checksum & 0x8000 else 0)) & 0xFFFFFFFF
    return checksum


def package_application(image_dir, output, version):
    if not re.fullmatch(r"V[A-Za-z0-9._-]{1,14}", version):
        raise ValueError("Version must be 2–15 ASCII characters starting with V")
    manifest = json.loads((image_dir / "manifest.json").read_text())
    application_path = image_dir / "scope.bin"
    application = application_path.read_bytes()
    application_hash = hashlib.sha256(application).hexdigest()
    if (
        not manifest["cold_boot"]
        or manifest["entry"] != 0x80080000
        or manifest["volatile_settings"]
        or manifest["usb_diagnostics"]
        or len(application) != manifest["size"]
        or application_hash != manifest["sha256"]
    ):
        raise ValueError("Expected a matching normal cold-boot build and manifest")
    output.mkdir(parents=True, exist_ok=True)
    codec = ROOT / "build/tools/lzss"
    codec.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["cc", "-O2", "-o", str(codec), str(ROOT / "tools/codec/lzss.c")], check=True
    )
    compressed_path = output / "application.lzss"
    decoded_path = output / "application.roundtrip.bin"
    subprocess.run([str(codec), "c", str(application_path), str(compressed_path)], check=True)
    subprocess.run(
        [str(codec), "d", str(compressed_path), str(decoded_path), str(len(application))],
        check=True,
    )
    if decoded_path.read_bytes() != application:
        raise ValueError("Compressed application did not reproduce the source build")
    compressed = compressed_path.read_bytes()
    container = struct.pack(
        "<II16sI", len(compressed) + 28, CONTAINER_FORMAT_WORD, CONTAINER_TAG, len(application)
    ) + compressed
    payload = container + bytes(-len(container) % SECTOR_BYTES)
    if APPLICATION_START + len(payload) > APPLICATION_END:
        raise ValueError("Application exceeds its stock flash slot")
    (output / "SlopScope-fel.bin").write_bytes(payload)
    header = bytearray(300)
    header[4 : 4 + len(version)] = version.encode("ascii")
    header[20:29] = b"SlopScope"
    header[192:199] = b"DOS1102"
    version_rules = f"dest={version}\r\nsrc=V4.0.0\r\n".encode("ascii")
    struct.pack_into("<4I", header, 32 + 4 * 16, 1, 300, len(payload), stock_checksum(payload))
    struct.pack_into(
        "<3I", header, 180, 300 + len(payload), len(version_rules), stock_checksum(version_rules)
    )
    decoded = header + payload + version_rules
    struct.pack_into("<I", decoded, 0, stock_checksum(decoded[4:]))
    encoded = bytes(
        (byte - ENCODING_KEY[index % len(ENCODING_KEY)]) & 255
        for index, byte in enumerate(decoded)
    )
    (output / "Scope.update").write_bytes(encoded)
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copyfile(ROOT / name, output / name)
    result = {
        "format": 1,
        "model": "DOS1102",
        "source_versions": ["V4.0.0"],
        "destination_version": version,
        "application_sha256": application_hash,
        "application_bytes": len(application),
        "container_bytes": len(container),
        "application_flash_offset": APPLICATION_START,
        "application_flash_bytes": len(payload),
        "fel_image_file": "SlopScope-fel.bin",
        "fel_image_sha256": hashlib.sha256(payload).hexdigest(),
        "enabled_payloads": ["application"],
        "stock_updater_also_rewrites_identity_version": True,
        "stock_executable_or_unit_snapshot_input": False,
        "package_bytes": len(encoded),
        "package_sha256": hashlib.sha256(encoded).hexdigest(),
        "qualification": "Stock updater success reported by user; see docs/flashing.md",
    }
    (output / "update-manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-dir", type=Path, default=ROOT / "build/cold")
    parser.add_argument("--output", type=Path, default=ROOT / "build/update")
    parser.add_argument("--version", default="VSlopScope-0.1")
    args = parser.parse_args()
    print(json.dumps(package_application(args.image_dir, args.output, args.version), indent=2))


if __name__ == "__main__":
    main()
