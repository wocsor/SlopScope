"""Capture bounded USB probes and optional UART snapshots without automatic recovery."""

import argparse
import datetime
import fcntl
import json
import os
import struct
import tempfile
import threading
import time
from pathlib import Path

from device import PRODUCT_ID, VENDOR_ID

import usb.core
import usb.util

DIAGNOSTIC_FIELDS = [
    "magic",
    "version",
    "uptime_ms",
    "loops",
    "sequence",
    "capture_state",
    "boot_stage",
    "power",
    "configured",
    "mps",
    "ep0",
    "tx",
    "rx",
    "rx_count",
    "reply_left",
    "packet_left",
    "line",
    "recent_poll_us",
    "max_poll_us",
    "bus_resets",
    "setups",
    "commands",
    "errors",
    "suspends",
    "resumes",
    "disconnects",
    "devctl",
    "phyctl",
    "last_bus_flags",
    "last_setup",
    "last_command_prefix",
]


UART_DIAGNOSTIC_FIELDS = [
    "magic",
    "version",
    "ready",
    "clock_hz",
    "divisor",
    "tx_bytes",
    "queue_read",
    "queue_write",
    "drops",
    "lsr",
    "lcr",
    "usr",
    "mcr",
    "gate",
    "reset",
    "pf_mux",
    "pf_data",
    "pf_pull",
    "cpu_pll",
    "periph_pll",
    "cpu_cfg",
    "bus_cfg",
    "halt",
    "fifo_level",
]


PACKET_DIAGNOSTIC_FIELDS = [
    "magic", "version", "queued", "consumed", "queued_ms", "consumed_ms",
    "offset", "reply_bytes", "packet_bytes", "crc32", "prefix0", "prefix1",
    "prefix2", "prefix3", "tx_max_packet", "rx_max_packet", "tx_fifo_size",
    "rx_fifo_size", "tx_fifo_address", "rx_fifo_address", "tx_status_before_ready",
    "rx_status", "endpoint_index", "sram_control",
]

EVENT_DIAGNOSTIC_FIELDS = [
    "magic", "number", "ticks_24mhz", "kind", "argument0", "argument1", "argument2",
    "ep0", "tx", "rx", "rx_count", "power", "devctl", "sequence",
    "last_command_prefix", "packet_number",
]
FAULT_DIAGNOSTIC_FIELDS = [
    "magic", "version", "reason", "uptime_ms", "event_count", "last_event",
    "queued", "consumed", "pending_ms", "sequence", "last_bus_flags",
    "last_setup", "last_command_prefix", "watchdog_config", "watchdog_mode",
    "command_bytes",
]
HEALTH_DIAGNOSTIC_FIELDS = [
    "magic", "version", "uptime_ms", "cpu_pll", "periph_pll", "cpu_cfg", "bus_cfg",
    "watchdog_config", "watchdog_mode", "uart_clock_hz", "uart_divisor", "uart_tx_bytes",
]
EVENT_NAMES = {
    1: "RX_PACKET", 2: "COMMAND", 3: "FIFO_WRITTEN", 4: "TX_ARMED",
    5: "TX_READY_CLEARED", 6: "BUS_FLAGS", 7: "SETUP", 8: "ABORT", 9: "TRIGGER",
}
LINK_DIAGNOSTIC_FIELDS = [
    "magic", "version", "uptime_ms", "frame_number", "sof_observations",
    "last_sof_age_ms", "frame_observations", "last_frame_change_age_ms", "power", "devctl",
]
UART_TRACE_LAYOUTS = {
    "LINK": (0x314B4E4C, LINK_DIAGNOSTIC_FIELDS, "uart_link"),
    "FAULT": (0x31544C46, FAULT_DIAGNOSTIC_FIELDS, "uart_fault"),
    "FROZEN": (0x31544B50, PACKET_DIAGNOSTIC_FIELDS, "uart_frozen_packet"),
    "EVT": (0x31545645, EVENT_DIAGNOSTIC_FIELDS, "uart_usb_event"),
    "HEALTH": (0x31544C48, HEALTH_DIAGNOSTIC_FIELDS, "uart_health"),
}


def capture_trace_line(text, capture):
    parts = text.split()
    if len(parts) < 4 or parts[2] not in UART_TRACE_LAYOUTS:
        return
    magic, names, kind = UART_TRACE_LAYOUTS[parts[2]]
    try:
        values = [int(value, 16) for value in parts[3:]]
        if len(values) != len(names) + 1 or values[0] != magic:
            raise ValueError("Unexpected UART trace layout")
        if names[1] == "version" and values[1] != 1:
            raise ValueError("Unsupported UART trace version")
        trace = dict(zip(names, values))
        if kind == "uart_usb_event":
            trace["kind_name"] = EVENT_NAMES.get(trace["kind"], "UNKNOWN")
        capture.emit(kind, trace=trace, dropped=values[-1])
    except (ValueError, IndexError) as error:
        capture.emit("uart_decode_error", **error_details(error))


