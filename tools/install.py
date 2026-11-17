#!/usr/bin/env python3
"""Install application/logo sectors using targeted reads and retained unit data.

Default is review only. --write backs up each candidate region, compares sectors,
programs changes and verifies readback. Never writes bootloaders or factory data.
"""

import argparse
import datetime
import json
import subprocess
import time
from pathlib import Path

from flash_layout import APPLICATION_END, APPLICATION_START, JOURNAL_START, SECTOR_BYTES
from toolchain import fel as find_fel
from unit_state import IDENTITY_BYTES, IDENTITY_START, load_unit, sha256

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/flash"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--from-runtime", action="store_true")
    parser.add_argument("--restart", action="store_true")
    args = parser.parse_args()
    plan = json.loads((OUT / "install_plan.json").read_text())
    if plan.get("format") != 2:
        raise ValueError("Run prepare_install.py again to create a targeted update plan")
    manifest = load_unit(Path(plan["unit_state"]))
    if manifest["regions"]["identity"]["sha256"] != plan["identity_sha256"]:
        raise ValueError("Unit identity changed since preparation")
    payloads = []
    intervals = []
    for entry in plan["payloads"]:
        data = (OUT / entry["file"]).read_bytes()
        start, end = entry["offset"], entry["offset"] + len(data)
        sector_end = (end + SECTOR_BYTES - 1) // SECTOR_BYTES * SECTOR_BYTES
        if start % SECTOR_BYTES or not (
            0xC000 <= start < end <= sector_end <= 0xC8000
            or APPLICATION_START <= start < end <= sector_end <= APPLICATION_END
        ):
            raise ValueError("Payload outside application/logo sector boundaries")
        if len(data) != entry["bytes"] or sha256(data) != entry["sha256"]:
            raise ValueError("Prepared payload changed")
        if any(
            start < other_end and other_start < sector_end for other_start, other_end in intervals
        ):
            raise ValueError("Overlapping payloads")
        intervals.append((start, sector_end))
        payloads.append((start, sector_end, data))
    print(json.dumps(plan, indent=2), flush=True)
    if not args.write:
        return
    loader = str(find_fel())

    def fel(*arguments):
        subprocess.run([loader, *map(str, arguments)], check=True)

    if args.from_runtime:
        from device import ScopeControl

        with ScopeControl() as control:
            print(control.request("REBOOT FEL"), flush=True)
        deadline = time.monotonic() + 8
        while subprocess.run([loader, "ver"], capture_output=True, check=False).returncode:
            if time.monotonic() > deadline:
                raise RuntimeError("FEL did not return")
            time.sleep(0.2)
    fel("ver")
    fel("writel", "0x01c05008", "0x84")
    info = subprocess.check_output([loader, "spiflash-info"], text=True)
    if "C8h" not in info or "40h" not in info or "16777216 bytes" not in info:
        raise RuntimeError("Unexpected or unreadable SPI flash: " + info)
    run = OUT / "installs" / datetime.datetime.now(datetime.UTC).strftime("%Y%m%dT%H%M%S%fZ")
    run.mkdir(parents=True)

    def read_region(start, length, name):
        path = run / name
        fel("spiflash-read", hex(start), hex(length), path)
        data = path.read_bytes()
        if len(data) != length:
            raise RuntimeError(f"Short read at {start:#x}")
        return data

    identity = read_region(IDENTITY_START, IDENTITY_BYTES, "identity-check.bin")
    if sha256(identity) != plan["identity_sha256"]:
        raise RuntimeError("Connected unit does not match retained identity; no sectors written")
    journal = read_region(JOURNAL_START, 8192, "settings-before.bin")
    changes = []
    read_bytes = IDENTITY_BYTES + 8192
    for start, end, payload in payloads:
        current = read_region(start, end - start, f"before-{start:06x}.bin")
        read_bytes += len(current)
        target = payload + current[len(payload) :]
        changed_start = None
        for position in range(0, len(target) + SECTOR_BYTES, SECTOR_BYTES):
            changed = position < len(target) and (
                current[position : position + SECTOR_BYTES]
                != target[position : position + SECTOR_BYTES]
            )
            if changed and changed_start is None:
                changed_start = position
            elif not changed and changed_start is not None:
                changes.append((start + changed_start, target[changed_start:position]))
                changed_start = None
    ranges = [[start, start + len(data)] for start, data in changes]
    print(json.dumps({"changed_runs": ranges, "prewrite_bytes_read": read_bytes}), flush=True)
    for index, (start, data) in enumerate(changes):
        path = run / f"write-{index}.bin"
        path.write_bytes(data)
        print(f"Writing {start:#x}..{start + len(data):#x}", flush=True)
        fel("spiflash-write", hex(start), path)
        if read_region(start, len(data), f"readback-{index}.bin") != data:
            raise RuntimeError(
                f"Readback mismatch at {start:#x}; stay in FEL and use the saved region backup"
            )
        print("Readback matched", flush=True)
    if read_region(JOURNAL_START, 8192, "settings-after.bin") != journal:
        raise RuntimeError("Settings journal changed unexpectedly")
    result = {
        "app_sha256": plan["app_sha256"],
        "runs": ranges,
        "verified": True,
        "settings_preserved": True,
        "identity_matched": True,
        "prewrite_bytes_read": read_bytes,
    }
    (run / "installed.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Installation verified.", flush=True)
    if args.restart:
        print("Restarting through the normal ROM/BOOT0/BOOT1 path...", flush=True)
        fel("writel", "0x01c20cb4", "1")
        fel("writel", "0x01c20cb8", "1")
    else:
        print("Scope remains in FEL; ready for power-cycle.", flush=True)


if __name__ == "__main__":
    main()
