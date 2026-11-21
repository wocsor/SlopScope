"""Local SlopScope viewer bridge; one worker owns each USB connection."""

from __future__ import annotations

import collections
import json
import logging
import queue
import threading
import time
import uuid
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import usb.core
from slopscope_usb.analysis import Processor
from slopscope_usb.controls import validate_action
from slopscope_usb.device import RATES, SCALES_MV, SlopScope, fields, frame_calibration_matches
from slopscope_usb.frame_transfer import FrameSkipped
from slopscope_usb.discovery import ScopeNotConnected, ScopeSelectionRequired, list_scopes

from .viewer import ViewerAssets

PREFERENCES = Path.home() / ".config/slopscope/viewer.json"
LEGACY_PREFERENCES = Path.home() / ".config/hscope/ramview.json"


class Poller(threading.Thread):
    def __init__(
        self, recover_usb=True, capture_directory=None, frame_read_size=4096, bounded_frames=False,
        serial_number=None,
    ):
        super().__init__(daemon=True)
        self.recover_usb = recover_usb
        self.frame_read_size = frame_read_size
        self.bounded_frames = bounded_frames
        self.serial_number = serial_number
        self.capture_directory = Path(capture_directory) if capture_directory else None
        self.cond = threading.Condition()
        self.jobs = queue.Queue(16)
        self.stop_event = threading.Event()
        self.processor = Processor()
        try:
            preferences = PREFERENCES if PREFERENCES.exists() else LEGACY_PREFERENCES
            prefs = json.loads(preferences.read_text())
            for key, value in prefs.items():
                self.processor.configure(key, value)
        except (OSError, ValueError, TypeError, AttributeError):
            self.processor = Processor()
        self.connection_errors = collections.deque(maxlen=20)
        self.seq = 0
        self.state = {
            "status": "Waiting for scope",
            "connected": False,
            "serial_number": serial_number,
            "settings": None,
            "frame": None,
            "session": uuid.uuid4().hex,
            "processing": dict(self.processor.config),
        }
        self.payload = json.dumps(self.state)

    def publish(self, **updates):
        if updates.get("settings") is not None:
            from slopscope_usb.lcd import math_processing_from_lcd
            for key, value in math_processing_from_lcd(updates["settings"]).items():
                if self.processor.config[key] != value:
                    self.processor.configure(key, value)
            updates["processing"] = dict(self.processor.config)
        with self.cond:
            self.state.update(updates)
            self.seq += 1
            self.state["revision"] = self.seq
            self.payload = json.dumps(self.state, separators=(",", ":"))
            self.cond.notify_all()
            return self.seq

    def submit(self, action):
        if not self.state["connected"]:
            raise RuntimeError("Scope is disconnected")
        job = {"action": action, "done": threading.Event(), "cancelled": False}
        self.jobs.put_nowait(job)
        if not job["done"].wait(25):
            job["cancelled"] = True
            raise TimeoutError("Control request timed out")
        if "error" in job:
            raise RuntimeError(job["error"])
        return {
            "settings": job["settings"],
            "revision": job["revision"],
            "session": self.state["session"],
            "processing": job["processing"],
            **({"record": job["record"]} if "record" in job else {}),
            **({"screenshot": job["screenshot"]} if "screenshot" in job else {}),
        }

    def reject_jobs(self, message):
        while True:
            try:
                job = self.jobs.get_nowait()
            except queue.Empty:
                return
            job["error"] = message
            job["done"].set()

    def run(self):
        reset_pending = False
        reset_attempts = 0
        last_reset = -float("inf")
        transport = None
        while not self.stop_event.is_set():
            scope = None
            try:
                reset_now = (
                    reset_pending and reset_attempts < 3 and time.monotonic() - last_reset >= 30
                )
                if reset_now:
                    reset_attempts += 1
                    last_reset = time.monotonic()
                    reset_pending = False
                    self.publish(
                        usb_recovery={
                            "attempts": reset_attempts,
                            "at": time.time(),
                            "state": "Resetting USB connection",
                        },
                        status="Recovering USB connection",
                    )
                with SlopScope(
                    serial_number=self.serial_number,
                    reset_usb=reset_now,
                    recover_halts=self.recover_usb,
                    capture_failed_reads=self.capture_directory is not None,
                    frame_read_size=self.frame_read_size,
                    bounded_frames=self.bounded_frames,
                ) as scope:
                    self.serial_number = scope.serial_number
                    try:
                        orphan = scope.request("RECORD STATUS")
                    except RuntimeError:
                        orphan = ""
                    if "busy=00000001" in orphan:
                        scope.request("RECORD CANCEL")
                    settings = scope.settings()
                    self.publish(
                        connected=True,
                        serial_number=scope.serial_number,
                        status="Connected",
                        settings=settings,
                        frame=None,
                        activity=None,
                        record_acquisition={"running": False},
                    )
                    calibration_job = None
                    record_job = None
                    record_steps = None
                    last_record_poll = 0
                    if scope.request("CALIBRATION STATUS").strip().endswith("active=00000001"):
                        scope.apply({"op": "device_load"})
                        scope.request("CALIBRATION CANCEL")
                        settings = scope.settings()
                    last_frame_request = 0
                    rejected_calibration = None
                    last_seq = None
                    last_generation = -1
                    last_tick = None
                    last_at = 0
                    last_settings = time.monotonic()
                    last_live = 0
                    last_transport = 0
                    connected_at = time.monotonic()
                    stamps = collections.deque(maxlen=45)
                    while not self.stop_event.is_set():
                        try:
                            job = self.jobs.get_nowait()
                        except queue.Empty:
                            job = None
                        if job and not job["cancelled"]:
                            try:
                                op = job["action"]["op"]
                                display_only = op in ("lcd_theme", "lcd_brightness", "lcd_cursors", "lcd_math", "lcd_fft")
                                if op == "lcd_copy":
                                    from slopscope_usb.lcd import lcd_copy

                                    job["screenshot"] = lcd_copy(scope, job["action"])
                                    job["settings"] = settings
                                    job["processing"] = dict(self.processor.config)
                                    job["revision"] = self.seq
                                else:
                                    if (
                                        record_job is not None
                                        and op != "processing"
                                        and not display_only
                                        and not (
                                            op == "trigger" and job["action"].get("key") == "force"
                                        )
                                    ):
                                        if op == "record":
                                            raise RuntimeError("A record is already being acquired")
                                        record_steps.close()
                                        record_job["error"] = "Record cancelled by control change"
                                        record_job["done"].set()
                                        record_job = record_steps = None
                                        self.publish(
                                            record_acquisition={
                                                "running": False,
                                                "message": "Record cancelled",
                                            }
                                        )
                                    if calibration_job is not None and op != "selfcal_cancel":
                                        raise RuntimeError(
                                            "Calibration is running; cancel it before changing controls"
                                        )
                                    if op == "selfcal_start":
                                        if settings.get("calibration_baseline") != 1:
                                            raise RuntimeError(
                                                "Select Reference corrections before recalibrating"
                                            )
                                        from slopscope_usb.calibration import calibrate

                                        calibration_job = calibrate(scope)
                                        self.publish(
                                            calibration={
                                                "running": True,
                                                "message": "Starting zero/offset calibration",
                                            }
                                        )
                                    elif op == "selfcal_cancel":
                                        if calibration_job is not None:
                                            calibration_job.close()
                                            calibration_job = None
                                        self.publish(
                                            calibration={
                                                "running": False,
                                                "message": "Calibration cancelled; original setup restored",
                                            }
                                        )
                                        settings = scope.settings()
                                    elif job["action"]["op"] == "profile":
                                        for action in job["action"]["actions"]:
                                            if action["op"] == "processing":
                                                self.processor.configure(action["key"], action["value"])
                                            else:
                                                settings = scope.apply(action)
                                        self.processor.reset()
                                    elif op == "record":
                                        record_steps = scope.record_steps(
                                            job["action"]["channel"],
                                            job["action"]["depth"],
                                            job["action"]["force"],
                                        )
                                        record_job = job
                                        self.publish(
                                            record_acquisition={
                                                "running": True,
                                                "message": "Acquiring record",
                                                "elapsed_s": 0,
                                            }
                                        )
                                    elif (
                                        record_job is not None
                                        and op == "trigger"
                                        and job["action"].get("key") == "force"
                                    ):
                                        scope.request("TRIGGER FORCE")
                                    elif job["action"]["op"] == "processing":
                                        from slopscope_usb.lcd import mirrored_math_action

                                        candidate = {**self.processor.config,
                                                     job["action"]["key"]: job["action"]["value"]}
                                        mirror = mirrored_math_action(candidate, settings, job["action"]["key"])
                                        if mirror is not None:
                                            settings = scope.apply(mirror)
                                        self.processor.configure(
                                            job["action"]["key"], job["action"]["value"]
                                        )
                                    else:
                                        settings = scope.apply(job["action"])
                                        if job["action"]["op"] == "defaults":
                                            self.processor = Processor()
                                        elif not display_only:
                                            self.processor.reset()
                                    if job["action"]["op"] in ("processing", "profile", "defaults"):
                                        PREFERENCES.parent.mkdir(parents=True, exist_ok=True)
                                        temporary = PREFERENCES.with_suffix(".tmp")
                                        temporary.write_text(
                                            json.dumps(self.processor.config, indent=2) + "\n"
                                        )
                                        temporary.replace(PREFERENCES)
                                    job["settings"] = settings
                                    job["processing"] = dict(self.processor.config)
                                    if not display_only and op != "processing":
                                        stamps.clear()
                                        last_seq = None
                                        last_tick = None
                                    job["revision"] = self.publish(
                                        settings=settings,
                                        processing=job["processing"],
                                        status="Stopped" if not settings["running"] else "Running",
                                    )
                                    job["processing"] = dict(self.processor.config)
                            except (usb.core.USBError, ValueError, UnicodeError) as exc:
                                job["error"] = str(exc)
                                raise
                            except Exception as exc:
                                job["error"] = str(exc)
                                self.publish(status=str(exc))
                            finally:
                                if job is not record_job:
                                    job["done"].set()
                        if record_job is not None:
                            if record_job["cancelled"]:
                                record_steps.close()
                                record_job["error"] = "Record request cancelled"
                                record_job["done"].set()
                                record_job = record_steps = None
                                self.publish(
                                    record_acquisition={
                                        "running": False,
                                        "message": "Record cancelled",
                                    }
                                )
                            elif time.monotonic() - last_record_poll >= 0.03:
                                last_record_poll = time.monotonic()
                                try:
                                    progress = next(record_steps)
                                    self.publish(
                                        record_acquisition=progress,
                                        activity={**scope.activity(), "received_at": time.time()},
                                        status="Acquiring record",
                                    )
                                except StopIteration as done:
                                    job = record_job
                                    record_job = record_steps = None
                                    settings = scope.settings()
                                    self.processor.reset()
                                    job.update(
                                        record=done.value,
                                        settings=settings,
                                        processing=dict(self.processor.config),
                                    )
                                    job["revision"] = self.publish(
                                        settings=settings,
                                        record_acquisition={
                                            "running": False,
                                            "message": "Record complete",
                                        },
                                        status="Stopped",
                                    )
                                    job["done"].set()
                                except Exception as exc:
                                    record_job["error"] = str(exc)
                                    record_job["done"].set()
                                    record_job = record_steps = None
                                    settings = scope.settings()
                                    self.publish(
                                        settings=settings,
                                        record_acquisition={"running": False, "message": str(exc)},
                                        status=str(exc),
                                    )
                            if record_job is not None:
                                self.stop_event.wait(0.005)
                                continue
                        if calibration_job is not None:
                            try:
                                progress = next(calibration_job)
                                if progress.get("finished"):
                                    result_path = PREFERENCES.with_name("calibration-last.json")
                                    result_path.parent.mkdir(parents=True, exist_ok=True)
                                    result_path.write_text(json.dumps(progress, indent=2) + "\n")
                                self.publish(
                                    calibration={
                                        "running": not progress.get("finished", False),
                                        **progress,
                                    }
                                )
                            except StopIteration:
                                calibration_job = None
                                settings = scope.settings()
                                self.processor.reset()
                                self.publish(
                                    settings=settings,
                                    calibration={
                                        **self.state.get("calibration", {}),
                                        "running": False,
                                        "message": "Zero/offset calibration complete and saved",
                                    },
                                )
                            except Exception as exc:
                                calibration_job = None
                                settings = scope.settings()
                                self.publish(
                                    settings=settings,
                                    calibration={
                                        "running": False,
                                        "message": str(exc),
                                        "error": True,
                                    },
                                )
                        if time.monotonic() - last_settings > 2:
                            if scope.settings_changed(settings):
                                settings = scope.settings()
                                self.publish(settings=settings)
                            last_settings = time.monotonic()
                        if time.monotonic() - last_transport > 2:
                            try:
                                transport = {
                                    **fields(scope.request("USB STATE")),
                                    "received_at": time.time(),
                                }
                            except RuntimeError as exc:
                                if str(exc) != "ERR unknown command":
                                    raise
                                transport = None
                            self.publish(transport=transport)
                            last_transport = time.monotonic()
                        if reset_attempts and time.monotonic() - connected_at >= 15:
                            reset_attempts = 0
                            reset_pending = False
                            self.publish(
                                usb_recovery={
                                    "attempts": 0,
                                    "at": time.time(),
                                    "state": "Connected after recovery",
                                }
                            )
                        if time.monotonic() - last_live > 0.1:
                            live = scope.live_status()
                            activity = {**scope.activity(), "received_at": time.time()}
                            self.publish(activity=activity)
                            last_live = time.monotonic()
                            updated = {
                                **settings,
                                "running": bool(live.get("enabled")),
                                "sequence": live.get("sequence", 0),
                                "busy": bool(live.get("busy")),
                                "epoch": live.get("epoch", 0),
                            }
                            if updated != settings:
                                settings = updated
                                self.publish(
                                    settings=settings,
                                    status="Running" if settings["running"] else "Stopped",
                                )
                        if (
                            settings["running"]
                            or self.state["frame"] is None
                            or settings.get("sequence", 0) != last_seq
                            or last_generation != self.processor.generation
                        ) and time.monotonic() - last_frame_request >= 0.03:
                            last_frame_request = time.monotonic()
                            try:
                                if self.bounded_frames:
                                    metadata, channels, payload = scope.frame(
                                        cancelled=lambda: not self.jobs.empty()
                                    )
                                else:
                                    metadata, channels, payload = scope.frame()
                            except FrameSkipped:
                                continue
                            except RuntimeError as exc:
                                if str(exc) == "ERR no frame":
                                    self.stop_event.wait(0.03)
                                    continue
                                raise
                            if not frame_calibration_matches(metadata, settings):
                                identity = tuple(metadata.get("calibration_ids", ()))
                                if identity != rejected_calibration:
                                    scope.voltage_calibration(refresh=True)
                                    settings = scope.settings()
                                    self.publish(settings=settings)
                                    rejected_calibration = identity
                                if not frame_calibration_matches(metadata, settings):
                                    self.publish(
                                        frame=None,
                                        status="Calibration changed; Run to acquire a new frame",
                                    )
                                    self.stop_event.wait(0.03)
                                    continue
                            rejected_calibration = None
                            if metadata["sequence"] != last_seq or last_generation != self.processor.generation:
                                now = time.monotonic()
                                if metadata["sequence"] != last_seq:
                                    stamps.append((now, len(payload)))
                                elapsed = stamps[-1][0] - stamps[0][0] if len(stamps) > 1 else 0
                                fps = (len(stamps) - 1) / elapsed if elapsed else 0
                                capture_fps = None
                                if last_seq is not None:
                                    ticks = (metadata["ticks_24mhz"] - last_tick) & 0xFFFFFFFF
                                    delta = (metadata["sequence"] - last_seq) & 0xFFFFFFFF
                                    if ticks and 0 < delta < 10000:
                                        capture_fps = delta * 24000000 / ticks
                                last_generation = self.processor.generation
                                last_seq = metadata["sequence"]
                                last_tick = metadata["ticks_24mhz"]
                                last_at = now
                                analysis = self.processor.process(
                                    channels, metadata["nominal_hz"], metadata, settings
                                )
                                self.publish(
                                    frame={
                                        **analysis,
                                        "metadata": metadata,
                                        "channels": channels,
                                        "fps": fps,
                                        "capture_fps": capture_fps,
                                        "payload_bps": fps * len(payload),
                                        "received_at": time.time(),
                                    },
                                    status="Live" if settings["running"] else "Stopped",
                                )
                            elif (
                                settings["running"]
                                and not settings.get("roll", {}).get("filling")
                                and last_at
                                and time.monotonic() - last_at > 3
                            ):
                                trigger = settings.get("trigger", {})
                                waiting = trigger.get("en") and trigger.get("sweep") in (1, 2)
                                self.publish(
                                    status="Waiting for matching trigger"
                                    if waiting
                                    else "No new acquisitions"
                                )
                                last_at = time.monotonic()
                        self.stop_event.wait(0.002 if settings["running"] else 0.04)
                    if record_job is not None:
                        record_steps.close()
                        record_job["error"] = "Viewer stopped"
                        record_job["done"].set()
                        record_job = record_steps = None
            except ScopeNotConnected as exc:
                self.publish(connected=False, status=str(exc), frame=None, settings=None)
                self.reject_jobs("Scope is disconnected")
                self.stop_event.wait(1)
            except ScopeSelectionRequired as exc:
                self.publish(connected=False, status="Select a scope by serial number",
                             available_scopes=exc.scopes, frame=None)
                self.stop_event.wait(1)
            except Exception as exc:
                if SlopScope._is_io(exc):
                    reset_pending = True
                error = {
                    "at": time.time(),
                    "type": type(exc).__name__,
                    "message": str(exc),
                    "errno": getattr(exc, "errno", None),
                    "backend_error_code": getattr(exc, "backend_error_code", None),
                    "operation": getattr(exc, "scope_operation", None)
                    or getattr(scope, "last_operation", None),
                    "transport": transport,
                }
                if hasattr(exc, "scope_read_details"):
                    error["read_details"] = exc.scope_read_details
                if self.capture_directory is not None:
                    try:
                        self.capture_directory.mkdir(parents=True, exist_ok=True)
                        capture_name = str(time.time_ns())
                        payload = getattr(exc, "scope_payload", None)
                        if payload is not None:
                            payload_path = self.capture_directory / (capture_name + ".bin")
                            payload_path.write_bytes(payload)
                            error["payload_path"] = str(payload_path)
                        read_buffer = getattr(exc, "scope_read_buffer", None)
                        if read_buffer is not None:
                            buffer_path = self.capture_directory / (
                                capture_name + ".read-buffer.bin"
                            )
                            buffer_path.write_bytes(read_buffer)
                            error["read_buffer_path"] = str(buffer_path)
                        (self.capture_directory / (capture_name + ".json")).write_text(
                            json.dumps(error, indent=2) + "\n"
                        )
                    except OSError as capture_error:
                        logging.error("Could not preserve USB capture: %s", capture_error)
                self.connection_errors.append(error)
                logging.exception(
                    "Scope communication failed: %s", json.dumps(error, separators=(",", ":"))
                )
                self.publish(connection_errors=list(self.connection_errors))
                if "record_job" in locals() and record_job is not None:
                    record_job["error"] = str(exc)
                    record_job["done"].set()
                    record_job = record_steps = None
                recovery_state = (
                    "Automatic USB reset limit reached; reconnect the data cable"
                    if reset_pending and reset_attempts >= 3
                    else "USB recovery pending"
                    if reset_pending
                    else "Waiting for connection"
                )
                self.publish(
                    connected=False,
                    status=recovery_state if reset_pending else str(exc),
                    frame=None,
                    settings=None,
                    record_acquisition={"running": False},
                    usb_recovery={
                        "attempts": reset_attempts,
                        "at": time.time(),
                        "state": recovery_state,
                    },
                )
                self.reject_jobs("Scope disconnected")
                if not self.recover_usb and (
                    isinstance(exc, (usb.core.USBError, TimeoutError, ValueError))
                    or scope is not None
                ):
                    self.publish(
                        status="USB error captured; recovery paused for diagnosis",
                        usb_recovery={
                            "state": "Disabled for diagnosis",
                            "attempts": reset_attempts,
                        },
                    )
                    self.stop_event.wait()
                    break
                self.stop_event.wait(1)
        self.reject_jobs("Viewer stopped")