def decode_packet_trace(data):
    if len(data) != 96:
        raise ValueError("Unexpected packet trace length")
    values = struct.unpack("<24I", data)
    if values[:2] != (0x31544B50, 1):
        raise ValueError("Unsupported packet trace")
    return dict(zip(PACKET_DIAGNOSTIC_FIELDS, values))


def decode_uart_status(data):
    if len(data) != 96:
        raise ValueError("Unexpected UART snapshot length")
    values = struct.unpack("<24I", data)
    if values[:2] != (0x54524155, 1):
        raise ValueError("Unsupported UART snapshot")
    return dict(zip(UART_DIAGNOSTIC_FIELDS, values))


def decode_snapshot(data):
    if len(data) != len(DIAGNOSTIC_FIELDS) * 4:
        raise ValueError("Unexpected diagnostic snapshot length")
    values = struct.unpack("<31I", data)
    if values[:2] != (0x44425355, 1):
        raise ValueError("Unsupported diagnostic snapshot")
    return dict(zip(DIAGNOSTIC_FIELDS, values))


class Capture:
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.stream = path.open("a")
        self.lock = threading.Lock()
        self.started = time.monotonic()

    def emit(self, kind, **values):
        row = {
            "at": time.time(),
            "elapsed_s": round(time.monotonic() - self.started, 6),
            "kind": kind,
            **values,
        }
        line = json.dumps(row, separators=(",", ":"))
        with self.lock:
            self.stream.write(line + "\n")
            self.stream.flush()
        print(line, flush=True)


def error_details(error):
    return {
        "error": str(error),
        "type": type(error).__name__,
        "backend": getattr(error, "backend_error_code", None),
        "errno": getattr(error, "errno", None),
    }


def capture_uart(port, capture, stopped, reconnect=False, receiver_auto_baud=False):
    while not stopped.is_set():
        capture_uart_connection(port, capture, stopped, receiver_auto_baud)
        if not reconnect or stopped.wait(1):
            return
        capture.emit("uart_reconnecting", port=port)


def capture_uart_connection(port, capture, stopped, receiver_auto_baud=False):
    import serial

    try:
        with serial.Serial(port=None, baudrate=115200, timeout=0.1) as receiver:
            receiver.dtr = receiver.rts = True
            receiver.port = port
            receiver.open()
            capture.emit("uart_open", port=port, baud=115200)
            if receiver_auto_baud:
                receiver.write(b"UART AUTO ON\n")
                receiver.flush()
                capture.emit("receiver_auto_baud_enabled")
            line = bytearray()
            while not stopped.is_set():
                data = receiver.read(4096)
                if not data:
                    continue
                capture.emit("uart_bytes", count=len(data), hex=data.hex())
                line.extend(data)
                while b"\n" in line:
                    text, _, remainder = line.partition(b"\n")
                    line = bytearray(remainder)
                    text = text.decode("ascii", errors="backslashreplace").strip()
                    capture.emit("uart_line", text=text)
                    if text.startswith("SCOPE DBG USB "):
                        try:
                            values = [int(value, 16) for value in text.split()[3:]]
                            snapshot = decode_snapshot(struct.pack("<31I", *values[:31]))
                            capture.emit("uart_snapshot", snapshot=snapshot, dropped=values[31])
                        except (ValueError, struct.error, IndexError) as error:
                            capture.emit("uart_decode_error", **error_details(error))
                    elif text.startswith("SCOPE DBG TX "):
                        try:
                            values = [int(value, 16) for value in text.split()[3:]]
                            trace = decode_packet_trace(struct.pack("<24I", *values[:24]))
                            capture.emit("uart_packet_trace", trace=trace, dropped=values[24])
                        except (ValueError, struct.error, IndexError) as error:
                            capture.emit("uart_decode_error", **error_details(error))
                    elif text.startswith("SCOPE DBG CMD "):
                        capture.emit("uart_frozen_command", command=text[len("SCOPE DBG CMD "):])
                    elif text.startswith("SCOPE DBG "):
                        capture_trace_line(text, capture)
                if len(line) > 8192:
                    capture.emit("uart_oversized_line", bytes=len(line))
                    line.clear()
    except Exception as error:
        capture.emit("uart_error", **error_details(error))


def control_probe(device, capture, vendor_snapshot):
    probes = [
        ("descriptor", (0x80, 6, 0x100, 0, 18)),
        ("configuration", (0x80, 8, 0, 0, 1)),
        ("in_halt", (0x82, 0, 0, 0x81, 2)),
        ("out_halt", (0x82, 0, 0, 1, 2)),
    ]
    if vendor_snapshot:
        probes.append(("diagnostic_snapshot", (0xC0, 0x5A, 0, 0, 124)))
        probes.append(("uart_status", (0xC0, 0x5A, 1, 0, 96)))
        probes.append(("packet_trace", (0xC0, 0x5A, 3, 0, 96)))
    for name, arguments in probes:
        started = time.monotonic()
        try:
            data = bytes(device.ctrl_transfer(*arguments, timeout=750))
            extra = {"snapshot": decode_snapshot(data)} if name == "diagnostic_snapshot" else {}
            if name == "uart_status":
                extra = {"uart_status": decode_uart_status(data)}
            if name == "packet_trace":
                extra = {"packet_trace": decode_packet_trace(data)}
            capture.emit(
                "control",
                name=name,
                ms=round((time.monotonic() - started) * 1000, 3),
                data=data.hex(),
                **extra,
            )
        except (usb.core.USBError, ValueError) as error:
            capture.emit("control_error", name=name, **error_details(error))


