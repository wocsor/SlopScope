"""Exclusive text-command access for firmware maintenance tools."""

import fcntl
import os
import tempfile
import time
from pathlib import Path

os.environ.setdefault("DYLD_FALLBACK_LIBRARY_PATH", "/opt/homebrew/lib:/usr/local/lib")

import usb.core
import usb.util

VENDOR_ID = 0xDEAF
PRODUCT_ID = 0xBABE


class ScopeControl:
    def __init__(self):
        # Share the viewer lock so maintenance cannot interrupt another client.
        lock_path = Path(tempfile.gettempdir()) / f"hscope-deaf-babe-{os.getuid()}.lock"
        self.lock = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        self.device = None
        self.pending = b""
        try:
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise RuntimeError("Close the viewer or other scope client first") from None
            self.device = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)
            if self.device is None:
                raise RuntimeError("CFW USB device is not connected")
            self.device.default_timeout = 3000
            try:
                configuration = self.device.get_active_configuration()
            except ValueError:
                configuration = None
            if configuration is None or configuration.bConfigurationValue != 1:
                self.device.set_configuration(1)
            usb.util.claim_interface(self.device, 0)
            marker = f"CFW_SYNC_{time.time_ns()}"
            if self.request("ECHO " + marker) != marker:
                raise RuntimeError("Scope command synchronization failed")
        except Exception:
            self.close()
            raise

    def request(self, command):
        encoded = command.encode("ascii")
        if not encoded or len(encoded) > 128 or b"\n" in encoded or b"\r" in encoded:
            raise ValueError("Send one ASCII command of at most 128 bytes")
        self.device.write(1, encoded + b"\n")
        deadline = time.monotonic() + 5
        while b"\n" not in self.pending:
            if time.monotonic() >= deadline:
                raise TimeoutError("Scope command timed out")
            self.pending += bytes(self.device.read(0x81, 512))
            if len(self.pending) > 4096:
                raise RuntimeError("Oversized scope reply")
        reply, self.pending = self.pending.split(b"\n", 1)
        text = reply.decode("ascii")
        if text.startswith("ERR "):
            raise RuntimeError(text)
        return text

    def close(self):
        try:
            if self.device is not None:
                try:
                    usb.util.release_interface(self.device, 0)
                except usb.core.USBError:
                    pass
                usb.util.dispose_resources(self.device)
                self.device = None
        finally:
            if self.lock is not None:
                os.close(self.lock)
                self.lock = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
