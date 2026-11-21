"""Compact, checksummed unit data retained across application updates."""

import argparse
import hashlib
import json
from pathlib import Path

from flash_layout import FLASH_BYTES, JOURNAL_BYTES, JOURNAL_START, LEGACY_JOURNAL_START

IDENTITY_START = 0x519000
IDENTITY_BYTES = 202000
REGIONS = {
    "application_header": (0x349000, 28),
    "identity": (IDENTITY_START, IDENTITY_BYTES),
    "settings_at_backup": (LEGACY_JOURNAL_START, FLASH_BYTES - LEGACY_JOURNAL_START),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def preserve_unit(backup_path, output, factory_record=None):
    image = backup_path.read_bytes()
    if len(image) != FLASH_BYTES:
        raise ValueError("Initial backup must be exactly 16 MiB")
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "unit.json"
    if manifest_path.exists():
        raise FileExistsError("Unit snapshot already exists; reuse it or choose another directory")
    manifest = {"format": 1, "source_sha256": sha256(image), "regions": {}}
    for name, (offset, length) in REGIONS.items():
        data = image[offset : offset + length]
        path = output / f"{name}.bin"
        with path.open("xb") as stream:
            stream.write(data)
        manifest["regions"][name] = {
            "file": path.name,
            "offset": offset,
            "bytes": length,
            "sha256": sha256(data),
        }
    data = image[JOURNAL_START:JOURNAL_START + JOURNAL_BYTES]
    settings_path = output / "cfw_settings_at_backup.bin"
    with settings_path.open("xb") as stream:
        stream.write(data)
    manifest["cfw_settings_at_backup"] = {
        "file": settings_path.name, "offset": JOURNAL_START,
        "bytes": JOURNAL_BYTES, "sha256": sha256(data),
    }
    if factory_record:
        data = factory_record.read_bytes()
        if len(data) != 0x8650 or int.from_bytes(data[0x6C:0x70], "little") != 0x069194C5:
            raise ValueError("Expected the validated FACTORY TABLE export from this unit")
        with (output / "factory_record.bin").open("xb") as stream:
            stream.write(data)
        manifest["factory_record"] = {
            "file": "factory_record.bin",
            "bytes": len(data),
            "sha256": sha256(data),
            "representation": "decoded LittleFS file; not a raw filesystem recovery image",
        }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def load_unit(directory):
    manifest = json.loads((directory / "unit.json").read_text())
    if manifest["format"] != 1:
        raise ValueError("Unsupported unit snapshot")
    for name, (offset, length) in REGIONS.items():
        region = manifest["regions"][name]
        if region["offset"] != offset or region["bytes"] != length:
            raise ValueError(f"Invalid {name} boundaries")
        data = (directory / region["file"]).read_bytes()
        if len(data) != length or sha256(data) != region["sha256"]:
            raise ValueError(f"Unit snapshot changed: {name}")
    if "cfw_settings_at_backup" in manifest:
        region = manifest["cfw_settings_at_backup"]
        data = (directory / region["file"]).read_bytes()
        if (region["offset"] != JOURNAL_START or region["bytes"] != JOURNAL_BYTES or
                len(data) != JOURNAL_BYTES or sha256(data) != region["sha256"]):
            raise ValueError("Retained CFW settings snapshot changed")
    if "factory_record" in manifest:
        record = manifest["factory_record"]
        data = (directory / record["file"]).read_bytes()
        if len(data) != record["bytes"] or sha256(data) != record["sha256"]:
            raise ValueError("Retained factory record changed")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backup", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--factory-record", type=Path)
    args = parser.parse_args()
    manifest = preserve_unit(args.backup, args.output, args.factory_record)
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