def bulk_reply(device, command, capture, marker=None):
    started = time.monotonic()
    capture.emit("bulk_write", command=command)
    device.write(1, command.encode("ascii") + b"\n", timeout=1000)
    reply = bytearray()
    while len(reply) < 32768:
        chunk = bytes(device.read(0x81, 512, timeout=1000))
        reply.extend(chunk)
        if (marker and marker + b"\n" in reply) or (not marker and b"\n" in reply):
            capture.emit(
                "bulk_reply",
                command=command,
                bytes=len(reply),
                ms=round((time.monotonic() - started) * 1000, 3),
                text=reply.decode("ascii", errors="backslashreplace"),
            )
            return
        if time.monotonic() - started > 3:
            break
    raise TimeoutError("Bulk reply did not complete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--uart", help="ESP32 receive-only serial port")
    parser.add_argument("--reconnect-uart", action="store_true", help="Reopen UART after disconnects")
    parser.add_argument(
        "--receiver-auto-baud", action="store_true",
        help="Enable ESP32 RX baud detection through its PC console; ESP32 TX remains unused",
    )
    parser.add_argument(
        "--vendor-snapshot", action="store_true", help="Diagnostic RAM firmware only"
    )
    parser.add_argument(
        "--control-only", action="store_true", help="Leave bulk endpoints untouched"
    )
    parser.add_argument(
        "--uart-only", action="store_true", help="Arm UART without taking USB ownership"
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.uart_only and not args.uart:
        parser.error("--uart-only requires --uart")
    if args.reconnect_uart and not args.uart:
        parser.error("--reconnect-uart requires --uart")
    if args.receiver_auto_baud and not args.uart:
        parser.error("--receiver-auto-baud requires --uart")
    maximum_seconds = 86400 if args.uart_only else 3600
    if args.seconds <= 0 or args.seconds > maximum_seconds:
        parser.error(f"Use a bounded duration between zero and {maximum_seconds} seconds")
    timestamp = datetime.datetime.now(datetime.UTC).strftime("%Y%m%dT%H%M%S%fZ")
    path = (
        args.output
        or Path(__file__).resolve().parents[1] / "build/usb-diagnostics" / f"{timestamp}.jsonl"
    )
    capture = Capture(path)
    lock_path = Path(tempfile.gettempdir()) / f"hscope-deaf-babe-{os.getuid()}.lock"
    lock_fd = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    stopped = threading.Event()
    receiver = None
    try:
        if not args.uart_only:
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.uart:
            receiver = threading.Thread(
                target=capture_uart,
                args=(args.uart, capture, stopped, args.reconnect_uart, args.receiver_auto_baud),
            )
            receiver.start()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            if args.uart_only:
                if receiver is None or not receiver.is_alive():
                    break
                stopped.wait(min(0.25, max(0, deadline - time.monotonic())))
                continue
            device = None
            try:
                device = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)
                if device is None:
                    fel = usb.core.find(idVendor=0x1F3A, idProduct=0xEFE8)
                    capture.emit("enumeration", cfw=False, fel=fel is not None)
                else:
                    capture.emit("enumeration", cfw=True, address=device.address, bus=device.bus)
                    control_probe(device, capture, args.vendor_snapshot)
                    if not args.control_only:
                        usb.util.claim_interface(device, 0)
                        marker = f"DIAG_SYNC_{time.time_ns()}".encode("ascii")
                        try:
                            bulk_reply(device, "ECHO " + marker.decode("ascii"), capture, marker)
                            for command in (
                                "PING",
                                "USB STATE",
                                "ACQUISITION STATUS",
                                "DISPLAY STATUS",
                            ):
                                bulk_reply(device, command, capture)
                        except (usb.core.USBError, TimeoutError) as error:
                            capture.emit("bulk_error", **error_details(error))
                            control_probe(device, capture, args.vendor_snapshot)
                            capture.emit(
                                "bulk_probing_stopped",
                                reason="Do not reset or replay uncertain commands",
                            )
                            args.control_only = True
            except (usb.core.USBError, ValueError) as error:
                capture.emit("device_error", **error_details(error))
            finally:
                if device is not None:
                    usb.util.dispose_resources(device)
            stopped.wait(min(1, max(0, deadline - time.monotonic())))
    except BlockingIOError:
        capture.emit(
            "exclusive_access_required", message="Disconnect viewer clients before probing"
        )
    finally:
        stopped.set()
        if receiver is not None:
            receiver.join(timeout=2)
        os.close(lock_fd)
        capture.stream.close()


if __name__ == "__main__":
    main()
