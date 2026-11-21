"""Discover scopes and select a single device without guessing between units."""

import hashlib
import os
import tempfile
from pathlib import Path

import usb.core


class ScopeNotConnected(RuntimeError):
    """No matching SlopScope device is currently connected."""


class ScopeSelectionRequired(RuntimeError):
    def __init__(self, scopes):
        self.scopes = scopes
        serials = ", ".join(scope["serial_number"] or "unreadable serial" for scope in scopes)
        super().__init__(
            f"Multiple scopes detected ({serials}). Connect with "
            "SlopScope(serial_number=...) or pass --serial SERIAL."
        )


def _connected_devices():
    try:
        return list(usb.core.find(find_all=True, idVendor=0xDEAF, idProduct=0xBABE))
    except usb.core.NoBackendError:
        raise RuntimeError("libusb is missing. Install it with brew install libusb on macOS, "
                           "or your Linux package manager.") from None


def _device_details(device):
    try:
        serial_number = device.serial_number
        error = None
    except (usb.core.USBError, ValueError) as exc:
        serial_number = None
        error = str(exc)
    return {
        "serial_number": serial_number,
        "bus": device.bus,
        "address": device.address,
        **({"error": error} if error else {}),
    }


def list_scopes():
    """Return connected SlopScope serial numbers and USB locations."""
    return [_device_details(device) for device in _connected_devices()]


def select_scope(serial_number=None, *, bus=None, address=None):
    devices = _connected_devices()
    if bus is not None or address is not None:
        devices = [device for device in devices
                   if (bus is None or device.bus == bus)
                   and (address is None or device.address == address)]
    details = [_device_details(device) for device in devices]
    if serial_number is not None:
        matches = [(device, info) for device, info in zip(devices, details)
                   if info["serial_number"] == serial_number]
    else:
        matches = list(zip(devices, details))
    if not matches:
        raise ScopeNotConnected(f"Waiting for SlopScope {serial_number}" if serial_number
                           else "Waiting for DEAF:BABE SlopScope")
    if len(matches) > 1:
        if serial_number is not None:
            raise RuntimeError(f"Duplicate USB serial {serial_number!r}; update the scopes' firmware")
        raise ScopeSelectionRequired(details)
    device, info = matches[0]
    if not info["serial_number"]:
        raise RuntimeError("Cannot read the scope USB serial: " + info.get("error", "missing descriptor"))
    return device, info["serial_number"]


def scope_lock_path(serial_number):
    serial_key = hashlib.sha256(serial_number.encode("utf-8")).hexdigest()[:24]
    return Path(tempfile.gettempdir()) / f"hscope-deaf-babe-{os.getuid()}-{serial_key}.lock"