def make_server(poller, port, assets=None):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def send_json(self, code, data):
            self.send_body(code, json.dumps(data).encode(), "application/json")

        def send_body(self, code, body, ctype):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def send_asset(self, filename, content_type):
            try:
                self.send_body(200, self.server.assets.read(filename), content_type)
            except (OSError, RuntimeError, ValueError) as error:
                self.send_body(503, str(error).encode(), "text/plain; charset=utf-8")

        def do_GET(self):
            poller = self.server.poller
            if self.path == "/":
                self.send_asset("index.html", "text/html; charset=utf-8")
            elif self.path == "/state":
                self.send_body(200, poller.payload.encode(), "application/json")
            elif self.path == "/devices":
                try:
                    self.send_json(200, {"devices": list_scopes(),
                                         "selected": poller.serial_number})
                except (RuntimeError, usb.core.USBError) as error:
                    self.send_json(409, {"error": str(error)})
            elif self.path == "/ramview.js":
                self.send_asset("ramview.js", "text/javascript; charset=utf-8")
            elif self.path in (
                "/browser-transport.js",
                "/webusb.js",
                "/browser-analysis.js",
                "/browser-controls.js",
                "/measurement-display.js",
                "/lcd-controls.js",
                "/lcd-ui.js",
                "/cursor-ui.js",
                "/scope-picker.js",
                "/math-mirror.js",
            ):
                self.send_asset(self.path[1:], "text/javascript; charset=utf-8")
            elif self.path in ("/options", "/options.json"):
                self.send_json(
                    200,
                    {
                        "rates": RATES,
                        "scales_mv": SCALES_MV,
                        "bridge": self.server.bridge_mode,
                        "bridge_available": True,
                    },
                )
            elif self.path == "/events":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Connection", "close")
                self.end_headers()
                last = -1
                try:
                    while not poller.stop_event.is_set():
                        with poller.cond:
                            poller.cond.wait_for(
                                lambda: poller.seq != last or poller.stop_event.is_set(), timeout=10
                            )
                            seq, payload = poller.seq, poller.payload
                        self.wfile.write(
                            (f"data: {payload}\n\n" if seq != last else ": keepalive\n\n").encode()
                        )
                        self.wfile.flush()
                        last = seq
                except (BrokenPipeError, ConnectionResetError):
                    pass
            else:
                self.send_json(404, {"error": "Not found"})

        def do_POST(self):
            poller = self.server.poller
            port = self.server.server_address[1]
            host = self.headers.get("Host")
            origin = self.headers.get("Origin")
            if host not in (f"127.0.0.1:{port}", f"localhost:{port}") or (
                origin and origin != f"http://{host}"
            ):
                self.close_connection = True
                self.send_json(403, {"error": "Local viewer requests only"})
                return
            if self.path not in ("/control", "/transport", "/device"):
                self.close_connection = True
                self.send_json(404, {"error": "Not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if (
                    self.headers.get("Content-Type", "").split(";")[0] != "application/json"
                    or not 0 < length <= 16000
                ):
                    raise ValueError("Expected a small JSON action")
                body = json.loads(self.rfile.read(length))
                if self.path == "/device":
                    serial_number = body.get("serial_number") if isinstance(body, dict) else None
                    if not isinstance(serial_number, str) or not serial_number or len(serial_number) > 128:
                        raise ValueError("Choose a USB serial number")
                    matches = [device for device in list_scopes()
                               if device["serial_number"] == serial_number]
                    if len(matches) != 1:
                        raise ValueError("Serial is missing or duplicated; refresh the scope list")
                    with self.server.transport_lock:
                        previous = self.server.poller
                        if previous.state.get("calibration", {}).get("running") or previous.state.get(
                            "record_acquisition", {}
                        ).get("running"):
                            raise RuntimeError("Finish or cancel capture/calibration before switching scopes")
                        previous.publish(connected=False, status="Switching scopes", frame=None)
                        previous.stop_event.set()
                        previous.reject_jobs("Scope selection changed")
                        with previous.cond:
                            previous.cond.notify_all()
                        if previous.ident is not None:
                            previous.join(timeout=6)
                        if previous.is_alive():
                            raise RuntimeError("Previous USB operation is still finishing; try again")
                        self.server.poller = Poller(
                            recover_usb=previous.recover_usb,
                            capture_directory=previous.capture_directory,
                            frame_read_size=previous.frame_read_size,
                            bounded_frames=previous.bounded_frames,
                            serial_number=serial_number,
                        )
                        if self.server.bridge_mode:
                            self.server.poller.start()
                    self.send_json(200, {"serial_number": serial_number})
                    return
                if self.path == "/transport":
                    if not isinstance(body, dict) or body.get("mode") not in ("webusb", "bridge"):
                        raise ValueError("Expected a USB transport mode")
                    with self.server.transport_lock:
                        poller = self.server.poller
                        if body["mode"] == "webusb":
                            if poller.state.get("calibration", {}).get(
                                "running"
                            ) or poller.state.get("record_acquisition", {}).get("running"):
                                raise RuntimeError(
                                    "Finish or cancel the current capture/calibration before switching"
                                )
                            poller.publish(connected=False, status="USB released for WebUSB")
                            poller.stop_event.set()
                            poller.reject_jobs("USB transport changed")
                            with poller.cond:
                                poller.cond.notify_all()
                            poller.join(timeout=6)
                            if poller.is_alive():
                                raise RuntimeError(
                                    "The USB operation is still finishing; try switching again"
                                )
                            self.server.bridge_mode = False
                        elif not self.server.bridge_mode:
                            self.server.poller = Poller(
                                recover_usb=self.server.poller.recover_usb,
                                capture_directory=self.server.poller.capture_directory,
                                frame_read_size=self.server.poller.frame_read_size,
                                bounded_frames=self.server.poller.bounded_frames,
                                serial_number=self.server.poller.serial_number,
                            )
                            self.server.bridge_mode = True
                            self.server.poller.start()
                    self.send_json(200, {"bridge": self.server.bridge_mode})
                    return
                if not self.server.bridge_mode:
                    raise RuntimeError("Switch to the Python bridge before sending HTTP controls")
                action = validate_action(body)
                result = poller.submit(action)
                self.send_json(200, result)
            except (ValueError, TypeError) as exc:
                self.close_connection = True
                self.send_json(400, {"error": str(exc)})
            except (RuntimeError, TimeoutError, queue.Full) as exc:
                self.send_json(409, {"error": str(exc) or "Control queue is full"})

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    server.poller = poller
    server.assets = assets or ViewerAssets()
    server.bridge_mode = True
    server.transport_lock = threading.Lock()
    return server


def serve(
    port=8766, open_browser=True, recover_usb=True, capture_directory=None, frame_read_size=4096,
    bounded_frames=True, serial_number=None, ui_dir=None, viewer_url=None,
):
    poller = Poller(
        recover_usb=recover_usb,
        capture_directory=capture_directory,
        frame_read_size=frame_read_size,
        bounded_frames=bounded_frames,
        serial_number=serial_number,
    )
    list_scopes()
    assets = ViewerAssets(ui_dir, viewer_url)
    assets.read("index.html")
    server = make_server(poller, port, assets)
    poller.start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"SlopScope Python bridge: {url}", flush=True)
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        poller = server.poller
        poller.stop_event.set()
        with poller.cond:
            poller.cond.notify_all()
        server.server_close()
        poller.join(timeout=6)

