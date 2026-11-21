"""Optional libusb diagnostic read preserving bytes returned with a transfer error."""

import ctypes
import mmap

import usb.core


class NativeReadBuffer:
    """Keep a page-isolated receive allocation alive across synchronous reads."""

    def __init__(self, size):
        self.size = size
        self.mapping = mmap.mmap(-1, (size + mmap.PAGESIZE - 1) & -mmap.PAGESIZE)
        self.address = ctypes.addressof(ctypes.c_ubyte.from_buffer(self.mapping))
        self.data = ctypes.cast(self.address, ctypes.POINTER(ctypes.c_ubyte))

    def close(self):
        self.data = None
        self.mapping.close()


def close_read_buffer(device):
    buffers = getattr(device, "_scope_read_buffers", None)
    if buffers is not None:
        del device._scope_read_buffers
        for buffer in buffers.values():
            buffer.close()


def read_with_partial(device, size):
    backend = device._ctx.backend
    if not hasattr(backend, "lib") or not hasattr(backend.lib, "libusb_bulk_transfer"):
        return device.read(0x81, size)
    from usb.backend.libusb1 import _check

    handle = device._ctx.managed_open()
    buffers = getattr(device, "_scope_read_buffers", None)
    if buffers is None:
        buffers = device._scope_read_buffers = {}
    buffer = buffers.get(size)
    if buffer is None:
        buffer = buffers[size] = NativeReadBuffer(size)
    data = buffer.data
    ctypes.memset(buffer.address, 0, size)
    transferred = ctypes.c_int()
    status = backend.lib.libusb_bulk_transfer(
        handle.handle, 0x81, data, size, ctypes.byref(transferred), device.default_timeout
    )
    try:
        _check(status)
    except usb.core.USBError as error:
        error.scope_partial_payload = bytes(data[: transferred.value])
        error.scope_read_buffer = bytes(data[:size])
        error.scope_read_details = {
            "requested": size,
            "reported": transferred.value,
            "buffer_page_offset": buffer.address & 4095,
            "libusb_status": status,
            "allocation": "persistent_private_pages",
        }
        raise
    return bytes(data[: transferred.value])
