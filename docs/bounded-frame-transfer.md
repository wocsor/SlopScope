# Bounded live frame transfer

This optional protocol extension bounds each reply and lets a client abandon an
old snapshot between chunks. It does not repair a USB bus transaction error or
turn preview frames into continuous, lossless ADC records. Acquisition and LCD
drawing continue independently. Existing FRAME7 clients remain compatible.

`CAPABILITIES` reports `frame_chunks=00000001` when supported.

- `FRAME OPEN` replaces the pinned snapshot with the latest available FRAME7
  image and replies `FRAME id=XXXXXXXX bytes=XXXXXXXX sequence=XXXXXXXX
  chunk=000001C0`. Fields are hexadecimal. A new nonzero ID invalidates older IDs.
- `FRAME READ <id> <offset>` takes decimal arguments and returns up to 448 bytes
  from that snapshot, prefixed by the binary header below. Reads are stateless:
  the same ID/offset returns the same bytes while the snapshot remains pinned.
- `FRAME CLOSE <id>` releases the snapshot and replies `OK`. Closing between
  chunks skips the remaining bytes. A new OPEN also replaces an abandoned frame.

The little-endian header contains eight 32-bit words: magic `0x314b4843` (CHK1),
transfer ID, acquisition sequence, byte offset, complete snapshot size, payload
size, protocol version 1, CRC32. CRC covers the entire header and payload with
the CRC word zeroed. The complete snapshot retains FRAME7's existing payload CRC.

Replies are at most 480 bytes, below a high-speed bulk packet's 512-byte maximum.
At full speed, a reply spans multiple 64-byte packets. The existing transport
sends a terminating zero-length packet when needed. Only one reply can be in
flight; controls and cancellation run after a chunk has been fully consumed.
This design avoids an unbounded queue and requires no extra endpoints or IRQs.

Firmware owns a separate 4,124-byte pinned snapshot. Legacy snapshot commands,
settings changes and new acquisitions cannot modify that copy. Bus reset,
configuration and endpoint transaction abort invalidate it. A transfer error
must be diagnosed/recovered before issuing further commands; never reinterpret
unreported bytes or send cancellation into an unfinished reply.

Python clients use `SlopScope(bounded_frames=True)` for the existing `frame()`
API, or consume `frame_chunks()` to receive `(offset, bytes)` incrementally.
Close the generator to skip the rest. Use a single calling thread; controls can
be issued between yielded chunks. The browser bridge opts in with
`slopscope-web --bridge --bounded-frames`. It abandons a frame when a control
arrives, applies the control, then requests a fresh snapshot. Python callers
can pass a cancellation callback to `frame(cancelled=...)` and handle
`hscope.frame_transfer.FrameSkipped`. Direct WebUSB remains on FRAME7.

The diagnostic reader uses persistent anonymous mappings, with each receive
buffer on private pages and a separate retained allocation for each read size.
This isolates receive memory from small command allocations and makes failed
buffer evidence easier to interpret. It is enabled by failure capture, not by
bounded transfers alone. High-speed failures still occurred with this allocation
strategy and with 480-byte chunk replies. Neither change fixed the bus error.

Initial hardware checks verified chunk CRCs and explicit cancellation, including
interleaved commands and fresh frames after cancellation. A full-speed run
completed 100 legacy and 100 chunked frames plus ten cancellations without an
error. High-speed runs failed during chunked transfers both before and after
changing FIFO writes to the recovered stock driver's word-plus-tail pattern.
The word writes reduce MMIO operations but are not a demonstrated bug fix.
This short full-speed comparison does not establish a root cause or qualify
long-term reliability. The protocol remains opt-in while diagnosis continues.
