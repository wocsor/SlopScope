"""USB control and capture transport for SlopScope firmware (DEAF:BABE)."""

from __future__ import annotations

import base64
from contextlib import closing
import fcntl
import os
import re
import struct
import time
import zlib

os.environ.setdefault("DYLD_FALLBACK_LIBRARY_PATH", "/opt/homebrew/lib:/usr/local/lib")
import usb.core
import usb.util
from .discovery import select_scope, scope_lock_path

SCALES_MV = (1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000)
RATES = (
    0,
    500000000,
    250000000,
    100000000,
    50000000,
    25000000,
    10000000,
    5000000,
    2500000,
    1000000,
    500000,
    250000,
    100000,
    50000,
    25000,
    10000,
    5000,
    2500,
    1000,
    500,
    250,
    100,
    50,
    25,
    10,
    5,
    2.5,
    1,
    0.5,
    0.25,
    0.1,
    0.05,
)
HEADER_KEYS = (
    "magic",
    "version",
    "header_bytes",
    "payload_bytes",
    "sequence",
    "nominal_hz",
    "ticks_24mhz",
    "csi_flags",
    "ch1_range",
    "ch2_range",
    "ch1_coupling",
    "ch2_coupling",
    "normal_adc",
    "offsets_packed",
    "rate_index",
    "crc32",
)


def fields(reply):
    return {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9A-F]{8})(?=\s|$)", reply)}


def calibration_identity(cal):
    return zlib.crc32(
        struct.pack(
            "<iIIiIII",
            *(cal[k] for k in ("zero", "gain", "dac", "slope", "valid", "adc_scale", "adc_trim")),
        )
    )


def frame_calibration_matches(metadata, settings):
    if "calibration_ids" not in metadata:
        return True
    table = settings.get("calibration")
    return bool(table) and all(
        "adc_scale" in table[ch][metadata[f"ch{ch + 1}_range"]]
        and calibration_identity(table[ch][metadata[f"ch{ch + 1}_range"]])
        == metadata["calibration_ids"][ch]
        for ch in range(2)
    )


def decode_frame(data):
    if len(data) not in (4064, 4096, 4092, 4108, 4116, 4124):
        raise ValueError("invalid waveform length")
    words = struct.unpack("<16I", data[:64])
    version, header = words[1:3]
    if (
        words[0] != 0x31435348
        or (version, header, words[3])
        not in (
            (1, 64, 4000),
            (2, 96, 4000),
            (3, 96, 3996),
            (4, 100, 3996),
            (5, 112, 3996),
            (6, 120, 3996),
            (7, 128, 3996),
        )
        or len(data) != header + words[3]
    ):
        raise ValueError("unsupported frame header")
    payload = data[header:]
    if zlib.crc32(payload) != words[15]:
        raise ValueError("waveform CRC mismatch")
    if not 1 <= words[14] <= 31 or not words[5]:
        raise ValueError("invalid sample rate metadata")
    if words[8] >= 14 or words[9] >= 14 or words[10] > 1 or words[11] > 1:
        raise ValueError("invalid channel metadata")
    metadata = dict(zip(HEADER_KEYS, words))
    if version >= 2:
        extra = struct.unpack("<8I", data[64:96])
        keys = (
            "acquire_mode",
            "device_averages",
            "bandwidth_bits",
            "trigger_family",
            "horizontal_ns",
            "forced",
            "trigger_mode",
            "capture_epoch",
        )
        metadata.update(zip(keys, extra))
        if (
            metadata["acquire_mode"] > 2
            or metadata["trigger_family"] > 1
            or metadata["trigger_mode"] > 7
        ):
            raise ValueError("invalid frame control metadata")
        if metadata["horizontal_ns"] & 0x80000000:
            metadata["horizontal_ns"] -= 0x100000000
    alignment = 2 if version >= 3 else 0
    if version >= 4:
        flags = struct.unpack("<I", data[96:100])[0]
        metadata["pulse_sampled"] = bool(flags & 1)
        if flags & 0x80:
            alignment = (flags >> 4) & 7
            if alignment not in (1, 2):
                raise ValueError("Invalid channel alignment")
    if version >= 5:
        flags, start, count, snapshot_tick = struct.unpack("<4I", data[96:112])
        if start > 1998 or count > 1998 - start:
            raise ValueError("Invalid partial frame bounds")
        metadata.update(
            pulse_sampled=bool(flags & 1),
            roll=bool(flags & 2),
            filling=bool(flags & 4),
            preview=bool(flags & 8),
            valid_from=start,
            valid_count=count,
            snapshot_ticks=snapshot_tick,
        )
    if version >= 6:
        numerator, denominator = struct.unpack("<2I", data[112:120])
        if not numerator or not denominator:
            raise ValueError("Invalid rational sample rate")
        metadata.update(
            rate_numerator=numerator,
            rate_denominator=denominator,
            nominal_hz=numerator / denominator,
        )
    if version >= 7:
        metadata["calibration_ids"] = list(struct.unpack("<2I", data[120:128]))
    count = words[3] // 2
    metadata["samples_per_channel"] = count
    metadata["channel_alignment_samples"] = alignment
    channels = [list(struct.unpack(f"{count}b", payload[off : off + count])) for off in (0, count)]
    return metadata, channels, payload


