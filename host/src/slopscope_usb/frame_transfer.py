"""Bounded snapshot transfers with explicit identity, offsets and cancellation."""

import struct
import time
import zlib

import usb.core

from .device import fields


class FrameSkipped(RuntimeError):
    """The caller abandoned a healthy transfer between fully consumed chunks."""


def iter_frame_chunks(scope):
    """Yield (offset, bytes); close the generator to skip its remaining chunks.

    Use one calling thread. Controls may run between yields; do not open another
    frame transfer until this generator has been closed.
    """
    if getattr(scope, "_frame_transfer_active", False):
        raise RuntimeError("A frame transfer is already active")
    if scope.pending:
        raise RuntimeError("Unexpected pending USB data")
    scope._frame_transfer_active = True
    transfer_id = None
    aligned = False
    try:
        opened = fields(scope.request("FRAME OPEN"))
        transfer_id = opened["id"]
        total_bytes, chunk_size = opened["bytes"], opened["chunk"]
        if not transfer_id or total_bytes != 4124 or chunk_size != 448:
            raise ValueError("Unsupported bounded frame transfer")
        aligned = True
        offset = 0
        while offset < total_bytes:
            payload_bytes = min(chunk_size, total_bytes - offset)
            command = f"FRAME READ {transfer_id} {offset}"
            scope.last_operation = {
                "command": command,
                "phase": "write",
                "started_at": time.time(),
                "expected": 32 + payload_bytes,
                "received": 0,
                "transfer_id": transfer_id,
                "frame_offset": offset,
            }
            received = bytearray()
            aligned = False
            try:
                scope.device.write(1, command.encode("ascii") + b"\n")
                scope.last_operation["phase"] = "read"
                deadline = time.monotonic() + 5
                while len(received) < 32 + payload_bytes:
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Frame chunk timeout")
                    received.extend(scope._read_bulk(512))
                    scope.last_operation["received"] = len(received)
                    if received.startswith(b"ERR ") and received.endswith(b"\n"):
                        aligned = True
                        raise RuntimeError(received.decode("ascii").strip())
                    if len(received) > 32 + payload_bytes:
                        raise ValueError("Oversized frame chunk")
                aligned = True
                scope.last_operation["phase"] = "validate"
                header = struct.unpack_from("<8I", received)
                expected = (
                    0x314B4843,
                    transfer_id,
                    opened["sequence"],
                    offset,
                    total_bytes,
                    payload_bytes,
                    1,
                )
                if header[:7] != expected:
                    raise ValueError("Frame chunk identity or bounds mismatch")
                struct.pack_into("<I", received, 28, 0)
                if zlib.crc32(received) != header[7]:
                    raise ValueError("Frame chunk CRC mismatch")
                scope.last_operation["phase"] = "complete"
            except (usb.core.USBError, RuntimeError, TimeoutError, ValueError) as error:
                error.scope_payload = bytes(received) + getattr(error, "scope_partial_payload", b"")
                error.scope_operation = dict(scope.last_operation)
                raise
            yield offset, bytes(received[32:])
            offset += payload_bytes
    finally:
        try:
            if (
                transfer_id is not None
                and aligned
                and scope.last_operation.get("phase") == "complete"
            ):
                scope.request(f"FRAME CLOSE {transfer_id}")
        finally:
            scope._frame_transfer_active = False
