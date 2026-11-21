#!/usr/bin/env python3
"""Initialize DRAM and load a verified development image through FEL."""

import argparse
import datetime
import hashlib
import json
import subprocess
import sys
import time

from device import ScopeControl
from toolchain import BUILD, ROOT, fel


def run_fel(*arguments, timeout=30):
    return subprocess.run(
        [fel(), *(str(argument) for argument in arguments)],
        capture_output=True,
        check=True,
        timeout=timeout,
    ).stdout


def wait_for_fel():
    deadline = time.monotonic() + 8
    while True:
        try:
            return run_fel("ver").decode().strip()
        except subprocess.CalledProcessError:
            if time.monotonic() >= deadline:
                raise RuntimeError("Put the scope in FEL before loading") from None
            time.sleep(0.2)


def write_verified(address, source, readback):
    data = source.read_bytes()
    run_fel("write", hex(address), source)
    run_fel("read", hex(address), len(data), readback)
    if readback.read_bytes() != data:
        raise RuntimeError(f"RAM upload verification failed at {address:#x}")


def stop_display_dma():
    gates = int(run_fel("readl", "0x01c20064").decode().strip(), 16)
    if gates & 0x1000:
        run_fel("writel", "0x01e60800", "0")
    if gates & 0x10:
        value = int(run_fel("readl", "0x01c0c000").decode().strip(), 16)
        run_fel("writel", "0x01c0c000", hex(value & ~0x80000000))
    if gates & 0x1010:
        time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cold-image", action="store_true")
    parser.add_argument("--probe-startup", action="store_true")
    parser.add_argument("--usb-diagnostics", action="store_true")
    args = parser.parse_args()
    if args.usb_diagnostics and args.cold_image:
        parser.error("USB diagnostics uses a RAM image")
    image_dir = BUILD / (
        "diagnostic-ram" if args.usb_diagnostics else "cold" if args.cold_image else "ram"
    )
    manifest = json.loads((image_dir / "manifest.json").read_text())
    image = image_dir / "scope.bin"
    data = image.read_bytes()
    if len(data) != manifest["size"] or hashlib.sha256(data).hexdigest() != manifest["sha256"]:
        raise RuntimeError("Application differs from its build manifest")
    if manifest["entry"] != 0x80080000 or len(data) > 0x50000:
        raise RuntimeError("Unsupported application load range")
    if not 0x800D0000 <= manifest["bss_start"] <= manifest["bss_end"] <= 0x80100000:
        raise RuntimeError("Unsupported BSS range")
    print(wait_for_fel(), flush=True)
    stop_display_dma()
    subprocess.run(
        [sys.executable, "-B", str(ROOT / "tools/run_dram.py"), "--dram-only"], check=True
    )
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    run = image_dir / "runs" / timestamp
    run.mkdir(parents=True)
    write_verified(0x80080000, image, run / "application.bin")
    zero = run / "bss-zero.bin"
    zero.write_bytes(bytes(manifest["bss_end"] - manifest["bss_start"]))
    run_fel("write", hex(manifest["bss_start"]), zero)
    markers = [int(args.probe_startup), 0, 0, 0x4155544F, 1]
    for index, value in enumerate(markers):
        source = run / f"marker-{index}.bin"
        source.write_bytes(value.to_bytes(4, "little"))
        run_fel("write", hex(0x800A0100 + index * 4), source)
    print("Upload verified; starting the application", flush=True)
    try:
        run_fel("exec", "0x80080000", timeout=10)
    except subprocess.TimeoutExpired:
        if args.probe_startup:
            raise
    if args.probe_startup:
        print("Startup probe returned; scope remains in FEL")
        return
    deadline = time.monotonic() + 15
    while True:
        try:
            with ScopeControl() as device:
                result = {
                    "ping": device.request("PING"),
                    "usb": device.request("USB STATE"),
                    "image_sha256": manifest["sha256"],
                    "flash_written": False,
                }
                (run / "loaded.json").write_text(json.dumps(result, indent=2) + "\n")
                print(json.dumps(result, indent=2), flush=True)
                return
        except (RuntimeError, OSError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)


if __name__ == "__main__":
    main()
