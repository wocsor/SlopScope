import argparse
import json
import shutil
import struct
import subprocess
from pathlib import Path

from flash_layout import APPLICATION_END, APPLICATION_START
from unit_state import load_unit, preserve_unit, sha256

from build import build


def main():
    root = Path(__file__).resolve().parents[1]
    out = root / "build/flash"
    parser = argparse.ArgumentParser(description="Prepare an update using retained unit data.")
    parser.add_argument(
        "--high-speed", action="store_true", help="High-speed capable USB (default)"
    )
    parser.add_argument("--full-speed", action="store_true")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--backup", type=Path, help="Existing full backup, for initial migration")
    source.add_argument("--unit-state", type=Path, help="Reusable compact unit snapshot directory")
    args = parser.parse_args()
    out.mkdir(parents=True, exist_ok=True)
    if args.backup:
        directory = out / "units" / sha256(args.backup.read_bytes())
        if not (directory / "unit.json").exists():
            preserve_unit(args.backup, directory)
    else:
        directory = args.unit_state.resolve()
    manifest = load_unit(directory)
    high_speed = not args.full_speed
    build(high_speed=high_speed, cold_boot=False, volatile_settings=True)
    build(high_speed=high_speed, cold_boot=True)
    application = (root / "build/cold/scope.bin").read_bytes()
    compressed = out / "boot1_compressed.bin"
    decoded = out / "boot1_decoded.bin"
    codec = root / "build/tools/lzss"
    codec.parent.mkdir(parents=True, exist_ok=True)
    if not codec.is_file():
        subprocess.run(
            ["cc", "-O2", "-o", str(codec), str(root / "tools/codec/lzss.c")], check=True
        )
    subprocess.run(
        [str(codec), "c", str(root / "build/cold/scope.bin"), str(compressed)], check=True
    )
    stream = compressed.read_bytes()
    header = bytearray((directory / manifest["regions"]["application_header"]["file"]).read_bytes())
    struct.pack_into("<I", header, 0, len(stream) + 28)
    struct.pack_into("<I", header, 24, len(application))
    if APPLICATION_START + len(header) + len(stream) > APPLICATION_END:
        raise RuntimeError("Application overlaps identity/factory state")
    subprocess.run(
        [str(codec), "d", str(compressed), str(decoded), str(len(application))], check=True
    )
    if decoded.read_bytes() != application:
        raise RuntimeError("BOOT1 input does not reproduce linked image")
    payloads = []
    for name, offset, data in [
        ("application", APPLICATION_START, bytes(header) + stream),
        ("logo", 0xC000, (root / "build/boot_logo.bin").read_bytes()),
    ]:
        path = out / f"{name}.payload.bin"
        path.write_bytes(data)
        payloads.append(
            {"file": path.name, "offset": offset, "bytes": len(data), "sha256": sha256(data)}
        )
    plan = {
        "format": 2,
        "unit_state": str(directory.resolve()),
        "identity_sha256": manifest["regions"]["identity"]["sha256"],
        "app_sha256": sha256(application),
        "payloads": payloads,
        "status": "prepared; changed sectors determined from targeted live reads",
        "preserved": [
            "bootloaders",
            "identity",
            "FPGA",
            "factory filesystem",
            "live settings journal",
        ],
        "flash_written": False,
        "usb_speed": "high-capable" if high_speed else "full",
    }
    (out / "install_plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    shutil.copyfile(root / "THIRD_PARTY_NOTICES.md", out / "THIRD_PARTY_NOTICES.md")
    shutil.copyfile(root / "LICENSE", out / "LICENSE")
    print(json.dumps(plan, indent=2))


if __name__ == "__main__":
    main()