class SlopScope:
    def __init__(
        self,
        *,
        serial_number=None,
        reset_usb=False,
        recover_halts=True,
        capture_failed_reads=False,
        frame_read_size=4096,
        bounded_frames=True,
    ):
        if frame_read_size not in (512, 1024, 2048, 4096):
            raise ValueError("Frame read size must be 512, 1024, 2048 or 4096 bytes")
        self.frame_read_size = frame_read_size
        self.bounded_frames = bounded_frames
        selected_device, self.serial_number = select_scope(serial_number)
        # Claim before set_configuration: that control request can reset another
        # client's transfer even on hosts which allow multiple open USB handles.
        self._lock_fd = os.open(
            scope_lock_path(self.serial_number),
            os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW,
            0o600,
        )
        self.device = None
        self.recover_halts = recover_halts
        self.capture_failed_reads = capture_failed_reads
        self._session_supported = False
        self.last_operation = {"command": "CONNECT", "phase": "find", "started_at": time.time()}
        try:
            try:
                fcntl.flock(self._lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise RuntimeError("Scope USB is already in use by another SlopScope USB client") from None
            self.device = selected_device
            if reset_usb:
                # Used only between failed sessions, under the exclusive lock.
                # Reset the USB connection, not the CPU, and never replay the
                # command which failed: its delivery may be uncertain.
                self.last_operation["phase"] = "bus_reset"
                self.device.reset()
                usb.util.dispose_resources(self.device)
                self.device = None
                deadline = time.monotonic() + 3
                while self.device is None:
                    try:
                        self.device, _ = select_scope(self.serial_number)
                        break
                    except RuntimeError:
                        self.device = None
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Waiting for scope after USB reset")
                    time.sleep(0.05)
            self.last_operation["phase"] = "set_configuration"
            self.device.default_timeout = 3000
            try:
                active_configuration = self.device.get_active_configuration()
            except ValueError:
                active_configuration = None
            if active_configuration is None or active_configuration.bConfigurationValue != 1:
                self.device.set_configuration(1)
            self.last_operation["phase"] = "claim"
            usb.util.claim_interface(self.device, 0)
            # Synchronize under the claimed interface without resetting another client.
            self.pending = b""
            self._synchronize()
            if not self.request("INFO").startswith("SlopScope "):
                raise RuntimeError("This USB device is not running SlopScope firmware")
            try:
                self.request("SESSION OPEN")
                self._session_supported = True
            except RuntimeError as error:
                if str(error) != "ERR unknown command":
                    raise
        except Exception as exc:
            if isinstance(exc, usb.core.USBError):
                exc.scope_operation = dict(self.last_operation)
            if self.device is not None:
                usb.util.dispose_resources(self.device)
                from .usb_capture import close_read_buffer

                close_read_buffer(self.device)
            os.close(self._lock_fd)
            self._lock_fd = None
            raise

    @staticmethod
    def _is_halt(error):
        # Synchronous libusb PIPE is an endpoint halt. Generic EIO (-1) does
        # not establish a halt; clearing it hid the original failure on macOS.
        return getattr(error, "backend_error_code", None) == -9 or error.errno == 32

    @staticmethod
    def _is_io(error):
        return isinstance(error, usb.core.USBError) and (
            getattr(error, "backend_error_code", None) == -1 or error.errno == 5
        )

    def _read_bulk(self, size):
        if self.capture_failed_reads:
            from .usb_capture import read_with_partial

            return read_with_partial(self.device, size)
        return bytes(self.device.read(0x81, size))

    def _synchronize(self):
        # set_configuration/CLEAR_HALT already abandons the old transaction.
        # Send an explicit marker instead of timing out an empty IN transfer.
        marker = f"HSCOPE_SYNC_{time.time_ns()}".encode()
        for attempt in range(2):
            try:
                self.last_operation = {
                    "command": "SYNCHRONIZE",
                    "phase": "write",
                    "started_at": time.time(),
                }
                self.device.write(1, b"ECHO " + marker + b"\n")
                data = b""
                until = time.monotonic() + 3
                self.last_operation["phase"] = "read"
                while time.monotonic() < until:
                    data += self._read_bulk(512)
                    if marker + b"\n" in data:
                        self.last_operation["phase"] = "complete"
                        return
                    if len(data) > 32768:
                        raise RuntimeError("Could not synchronize scope replies")
                raise TimeoutError("Scope synchronization timed out")
            except usb.core.USBError as exc:
                if not self.recover_halts or not self._is_halt(exc) or attempt:
                    raise
                self.device.clear_halt(0x01)
                self.device.clear_halt(0x81)
        raise TimeoutError("Scope synchronization timed out")

    def request(self, command):
        data = command.encode("ascii")
        if not data or len(data) > 128 or b"\n" in data or b"\r" in data:
            raise ValueError("invalid command")
        self.last_operation = {"command": command, "phase": "write", "started_at": time.time()}
        self.device.write(1, data + b"\n")
        deadline = time.monotonic() + 5
        self.last_operation["phase"] = "read"
        while b"\n" not in self.pending:
            if time.monotonic() > deadline:
                raise TimeoutError("USB reply timeout")
            self.pending += self._read_bulk(512)
            self.last_operation["received"] = len(self.pending)
            if len(self.pending) > 4096:
                raise ValueError("oversized ASCII reply")
        reply, self.pending = self.pending.split(b"\n", 1)
        reply = reply.decode("ascii")
        if reply.startswith("ERR "):
            raise RuntimeError(reply)
        self.last_operation["phase"] = "complete"
        return reply

    def frame(self, *, cancelled=None):
        if self.bounded_frames:
            from .frame_transfer import FrameSkipped

            data = bytearray()
            with closing(self.frame_chunks()) as chunks:
                for offset, payload in chunks:
                    if cancelled is not None and cancelled():
                        raise FrameSkipped("Frame skipped for a pending control")
                    if offset != len(data):
                        raise ValueError("Non-contiguous frame chunks")
                    data.extend(payload)
            return decode_frame(bytes(data))
        if self.pending:
            raise RuntimeError("unexpected pending USB data")
        if not hasattr(self, "_frame_version"):
            try:
                self._frame_version = fields(self.request("ACQUIRE STATUS")).get("frame", 1)
            except RuntimeError:
                self._frame_version = 1
        version = getattr(self, "_frame_version", 1)
        expected = (
            4124
            if version >= 7
            else 4116
            if version >= 6
            else 4108
            if version >= 5
            else 4096
            if version >= 4
            else 4092
            if version == 3
            else 4096
            if version == 2
            else 4064
        )
        command = (
            b"FRAME7\n"
            if version >= 7
            else b"FRAME6\n"
            if version >= 6
            else b"FRAME5\n"
            if version >= 5
            else b"FRAME4\n"
            if version >= 4
            else b"FRAME3\n"
            if version == 3
            else b"FRAME2\n"
            if version == 2
            else b"FRAME\n"
        )
        for attempt in range(2):
            self.last_operation = {
                "command": command.decode().strip(),
                "phase": "write",
                "expected": expected,
                "received": 0,
                "started_at": time.time(),
            }
            self.device.write(1, command)
            data = bytearray()
            deadline = time.monotonic() + 5
            self.last_operation["phase"] = "read"
            try:
                while len(data) < expected:
                    if time.monotonic() > deadline:
                        raise TimeoutError("USB frame timeout")
                    data.extend(self._read_bulk(self.frame_read_size))
                    self.last_operation["received"] = len(data)
                    if data[:4] == b"ERR ":
                        raise RuntimeError(data.decode("ascii").strip())
                    if len(data) > expected:
                        raise ValueError("oversized binary frame")
                result = decode_frame(bytes(data))
                self.last_operation["phase"] = "complete"
                return result
            except usb.core.USBError as exc:
                if hasattr(exc, "scope_read_details"):
                    exc.scope_read_details["frame_offset"] = len(data)
                exc.scope_payload = bytes(data) + getattr(exc, "scope_partial_payload", b"")
                self.last_operation["received"] = len(exc.scope_payload)
                exc.scope_operation = dict(self.last_operation)
                if not self.recover_halts or not self._is_halt(exc) or attempt:
                    raise
                # Discard a partial frame, recover the halted endpoints and
                # realign the text protocol before asking for a fresh packet.
                self.pending = b""
                self.device.clear_halt(0x01)
                self.device.clear_halt(0x81)
                self._synchronize()
            except (ValueError, TimeoutError, RuntimeError) as exc:
                exc.scope_payload = bytes(data)
                exc.scope_operation = dict(self.last_operation)
                raise

    def record(self, channel=1, depth=10000, force=False):
        steps = self.record_steps(channel, depth, force)
        while True:
            try:
                next(steps)
                time.sleep(0.025)
            except StopIteration as done:
                return done.value

    def record_steps(self, channel=1, depth=10000, force=False):
        if channel not in (1, 2, "both") or depth not in (1000, 10000):
            raise ValueError("Invalid record request")
        self.stop()
        config = fields(self.request("CAPTURE CONFIG"))
        rate = config.get("nominal_hz", 1) / config.get("rate_denominator", 1)
        self.request(f"RECORD DEPTH {depth}")
        self.request("RECORD START BOTH" if channel == "both" else f"RECORD START CH{channel}")
        started = time.monotonic()
        deadline = started + max(10, 2 * depth / rate + 5)
        completed = False
        try:
            if force:
                self.request("TRIGGER FORCE")
            while True:
                status = fields(self.request("RECORD STATUS"))
                if status.get("ready"):
                    break
                if not status.get("busy"):
                    raise RuntimeError(f"Record capture failed: {status}")
                if time.monotonic() > deadline:
                    raise TimeoutError("Record trigger timed out")
                yield {
                    "running": True,
                    "elapsed_s": time.monotonic() - started,
                    "duration_s": depth / rate,
                    "state": status.get("state"),
                    "channel": channel,
                    "depth": depth,
                    "stage": "Filling sample history"
                    if status.get("filling")
                    else "Reading acquired record"
                    if status.get("reading")
                    else "Waiting for trigger / buffer",
                    "remaining_ms": status.get("remaining_ms", 0),
                }
            records = [
                self._read_record(ch, "channels" in status)
                for ch in ((1, 2) if channel == "both" else (channel,))
            ]
            for record in records:
                meta = record["metadata"]
                meta["forced"] = bool(status.get("forced"))
                meta["trigger_qualified"] = not meta["forced"] and (
                    meta["trigger_mode"] == 0 or meta.get("pulse_sampled", False)
                )
            if channel == "both":
                first, second = (r["metadata"] for r in records)
                if any(
                    first[k] != second[k]
                    for k in ("sequence", "ticks_24mhz", "nominal_hz", "record_depth")
                ):
                    raise ValueError("Paired record acquisition mismatch")
            completed = True
            return (
                {"format": "slopscope-record-pair", "version": 1, "records": records}
                if channel == "both"
                else records[0]
            )
        finally:
            if not completed:
                self.request("RECORD CANCEL")

    def _read_record(self, channel, paired=False):
        if self.pending:
            raise RuntimeError("Unexpected USB data before record")
        self.device.write(
            1, (f"RECORD FRAME CH{channel}\n" if paired else "RECORD FRAME\n").encode()
        )
        data = bytearray()
        expected = 64
        while len(data) < expected:
            data.extend(self.device.read(0x81, 16384))
            if data[:4] == b"ERR ":
                raise RuntimeError(data.decode().strip())
            if len(data) >= 64:
                words = struct.unpack("<16I", data[:64])
                if (
                    words[0] != 0x31525348
                    or (words[1], words[2]) not in ((1, 64), (2, 96), (3, 104), (4, 108))
                    or words[3] not in (1000, 10000)
                ):
                    raise ValueError("Invalid record header")
                expected = words[2] + words[3]
            if len(data) > 10108:
                raise ValueError("Oversized record")
        if len(data) != expected or zlib.crc32(data[words[2] :]) != words[15]:
            raise ValueError("Record CRC/length mismatch")
        keys = (
            "magic",
            "version",
            "header_bytes",
            "payload_bytes",
            "sequence",
            "nominal_hz",
            "ticks_24mhz",
            "csi_flags",
            "channel",
            "range",
            "coupling",
            "offset_dac",
            "record_depth",
            "trigger_mode",
            "horizontal_ns",
            "crc32",
        )
        metadata = dict(zip(keys, words))
        metadata["horizontal_ns"] = struct.unpack("<i", data[56:60])[0]
        if words[1] >= 2:
            extra = struct.unpack("<8I", data[64:96])
            metadata.update(
                zip(
                    (
                        "rate_numerator",
                        "rate_denominator",
                        "forced",
                        "capture_epoch",
                        "programmed_pretrigger",
                        "programmed_posttrigger",
                        "armed_ticks",
                        "completed_ticks",
                    ),
                    extra,
                )
            )
            if not extra[0] or not extra[1]:
                raise ValueError("Invalid record sampling rate")
            metadata["nominal_hz"] = extra[0] / extra[1]
        if words[1] >= 3:
            sampled, alignment = struct.unpack("<2I", data[96:104])
            metadata.update(pulse_sampled=bool(sampled), alignment_drop_leading_samples=alignment)
        if words[1] >= 4:
            metadata["calibration_id"] = struct.unpack("<I", data[104:108])[0]
        cal = self.voltage_calibration()[channel - 1][words[9]]
        match = (
            "calibration_id" not in metadata
            or "adc_scale" in cal
            and calibration_identity(cal) == metadata["calibration_id"]
        )
        metadata.update(calibration=cal if match else None, calibration_matches=bool(match))
        metadata["probe"] = fields(self.request(f"CH{channel} VIEW")).get("probe", 1)
        metadata["trigger_qualified"] = not bool(metadata.get("forced")) and (
            metadata["trigger_mode"] == 0 or metadata.get("pulse_sampled", False)
        )
        if metadata["channel"] != channel - 1:
            raise ValueError("Unexpected record channel")
        return {
            "format": "slopscope-record",
            "version": 1,
            "encoding": "signed-int8/base64",
            "metadata": metadata,
            "data": base64.b64encode(data[words[2] :]).decode("ascii"),
        }

    def wait_analog(self):
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            status = fields(self.request("ANALOG STATUS"))
            if status.get("state") == 2:
                return
            if status.get("state") == 3:
                raise RuntimeError("Analog update failed")
            time.sleep(0.025)
        raise TimeoutError("Analog update timed out")

    def voltage_calibration(self, refresh=False, reference=False):
        cache = "_reference_calibration" if reference else "_calibration"
        if refresh:
            setattr(self, cache, None)
        if getattr(self, cache, None) is None:
            table = []
            for channel in (1, 2):
                entries = []
                for index in range(14):
                    query = "REFERENCE" if reference else "GET"
                    cal = fields(self.request(f"CH{channel} CAL {query} {index}"))
                    for key in ("zero", "slope"):
                        if cal[key] & 0x80000000:
                            cal[key] -= 0x100000000
                    entries.append(cal)
                table.append(entries)
            setattr(self, cache, table)
        return getattr(self, cache)

    def factory_table(self):
        """Read the validated stock record currently loaded from flash.

        This exports the original bytes; it cannot write or replace factory data.
        """
        if self.pending:
            raise RuntimeError("unexpected pending USB data")
        self.device.write(1, b"FACTORY TABLE\n")
        data = bytearray()
        while len(data) < 0x8650:
            data.extend(self.device.read(0x81, 16384))
            if data.startswith(b"ERR "):
                raise RuntimeError(data.decode("ascii").strip())
            if len(data) > 0x8650:
                raise ValueError("oversized factory table")
        if struct.unpack_from("<I", data, 0x6C)[0] != 0x069194C5:
            raise ValueError("unexpected factory model")
        checksum = 0
        for byte in data[:0x8640]:
            checksum ^= ((byte if byte < 128 else byte - 256) * 256) & 0xFFFFFFFF
            for _ in range(8):
                checksum = ((checksum << 1) ^ (0x1021 if checksum & 0x8000 else 0)) & 0xFFFFFFFF
        if checksum != struct.unpack_from("<I", data, 0x8644)[0]:
            raise ValueError("factory table checksum mismatch")
        return bytes(data)

    def measure_factory_gain(self, channel, range_index, positions_mdiv=(0,)):
        """Collect matched legacy/factory level pairs at one or two positions.

        Requires a stable two-level input that stays inside the selected range.
        The old ADC pair is restored even when a measurement fails.
        """
        if channel not in (1, 2) or not 0 <= range_index < 14:
            raise ValueError("invalid channel or range")
        positions = tuple(positions_mdiv)
        if (
            not positions
            or len(positions) > 2
            or any(type(x) is not int or not -4000 <= x <= 4000 for x in positions)
        ):
            raise ValueError("use one or two valid positions")
        try:
            baseline = fields(self.request("VOLTAGE BASELINE"))["mode"]
        except RuntimeError:
            baseline = 1
        if baseline != 1:
            raise RuntimeError(
                "Select Reference corrections before collecting calibration measurements"
            )
        cal = fields(self.request(f"CH{channel} CAL GET {range_index}"))
        for key in ("zero", "slope"):
            if cal[key] & 0x80000000:
                cal[key] -= 0x100000000
        legacy = fields(self.request(f"CH{channel} CAL LEGACY {range_index}"))
        factory = fields(self.request(f"CH{channel} CAL FACTORY {range_index}"))
        if (cal["adc_scale"], cal["adc_trim"]) != (legacy["scale"], legacy["trim"]):
            raise RuntimeError("range is not using its legacy ADC pair")
        channel_status = fields(self.request(f"CH{channel} STATUS"))
        view = fields(self.request(f"CH{channel} VIEW"))
        old_range = channel_status["range"]
        old_position = view["position_mdiv"]
        old_running = bool(self.live_status().get("enabled"))
        if old_position & 0x80000000:
            old_position -= 0x100000000

        def collect(position):
            self.request(f"CH{channel} POSITION {position}")
            self.wait_analog()
            prior = self.live_status().get("sequence", 0)
            self.request("LIVE RUN")
            deadline = time.monotonic() + 4
            frame = None
            while time.monotonic() < deadline:
                live = self.live_status()
                if live.get("sequence") != prior:
                    metadata, channels, _ = self.frame()
                    if (
                        metadata["sequence"] == live["sequence"]
                        and metadata[f"ch{channel}_range"] == range_index
                    ):
                        frame = (metadata, channels[channel - 1])
                        break
                time.sleep(0.025)
            self.stop()
            if frame is None:
                raise TimeoutError("No fresh factory-gain measurement frame")
            data = sorted(frame[1])
            n = len(data)
            mid = (data[n // 5] + data[4 * n // 5]) / 2
            low = sorted(x for x in data if x <= mid)
            high = sorted(x for x in data if x > mid)
            if len(low) < 20 or len(high) < 20:
                raise RuntimeError("Input did not show both stable levels")

            def median(x):
                return (x[(len(x) - 1) // 2] + x[len(x) // 2]) // 2

            levels = {
                "low": median(low),
                "high": median(high),
                "clipped": sum(abs(x) >= 124 for x in data) / n > 0.01,
            }
            if levels["clipped"] or levels["high"] - levels["low"] < 8:
                raise RuntimeError("Input clipped or has too little ADC span")
            return {
                "dac": fields(self.request(f"CH{channel} STATUS"))["offset_dac"],
                "levels": levels,
            }

        was_running = old_running
        self.stop()
        factory_applied = False
        try:
            if old_range != range_index:
                self.request(f"CH{channel} RANGE {range_index}")
                self.wait_analog()
            old_points = [collect(pos) for pos in positions]
            self.request(
                f"CH{channel} CAL SET2 {range_index} {cal['zero']} {cal['gain']} {cal['dac']} {cal['slope']} {cal['valid']} {factory['scale']} {factory['trim']}"
            )
            factory_applied = True
            self.wait_analog()
            new_points = [collect(pos) for pos in positions]
            return [
                {"dac": a["dac"], "legacy_levels": a["levels"], "factory_levels": b["levels"]}
                for a, b in zip(old_points, new_points)
            ]
        finally:
            try:
                self.stop()
            finally:
                try:
                    if factory_applied:
                        self.request(
                            f"CH{channel} CAL SET2 {range_index} {cal['zero']} {cal['gain']} {cal['dac']} {cal['slope']} {cal['valid']} {legacy['scale']} {legacy['trim']}"
                        )
                        self.wait_analog()
                finally:
                    try:
                        current = fields(self.request(f"CH{channel} STATUS"))["range"]
                        if current != old_range:
                            self.request(f"CH{channel} RANGE {old_range}")
                            self.wait_analog()
                    finally:
                        try:
                            current_position = fields(self.request(f"CH{channel} VIEW"))[
                                "position_mdiv"
                            ]
                            if current_position & 0x80000000:
                                current_position -= 0x100000000
                            if current_position != old_position:
                                self.request(f"CH{channel} POSITION {old_position}")
                                self.wait_analog()
                        finally:
                            if was_running:
                                self.request("LIVE RUN")

    def measure_factory_gain_dc(
        self, channel, range_index, positions_mdiv, voltage_levels_uv, set_level
    ):
        """Measure two steady DC levels under legacy and factory ADC words.

        `set_level(uv, path, position_mdiv)` must set and hold the isolated
        source output, then return. Use two levels at every position. This
        yields the same transfer tuples accepted by migrate_factory_gain().
        """
        if channel not in (1, 2) or not 0 <= range_index < 14:
            raise ValueError("invalid channel or range")
        positions = tuple(positions_mdiv)
        levels = tuple(voltage_levels_uv)
        if (
            not positions
            or len(positions) > 2
            or any(type(x) is not int or not -4000 <= x <= 4000 for x in positions)
        ):
            raise ValueError("use one or two valid positions")
        if len(levels) != 2 or any(type(x) is not int for x in levels) or levels[0] >= levels[1]:
            raise ValueError("provide two increasing DC levels in microvolts")
        if not callable(set_level):
            raise ValueError("set_level callback is required")
        try:
            baseline = fields(self.request("VOLTAGE BASELINE"))["mode"]
        except RuntimeError:
            baseline = 1
        if baseline != 1:
            raise RuntimeError(
                "Select Reference corrections before collecting calibration measurements"
            )
        cal = fields(self.request(f"CH{channel} CAL GET {range_index}"))
        for key in ("zero", "slope"):
            if cal[key] & 0x80000000:
                cal[key] -= 0x100000000
        legacy = fields(self.request(f"CH{channel} CAL LEGACY {range_index}"))
        factory = fields(self.request(f"CH{channel} CAL FACTORY {range_index}"))
        if (cal["adc_scale"], cal["adc_trim"]) != (legacy["scale"], legacy["trim"]):
            raise RuntimeError("range is not using its legacy ADC pair")
        status = fields(self.request(f"CH{channel} STATUS"))
        view = fields(self.request(f"CH{channel} VIEW"))
        old_range = status["range"]
        old_position = view["position_mdiv"]
        old_running = bool(self.live_status().get("enabled"))
        if old_position & 0x80000000:
            old_position -= 0x100000000
        factory_applied = False

        def capture(position, voltage, path):
            actual_uv = set_level(voltage, path, position)
            if actual_uv is not None and type(actual_uv) is not int:
                raise ValueError("set_level must return actual microvolts or None")
            prior = self.live_status().get("sequence", 0)
            self.request("LIVE RUN")
            deadline = time.monotonic() + 4
            result = None
            while time.monotonic() < deadline:
                live = self.live_status()
                if live.get("sequence") != prior:
                    metadata, channels, _ = self.frame()
                    if (
                        metadata["sequence"] == live["sequence"]
                        and metadata[f"ch{channel}_range"] == range_index
                    ):
                        result = (metadata, channels[channel - 1])
                        break
                time.sleep(0.025)
            self.stop()
            if result is None:
                raise TimeoutError("No fresh DC calibration frame")
            samples = sorted(result[1])
            n = len(samples)
            if sum(abs(x) >= 124 for x in samples) / n > 0.01:
                raise RuntimeError("DC level clips this range")
            code = (samples[(n - 1) // 2] + samples[n // 2]) // 2
            return {
                "dac": fields(self.request(f"CH{channel} STATUS"))["offset_dac"],
                "code": code,
                "actual_uv": actual_uv,
            }

        was_running = old_running
        self.stop()
        try:
            if old_range != range_index:
                self.request(f"CH{channel} RANGE {range_index}")
                self.wait_analog()
            old_points = []
            for position in positions:
                self.request(f"CH{channel} POSITION {position}")
                self.wait_analog()
                pair = [capture(position, voltage, "legacy") for voltage in levels]
                if pair[1]["code"] - pair[0]["code"] < 8:
                    raise RuntimeError("DC levels produce less than 8 ADC codes")
                old_points.append(pair)
            self.request(
                f"CH{channel} CAL SET2 {range_index} {cal['zero']} {cal['gain']} {cal['dac']} {cal['slope']} {cal['valid']} {factory['scale']} {factory['trim']}"
            )
            factory_applied = True
            self.wait_analog()
            new_points = []
            for position in positions:
                self.request(f"CH{channel} POSITION {position}")
                self.wait_analog()
                pair = [capture(position, voltage, "factory") for voltage in levels]
                if pair[1]["code"] - pair[0]["code"] < 8:
                    raise RuntimeError("DC levels produce less than 8 factory ADC codes")
                new_points.append(pair)
            for old, new in zip(old_points, new_points):
                for before, after, target in zip(old, new, levels):
                    if before["actual_uv"] is not None and after["actual_uv"] is not None:
                        tolerance = max(100, abs(target) // 1000)
                        if abs(before["actual_uv"] - after["actual_uv"]) > tolerance:
                            raise RuntimeError(
                                "Supply voltage changed between legacy and factory measurements"
                            )
            return [
                {
                    "dac": old[0]["dac"],
                    "legacy_levels": {"low": old[0]["code"], "high": old[1]["code"]},
                    "factory_levels": {"low": new[0]["code"], "high": new[1]["code"]},
                }
                for old, new in zip(old_points, new_points)
            ]
        finally:
            try:
                self.stop()
            finally:
                try:
                    if factory_applied:
                        self.request(
                            f"CH{channel} CAL SET2 {range_index} {cal['zero']} {cal['gain']} {cal['dac']} {cal['slope']} {cal['valid']} {legacy['scale']} {legacy['trim']}"
                        )
                        self.wait_analog()
                finally:
                    try:
                        current = fields(self.request(f"CH{channel} STATUS"))["range"]
                        if current != old_range:
                            self.request(f"CH{channel} RANGE {old_range}")
                            self.wait_analog()
                    finally:
                        try:
                            current_position = fields(self.request(f"CH{channel} VIEW"))[
                                "position_mdiv"
                            ]
                            if current_position & 0x80000000:
                                current_position -= 0x100000000
                            if current_position != old_position:
                                self.request(f"CH{channel} POSITION {old_position}")
                                self.wait_analog()
                        finally:
                            if was_running:
                                self.request("LIVE RUN")

    def migrate_factory_gain(self, channel, range_index, measurements):
        """Apply the one/two-position transfer measured by measure_factory_gain."""
        if channel not in (1, 2) or not 0 <= range_index < 14 or len(measurements) not in (1, 2):
            raise ValueError("invalid channel, range or measurements")
        args = []
        for point in measurements:
            dac = point.get("dac")
            if type(dac) is not int or not 0 <= dac <= 65535:
                raise ValueError("invalid measurement DAC")
            row = [dac]
            for key in ("legacy_levels", "factory_levels"):
                levels = point.get(key)
                if not isinstance(levels, dict) or "low" not in levels or "high" not in levels:
                    raise ValueError("transfer needs both low/high pairs")
                low, high = levels["low"], levels["high"]
                if type(low) is not int or type(high) is not int or not -127 <= low < high <= 126:
                    raise ValueError("invalid signed-byte levels")
                row.extend((low, high))
            args.extend(row)
        if len(measurements) == 1:
            _, old_low, old_high, new_low, new_high = args
            command = (
                f"CH{channel} CAL MIGRATE {range_index} {old_low} {old_high} {new_low} {new_high}"
            )
        else:
            command = f"CH{channel} CAL MIGRATE2 {range_index} " + " ".join(map(str, args))
        running = bool(self.live_status().get("enabled"))
        self.stop()
        try:
            reply = self.request(command)
            self.wait_analog()
            self._calibration = None
            return reply
        finally:
            if running:
                self.request("LIVE RUN")

    def settings_changed(self, previous):
        revision = previous.get("ui_revision")
        return revision is None or self.request("UI REVISION") != revision

    def settings(self):
        try:
            ui_revision = self.request("UI REVISION")
        except RuntimeError as error:
            if not str(error).startswith("ERR "):
                raise
            ui_revision = None
        try:
            capabilities = fields(self.request("CAPABILITIES"))
        except RuntimeError:
            capabilities = {}
        cap = fields(self.request("CAPTURE CONFIG"))
        cap["nominal_hz"] = cap.get("nominal_hz", 0) / cap.get("rate_denominator", 1)
        channels = [
            {**fields(self.request(f"CH{i} STATUS")), **fields(self.request(f"CH{i} VIEW"))}
            for i in (1, 2)
        ]
        device_acquisition = {}
        banks = {}
        try:
            device_acquisition = fields(self.request("ACQUIRE STATUS"))
            self._frame_version = device_acquisition.get("frame", 1)
            for i, ch in enumerate(channels, 1):
                ch["bandwidth"] = fields(self.request(f"CH{i} BANDWIDTH"))["enabled"]
        except RuntimeError:
            self._frame_version = 1

        view = fields(self.request("VIEW STATUS"))
        try:
            horizontal = fields(self.request("HORIZONTAL STATUS"))
        except RuntimeError:
            horizontal = {}
        try:
            roll = fields(self.request("ROLL STATUS"))
        except RuntimeError:
            roll = {}
        try:
            factory = fields(self.request("FACTORY STATUS"))
        except RuntimeError:
            factory = {}
        for channel in channels:
            if channel.get("position_mdiv", 0) & 0x80000000:
                channel["position_mdiv"] -= 0x100000000
        capture = fields(self.request("CAPTURE STATUS"))
        display = fields(self.request("DISPLAY STATUS"))
        trigger = {
            **fields(self.request("TRIGGER STATUS")),
            **fields(self.request("TRIGGER DETAIL")),
        }
        try:
            trigger.update(fields(self.request("TRIGGER ADVANCED")))
            trigger.update(fields(self.request("TRIGGER LIMITS")))
            trigger["advanced_supported"] = True
        except RuntimeError:
            trigger["advanced_supported"] = False
        try:
            trigger["pulse_status"] = fields(self.request("TRIGGER PULSE STATUS"))
        except RuntimeError:
            pass
        try:
            trigger.update(fields(self.request("TRIGGER FAMILY")))
            for name in ("NORMAL", "CH1", "CH2"):
                banks[name] = {}
                for part in ("EDGE", "TIMING", "MODE", "LIMITS"):
                    banks[name].update(fields(self.request(f"TRIGGER BANK {name} {part}")))
        except RuntimeError:
            pass

        persistence = fields(self.request("SETTINGS STATUS"))
        boot = fields(self.request("BOOT STATUS"))
        try:
            baseline = fields(self.request("VOLTAGE BASELINE"))["mode"]
        except RuntimeError:
            baseline = None
        key = (
            persistence.get("sequence"),
            boot.get("stage"),
            persistence.get("state"),
            baseline,
            tuple(ch.get("position_mdiv") for ch in channels) if baseline in (0, 2) else None,
        )
        if getattr(self, "_calibration_key", None) != key:
            self._calibration = None
            self._calibration_key = key
        reference_key = (persistence.get("sequence"), boot.get("stage"), persistence.get("state"))
        if getattr(self, "_reference_calibration_key", None) != reference_key:
            self._reference_calibration = None
            self._reference_calibration_key = reference_key
        calibration = self.voltage_calibration()
        calibration_model = fields(self.request("VOLTAGE MODEL"))["version"]
        from .lcd import lcd_settings

        lcd = lcd_settings(self)
        speed = self.request("USB SPEED")
        live = self.live_status()
        return {
            "ui_revision": ui_revision,
            "serial_number": self.serial_number,
            "lcd": lcd,
            "capabilities": capabilities,
            "running": bool(live.get("enabled")),
            "sequence": live.get("sequence", 0),
            "busy": bool(live.get("busy")),
            "epoch": live.get("epoch", 0),
            "rate_index": cap.get("rate_index"),
            "nominal_hz": cap.get("nominal_hz"),
            "channels": channels,
            "capture": capture,
            "display": display,
            "usb_speed": speed,
            "trigger": trigger,
            "device_acquisition": device_acquisition,
            "trigger_banks": banks,
            "roll": roll,
            "factory": factory,
            "horizontal": horizontal,
            "view": view,
            "calibration": calibration,
            "calibration_model": calibration_model,
            "calibration_baseline": baseline,
            "reference_calibration": self.voltage_calibration(reference=True)
            if baseline is not None
            else None,
            "persistence": persistence,
            "boot": boot,
        }

    def live_status(self):
        return fields(self.request("LIVE STATUS"))

    def activity(self):
        try:
            return fields(self.request("ACQUISITION STATUS"))
        except RuntimeError:
            return {}

    def stop(self):
        self.request("LIVE OFF")
        deadline = time.monotonic() + 3
        while True:
            state = fields(self.request("CAPTURE STATUS")).get("state")
            if state not in (1, 2, 3):
                break
            if time.monotonic() > deadline:
                raise TimeoutError("Scope did not finish stopping")
            time.sleep(0.01)

    def apply(self, action):
        op = action["op"]
        if op == "lcd_copy":
            from .lcd import lcd_copy

            return lcd_copy(self, action)
        elif op.startswith("lcd_"):
            from .lcd import lcd_command

            self.request(lcd_command(action))
        elif op == "run":
            self.request("LIVE RUN")
        elif op == "stop":
            self.stop()
        elif op == "calibration_baseline":
            self.request("VOLTAGE BASELINE " + action["value"])
            deadline = time.monotonic() + 15
            while True:
                state = fields(self.request("SETTINGS STATUS"))["state"]
                if state == 2:
                    break
                if state == 3:
                    raise RuntimeError("Calibration baseline restore failed")
                if time.monotonic() > deadline:
                    raise TimeoutError("Calibration baseline restore timed out")
                time.sleep(0.025)
            self._calibration = None
            self._reference_calibration = None
        elif op == "device_save":
            running = bool(self.live_status().get("enabled"))
            if running:
                self.stop()
            try:
                self.request("SETTINGS SAVE RUN" if running else "SETTINGS SAVE")
            finally:
                if running:
                    self.request("LIVE ON")
        elif op == "device_load":
            self._reference_calibration = None
            self.stop()
            self.request("SETTINGS LOAD")
            deadline = time.monotonic() + 15
            while True:
                status = fields(self.request("SETTINGS STATUS"))
                if status.get("state") == 2:
                    break
                if status.get("state") == 3:
                    raise RuntimeError("Scope settings restore failed")
                if time.monotonic() > deadline:
                    raise TimeoutError("Scope settings restore timed out")
                time.sleep(0.05)
            self._calibration = None
        elif op == "calibration":
            self._reference_calibration = None
            cal = action["cal"]
            ch = action["channel"]
            index = action["range"]
            running = bool(self.live_status().get("enabled"))
            self.stop()
            try:
                paired = "adc_scale" in cal and "adc_trim" in cal
                command = f"CH{ch} CAL {'SET2' if paired else 'SET'} {index} {cal['zero']} {cal['gain']} {cal['dac']} {cal['slope']} {cal['valid']}"
                if paired:
                    command += f" {cal['adc_scale']} {cal['adc_trim']}"
                self.request(command)
                self.wait_analog()
                updated = fields(self.request(f"CH{ch} CAL GET {index}"))
                for key in ("zero", "slope"):
                    if updated[key] & 0x80000000:
                        updated[key] -= 0x100000000
                if getattr(self, "_calibration", None) is not None:
                    self._calibration[ch - 1][index] = updated
            finally:
                if running:
                    self.request("LIVE ON")
        elif op == "fit":
            self.fit_voltage()
        elif op == "acquire":
            self.request(
                f"ACQUIRE {'MODE' if action['key'] == 'mode' else 'AVERAGES'} {action['value']}"
            )
        elif op == "trigger_limits":
            self.request(f"TRIGGER LIMITS {action['lower_uv']} {action['upper_uv']}")
        elif op == "video":
            standard, sync, count = action["standard"], action["sync"], action["count"]
            if fields(self.request("CAPABILITIES")).get("video_controls"):
                self.request(f"TRIGGER VIDEO SET {standard} {sync} {count}")
            else:
                # Compatibility with the previously installed CFW. New firmware
                # applies these three fields in a single command.
                for key, value in (("STANDARD", standard), ("SYNC", sync), ("COUNT", count)):
                    self.request(f"TRIGGER VIDEO {key} {value}")
        elif op == "horizontal":
            self.request(
                "HORIZONTAL STOCK"
                if action.get("stock")
                else f"HORIZONTAL OFFSET {action['value']}"
            )
        elif op == "roll":
            self.request(f"ROLL MODE {action['value']}")
        elif op == "view":
            self.request(f"VIEW WINDOW {action['start']} {action['points']}")
        elif op == "autoset":
            self.autoset()
        elif op == "defaults":
            self.defaults()
        elif op == "trigger":
            key = action["key"]
            value = action.get("value")
            if key == "force":
                self.request("TRIGGER FORCE")
            elif key == "single":
                self.request("TRIGGER SINGLE")
            else:
                if key == "family" and value == "ALTERNATE":
                    self.request("TRIGGER SWEEP AUTO")
                if key == "mode" and value == "VIDEO":
                    self.request("TRIGGER SWEEP AUTO")
                argument = {
                    "enabled": "ENABLE",
                    "source": "SOURCE",
                    "slope": "SLOPE",
                    "sweep": "SWEEP",
                    "level_uv": "LEVEL",
                    "code": "CODE",
                    "coupling": "COUPLING",
                    "holdoff_ns": "HOLDOFF",
                    "mode": "MODE",
                    "family": "FAMILY",
                    "condition": "CONDITION",
                    "width_ns": "WIDTH",
                    "lower_uv": "LOWER",
                    "upper_uv": "UPPER",
                    "count": "COUNT",
                    "video_count": "VIDEO COUNT",
                    "video_sync": "VIDEO SYNC",
                    "video_standard": "VIDEO STANDARD",
                }[key]
                self.request(f"TRIGGER {argument} {value}")
        elif op == "timebase":
            running = bool(self.live_status().get("enabled"))
            if running:
                self.stop()
            try:
                self.apply({"op": "rate", "index": action["index"]})
                self.request("VIEW WINDOW 0 750")
            finally:
                if running:
                    self.request("LIVE ON")
        elif op == "rate":
            self.request(f"CAPTURE RATE {action['index']}")
            deadline = time.monotonic() + 4
            while fields(self.request("CAPTURE CONFIG")).get("rate_index") != action["index"]:
                if time.monotonic() > deadline:
                    raise TimeoutError("Timebase update not applied")
                time.sleep(0.025)
        else:
            channel = action["channel"]
            position_after = None
            if op in ("scale", "probe"):
                previous = self.settings()["channels"][channel - 1]
                new_range = action["index"] if op == "scale" else previous["range"]
                new_probe = action["value"] if op == "probe" else previous["probe"]
                position_after = (
                    max(
                        -4000,
                        min(
                            4000,
                            round(
                                previous["position_mdiv"]
                                * SCALES_MV[previous["range"]]
                                * previous["probe"]
                                / (SCALES_MV[new_range] * new_probe)
                            ),
                        ),
                    )
                    if 2 <= new_range <= 11
                    else 0
                )
                if op == "scale":
                    self.apply({"op": "position", "channel": channel, "value": 0})
            arg = (
                f"RANGE {action['index']}"
                if op == "scale"
                else f"POSITION {action['value']}"
                if op == "position"
                else f"{op.upper()} {action['value']}"
            )
            deadline = time.monotonic() + 4
            while True:
                try:
                    self.request(f"CH{channel} {arg}")
                    break
                except RuntimeError as exc:
                    if "busy" not in str(exc) or time.monotonic() > deadline:
                        raise
                    time.sleep(0.025)
            if op in ("scale", "coupling", "position", "offset", "bandwidth"):
                self.wait_analog()
            if position_after is not None:
                self.apply({"op": "position", "channel": channel, "value": position_after})
        return self.settings()

    def fit_voltage(self):
        from .voltage import to_volts

        settings = self.settings()
        metadata, channels, _ = self.frame()
        for ch in range(2):
            if not settings["channels"][ch].get("visible"):
                continue
            values = to_volts(channels[ch], settings, metadata, ch)
            if settings["channels"][ch].get("inverse"):
                values = -values
            low, high = float(values.min()), float(values.max())
            center = (low + high) / 2
            desired = max((high - low) / 6, abs(center) / 3.5, 0.001)
            probe = settings["channels"][ch]["probe"]
            choices = [
                i
                for i in range(3 if settings.get("calibration_baseline") in (0, 2) else 2, 12)
                if SCALES_MV[i] * probe / 1000 >= desired
            ]
            index = choices[0] if choices else 11
            position = max(
                -4000, min(4000, round(-center / (SCALES_MV[index] * probe / 1000)) * 1000)
            )
            self.apply({"op": "position", "channel": ch + 1, "value": 0})
            if index != settings["channels"][ch]["range"]:
                self.apply({"op": "scale", "channel": ch + 1, "index": index})
            self.apply({"op": "position", "channel": ch + 1, "value": position})

    def defaults(self):
        self.stop()
        for channel in (1, 2):
            for arg in ("POSITION 0", "RANGE 9", "COUPLING DC"):
                self.request(f"CH{channel} {arg}")
                self.wait_analog()
            for arg in ("PROBE 1", "DISPLAY ON", "INVERSE OFF"):
                self.request(f"CH{channel} {arg}")
        try:
            self.request("TRIGGER FAMILY NORMAL")
            self.request("TRIGGER MODE EDGE")
            self.request("HORIZONTAL STOCK")
            self.request("ACQUIRE MODE SAMPLE")
            for ch in (1, 2):
                self.request(f"CH{ch} BANDWIDTH OFF")
                self.wait_analog()
        except RuntimeError:
            pass
        self.apply({"op": "calibration_baseline", "value": "STOCK"})
        for cmd in (
            "CAPTURE MODE NORMAL",
            "CAPTURE DECIMATION 1",
            "CAPTURE RATE 10",
            "VIEW WINDOW 0 750",
            "TRIGGER ENABLE ON",
            "TRIGGER SOURCE CH1",
            "TRIGGER SLOPE RISE",
            "TRIGGER SWEEP AUTO",
            "TRIGGER COUPLING DC",
            "TRIGGER HOLDOFF 100",
            "TRIGGER CODE 0",
            "LIVE ON",
        ):
            self.request(cmd)

    def autoset(self):
        from .analysis import measure

        metadata, channels, _ = self.frame()
        stats = [measure(ch, metadata["nominal_hz"]) for ch in channels]
        source = max(range(2), key=lambda i: stats[i]["amplitude"])
        signal = stats[source]
        if signal["amplitude"] < 12:
            raise RuntimeError(
                "No clear signal for Autoset; connect an input and choose a suitable range"
            )
        self.stop()
        channel = source + 1
        self.request(f"CH{channel} POSITION 0")
        self.wait_analog()
        current = metadata[f"ch{channel}_range"]
        wanted = SCALES_MV[current] * signal["amplitude"] / 100
        try:
            lowest = 3 if fields(self.request("VOLTAGE BASELINE"))["mode"] != 1 else 2
        except RuntimeError:
            lowest = 2
        selected = min(
            range(lowest, 12), key=lambda i: abs(__import__("math").log(SCALES_MV[i] / wanted))
        )
        if signal["clipped"]:
            selected = min(11, max(selected, current + 1))
        self.request(f"CH{channel} RANGE {selected}")
        self.wait_analog()
        rate = metadata["rate_index"]
        if signal["frequency_hz"]:
            wanted = 750 * signal["frequency_hz"] / 4
            rate = min(range(1, 16), key=lambda i: abs(__import__("math").log(RATES[i] / wanted)))
        for cmd in (
            f"CAPTURE RATE {rate}",
            "VIEW WINDOW 0 750",
            "TRIGGER ENABLE ON",
            f"TRIGGER SOURCE CH{channel}",
            "TRIGGER SLOPE RISE",
            "TRIGGER SWEEP NORMAL",
        ):
            self.request(cmd)
        # Comparator codes and CSI codes have different offsets. Verify an edge
        # in hardware instead of guessing the trigger level from waveform pixels.
        chosen = None
        dwell = max(0.1, min(0.5, 3 / (signal["frequency_hz"] or 30)))
        for candidate in (0,) + tuple(
            sign * level for level in range(16, 113, 16) for sign in (-1, 1)
        ):
            self.request(f"TRIGGER CODE {candidate}")
            baseline = self.live_status()["sequence"]
            self.request("LIVE ON")
            deadline = time.monotonic() + dwell
            while time.monotonic() < deadline:
                if self.live_status()["sequence"] != baseline:
                    chosen = candidate
                    break
                time.sleep(0.01)
            self.stop()
            if chosen is not None:
                break
        if chosen is None:
            self.request("TRIGGER SWEEP AUTO")
            self.request("LIVE ON")
            raise RuntimeError(
                "Autoset selected range/timebase but found no stable edge; acquisition restored in Auto mode"
            )
        self.request(f"TRIGGER CODE {chosen}")
        self.request("TRIGGER SWEEP AUTO")
        self.request("LIVE ON")

    def keep_alive(self):
        """Renew an idle application session; normal commands already renew it."""
        return self.request("SESSION PING" if self._session_supported else "PING")

    def frame_chunks(self):
        from .frame_transfer import iter_frame_chunks

        return iter_frame_chunks(self)

    def close(self, *, graceful=True):
        if self._lock_fd is None:
            return
        try:
            if (
                graceful
                and self._session_supported
                and self.last_operation.get("phase") == "complete"
                and not self.last_operation.get("command", "").startswith("REBOOT ")
            ):
                self._session_supported = False
                try:
                    self.request("SESSION CLOSE")
                except (usb.core.USBError, RuntimeError, TimeoutError, ValueError):
                    pass
            try:
                usb.util.release_interface(self.device, 0)
            except usb.core.USBError:
                pass
            usb.util.dispose_resources(self.device)
            from .usb_capture import close_read_buffer

            close_read_buffer(self.device)
        finally:
            os.close(self._lock_fd)
            self._lock_fd = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close(graceful=exc[0] is None)
