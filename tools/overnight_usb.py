"""Supervise receive-only UART and passive USB/viewer evidence for an overnight run."""

import argparse
import datetime
import json
import os
import plistlib
import signal
import subprocess
import sys
import time
from pathlib import Path
from urllib.request import urlopen

os.environ.setdefault("DYLD_FALLBACK_LIBRARY_PATH", "/opt/homebrew/lib:/usr/local/lib")

ROOT = Path(__file__).resolve().parents[1]


def passive_usb_inventory():
    result = subprocess.run(
        ["/usr/sbin/ioreg", "-a", "-l", "-p", "IOUSB"],
        capture_output=True, check=True, timeout=3,
    )
    devices = {}

    def visit(node):
        if isinstance(node, dict):
            vendor, product = node.get("idVendor"), node.get("idProduct")
            if (vendor, product) in ((0xDEAF, 0xBABE), (0x1F3A, 0xEFE8)):
                location = node.get("locationID", 0)
                address = node.get("USB Address")
                devices[(location, address, vendor, product)] = {
                    "vid": vendor, "pid": product,
                    "bus": location >> 24, "address": address,
                }
            for value in node.values():
                visit(value)
        elif isinstance(node, list):
            for value in node:
                visit(value)

    visit(plistlib.loads(result.stdout))
    return sorted(devices.values(), key=lambda device: (device["bus"], device["address"] or 0))


