#!/usr/bin/env python3
"""FEL-only DRAM test / display bring-up. Never programs flash.
Each execution saves separate artifacts and restores the occupied lower SRAM range.
DRAM, DDR controller and display state intentionally remain configured afterward.
"""

import argparse
import datetime
import hashlib
import json
import struct
import subprocess
import time
from pathlib import Path

from toolchain import fel as find_fel

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/dram"

FEL = find_fel()
KEYS = [
    "magic",
    "version",
    "stage",
    "error",
    "flags",
    "sctlr",
    "dram_result",
    "tested_bytes",
    "fail_addr",
    "expected",
    "actual",
    "dram_cr",
    "dram_pll",
    "framebuffer",
    "width",
    "height",
    "sparse_locations",
    "passes",
]


def fel(*args, timeout=30):
    return subprocess.run(
        [str(FEL), *map(str, args)], capture_output=True, check=True, timeout=timeout
    ).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--display", action="store_true")
    ap.add_argument("--dram-only", action="store_true")
    args = ap.parse_args()
    assert not (args.display and args.dram_only)
    manifest = json.loads((BUILD / "manifest.json").read_text())
    fingerprint = hashlib.sha256((BUILD / "manifest.json").read_bytes()).hexdigest()
    if args.display:
        prior = json.loads((BUILD / "memory_hardware.json").read_text())
        assert prior.get("manifest_sha256") == fingerprint, (
            "Memory test must pass for this exact build first"
        )
        assert (
            prior.get("stage") == 3
            and prior.get("error") == 0
            and prior.get("tested_bytes") == 0x2000000
        )
        assert prior.get("passes") == 2 and prior.get("sram_restored")
    for s in manifest["segments"]:
        a, n = s["address"], s["size"]
        assert 0x2000 <= a and a + n <= 0x4800
        data = (BUILD / s["file"]).read_bytes()
        assert len(data) == n and hashlib.sha256(data).hexdigest() == s["sha256"]
    version = fel("ver").decode().strip()
    assert "soc=00001663" in version
    print(version, flush=True)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    out = BUILD / "runs" / stamp
    out.mkdir(parents=True)
    result = {
        "manifest_sha256": fingerprint,
        "display_requested": args.display,
        "flash_written": False,
        "run_directory": str(out),
    }
    backed_up = False
    restored = False
    try:
        for a, n in ((0x2000, 0x3800),):
            fel("read", hex(a), hex(n), out / f"before_{a:x}.bin")
        backed_up = True
        fel("read", "0x01c20000", "0x300", out / "ccu_before.bin")
        for s in manifest["segments"]:
            a = s["address"]
            fel("write", hex(a), BUILD / s["file"])
            fel("read", hex(a), s["size"], out / f"upload_{a:x}.bin")
            assert (out / f"upload_{a:x}.bin").read_bytes() == (BUILD / s["file"]).read_bytes()
        (out / "request.bin").write_bytes(
            struct.pack("<I", 7 if args.display else 1 if args.dram_only else 3)
        )
        fel("write", "0x49f0", out / "request.bin")
        print(
            "SRAM uploads verified. Running "
            + ("DDR initialization only" if args.dram_only else "DDR and 32 MB memory test")
            + "...",
            flush=True,
        )
        fel("exec", "0x2000", timeout=120)
        # FEL EXEC acknowledges before the target has finished. Do not send a READ
        # while a long RAM test is still running; BROM cannot service it then.
        wait = 3 if args.dram_only else 60
        for tick in range(wait):
            if tick % 10 == 0:
                print(f"Waiting for CPU return: {tick}/{wait}s", flush=True)
            time.sleep(1)
        fel("read", "0x4800", "72", out / "mailbox.bin")
        result.update(dict(zip(KEYS, struct.unpack("<18I", (out / "mailbox.bin").read_bytes()))))
        print("Mailbox:", json.dumps({k: result[k] for k in KEYS}), flush=True)
        result["fel_after"] = fel("ver").decode().strip()
        if args.display and result.get("stage") == 4 and result.get("error") == 0:
            fel("read", "0x81c00000", "768000", out / "framebuffer.bin")
            colors = [0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0]
            row = b"".join(struct.pack("<H", c) * 100 for c in colors)
            framebuffer = (out / "framebuffer.bin").read_bytes()
            result["framebuffer_matches_bars"] = framebuffer == row * 480
            result["framebuffer_sha256"] = hashlib.sha256(framebuffer).hexdigest()
    finally:
        if backed_up:
            try:
                restored = True
                for a, n in ((0x2000, 0x3800),):
                    fel("write", hex(a), out / f"before_{a:x}.bin")
                    fel("read", hex(a), hex(n), out / f"after_{a:x}.bin")
                    restored = (
                        restored
                        and (out / f"before_{a:x}.bin").read_bytes()
                        == (out / f"after_{a:x}.bin").read_bytes()
                    )
            except Exception as e:
                restored = False
                result["restore_error"] = str(e)
        result["sram_restored"] = restored
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        (
            BUILD
            / (
                "display_hardware.json"
                if args.display
                else "dram_hardware.json"
                if args.dram_only
                else "memory_hardware.json"
            )
        ).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)
    assert restored, "SRAM restoration not verified"
    assert result.get("magic") == 0x4252494E and result.get("error") == 0, (
        "Diagnostic failed; inspect saved result"
    )
    assert result.get("stage") == (4 if args.display else 2 if args.dram_only else 3)
    if not args.dram_only:
        assert result.get("tested_bytes") == 0x2000000 and result.get("passes") == 2
    if args.display:
        assert result.get("framebuffer_matches_bars"), "Framebuffer readback differs"


if __name__ == "__main__":
    main()