def write_json(path, value):
    pending = path.with_name(path.name + ".pending")
    with pending.open("w") as stream:
        stream.write(json.dumps(value, indent=2) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(pending, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--uart", required=True)
    parser.add_argument("--hours", type=float, default=12)
    parser.add_argument("--state-url", default="http://127.0.0.1:8766/state")
    parser.add_argument("--host-power-monitor", type=Path)
    args = parser.parse_args()
    if not 0 < args.hours <= 24:
        parser.error("Use a capture duration up to 24 hours")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    session_path = output / "session.json"
    if session_path.exists():
        session = json.loads(session_path.read_text())
    else:
        session = {
            "started_at": time.time(),
            "deadline": time.time() + args.hours * 3600,
            "uart": args.uart,
            "scope_reset_policy": "No resets, recovery or firmware writes by this recorder",
            "inventory_method": "IORegistry properties; no libusb enumeration",
            "host_power_monitor": str(args.host_power_monitor) if args.host_power_monitor else None,
        }
        write_json(session_path, session)
    if session["uart"] != args.uart:
        parser.error("Use a new output directory when changing the UART device")

    stopped = False

    def stop_capture(signum, frame):
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGTERM, stop_capture)
    signal.signal(signal.SIGINT, stop_capture)
    processes = {}
    uart_path = output / "uart.jsonl"
    cursor_path = output / "uart-cursor.json"
    cursor = json.loads(cursor_path.read_text()) if cursor_path.exists() else {}
    uart_offset = cursor.get("offset", 0)
    last_snapshot = cursor.get("snapshot")
    last_link = cursor.get("link")
    last_snapshot_at = cursor.get("snapshot_at")
    previous_usb = None
    previous_viewer = None
    previous_failure = None
    last_gap_report = 0
    with (output / "events.jsonl").open("a", buffering=1) as events:

        def record(kind, **values):
            entry = {
                "at": time.time(),
                "utc": datetime.datetime.now(datetime.UTC).isoformat(),
                "kind": kind,
                **values,
            }
            events.write(json.dumps(entry, separators=(",", ":")) + "\n")
            events.flush()
            os.fsync(events.fileno())
            print(json.dumps(entry), flush=True)

        def supervise(name, command, log_name):
            process = processes.get(name)
            if process is not None and process.poll() is None:
                return
            if process is not None:
                record("recorder_process_exited", name=name, returncode=process.returncode)
            with (output / log_name).open("a") as log:
                process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=log)
            processes[name] = process
            record("recorder_process_started", name=name, pid=process.pid)

        record("overnight_recorder_started", pid=os.getpid(), **session)
        try:
            while not stopped and time.time() < session["deadline"]:
                try:
                    remaining = max(1, session["deadline"] - time.time())
                    supervise(
                        "uart",
                        [
                            sys.executable, "-u", "-B", str(ROOT / "tools/diagnose_usb.py"),
                            "--uart-only", "--reconnect-uart", "--receiver-auto-baud",
                            "--uart", args.uart,
                            "--seconds", str(min(86400, remaining)), "--output", str(uart_path),
                        ],
                        "uart-console.log",
                    )
                    supervise(
                        "kernel",
                        [
                            "/usr/bin/log", "stream", "--style", "ndjson", "--level", "debug",
                            "--predicate",
                            '(eventMessage CONTAINS "DOS1102") OR '
                            '(eventMessage CONTAINS "02100000") OR '
                            '(eventMessage CONTAINS "AWUSBFEX")',
                        ],
                        "macos-usb.jsonl",
                    )
                    if uart_path.exists():
                        with uart_path.open() as uart:
                            uart.seek(uart_offset)
                            while True:
                                position = uart.tell()
                                line = uart.readline()
                                if not line or not line.endswith("\n"):
                                    uart.seek(position)
                                    break
                                try:
                                    row = json.loads(line)
                                except ValueError as error:
                                    record("uart_log_decode_error", offset=position, error=str(error))
                                    continue
                                kind = row.get("kind")
                                if kind in ("uart_open", "uart_error", "uart_reconnecting"):
                                    record("uart_connection_event", entry=row)
                                if kind == "uart_fault":
                                    record("device_usb_trace_frozen", entry=row)
                                    write_json(output / f"device-fault-{row['trace']['uptime_ms']}.json", row)
                                if kind == "uart_line":
                                    text = row.get("text", "")
                                    if text.startswith("SCOPE ") and any(
                                        marker in text for marker in ("BOOT0", "BOOT1", "Jump Exec")
                                    ):
                                        record("scope_boot_message", entry=row)
                                    elif "rst:" in text:
                                        record("receiver_boot_message", entry=row)
                                    elif text.startswith("UART AUTO"):
                                        record("receiver_baud_selection", entry=row)
                                if kind == "uart_snapshot":
                                    snapshot = row["snapshot"]
                                    if last_snapshot and snapshot["uptime_ms"] < last_snapshot["uptime_ms"]:
                                        record(
                                            "scope_uptime_reset", before=last_snapshot,
                                            after=snapshot, source_at=row["at"],
                                        )
                                    last_snapshot, last_snapshot_at = snapshot, row["at"]
                                if kind == "uart_link":
                                    last_link = row
                            uart_offset = uart.tell()
                        write_json(cursor_path, {
                            "offset": uart_offset, "snapshot": last_snapshot,
                            "snapshot_at": last_snapshot_at,
                            "link": last_link,
                        })

                    devices = passive_usb_inventory()
                    if devices != previous_usb:
                        record("usb_enumeration_changed", before=previous_usb, after=devices)
                        previous_usb = devices
                    if args.host_power_monitor:
                        power = subprocess.run(
                            [str(args.host_power_monitor.resolve()), "status"],
                            capture_output=True, text=True, timeout=3,
                        )
                        with (output / "host-power.jsonl").open("a") as power_log:
                            power_log.write(json.dumps({
                                "at": time.time(), "returncode": power.returncode,
                                "stdout": power.stdout.strip(), "stderr": power.stderr.strip(),
                            }) + "\n")
                    try:
                        with urlopen(args.state_url, timeout=2) as response:
                            state = json.load(response)
                        viewer = {key: state.get(key) for key in ("connected", "status", "session")}
                        if viewer != previous_viewer:
                            record("viewer_state_changed", state=viewer)
                            previous_viewer = viewer
                        failures = state.get("connection_errors") or []
                        if state.get("connected") and state.get("frame"):
                            write_json(output / "last-connected-state.json", state)
                        if failures and failures[-1].get("at") != previous_failure:
                            previous_failure = failures[-1]["at"]
                            path = output / f"failure-{int(previous_failure * 1e9)}.json"
                            write_json(path, state)
                            record("usb_failure_saved", path=str(path), error=failures[-1])
                    except Exception as error:
                        viewer = {"http_error": str(error)}
                        if viewer != previous_viewer:
                            record("viewer_unavailable", **viewer)
                            previous_viewer = viewer
                    if last_snapshot_at and time.time() - last_snapshot_at > 10:
                        if time.time() - last_gap_report > 60:
                            record("uart_snapshot_gap", last_snapshot_at=last_snapshot_at)
                            last_gap_report = time.time()
                    write_json(output / "status.json", {
                        "at": time.time(), "deadline": session["deadline"],
                        "uart_snapshot_at": last_snapshot_at, "uart_snapshot": last_snapshot,
                        "uart_link": last_link,
                        "viewer": previous_viewer, "usb_devices": previous_usb,
                        "processes": {name: process.pid for name, process in processes.items()},
                    })
                except Exception as error:
                    record("recorder_iteration_error", error=str(error), type=type(error).__name__)
                time.sleep(2)
        finally:
            for process in processes.values():
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            record("overnight_recorder_stopped", interrupted=stopped)


if __name__ == "__main__":
    main()
