# USB interface

The device uses vendor bulk USB, VID `DEAF`, PID `BABE`, interface 0, OUT `0x01`
and IN `0x81`. Endpoint packets are 512 bytes at high speed or 64 at full speed.
Link speed does not establish sustained application throughput.

Text commands are newline terminated ASCII, at most 128 bytes excluding the
newline. Text replies end with a newline. Status integers are commonly printed
as eight hexadecimal digits; command numeric arguments are parsed as decimal.
Clients must serialize requests and consume the complete reply before sending
the next command. Binary frame/record commands require their dedicated decoder.

| Command | Purpose |
| --- | --- |
| `PING`, `INFO`, `CAPABILITIES` | Discovery and feature limits |
| `USB SPEED`, `USB STATE` | Negotiated speed and transport diagnostics |
| `SESSION OPEN`, `SESSION PING`, `SESSION CLOSE`, `SESSION STATUS` | Application connection and LCD indicators |
| `REBOOT FEL`, `REBOOT NORMAL` | Return to loader or reboot installed firmware |
| `LIVE STATUS`, `LIVE ON`, `LIVE OFF`, `LIVE RUN` | Acquisition control |
| `ACQUISITION STATUS`, `CAPTURE CONFIG` | Capture state and geometry |
| `CH1 STATUS`, `CH2 VIEW` | Channel hardware and display state |

The exact channel and trigger grammar lives in `firmware/protocol/`. Binary
frame serialization lives in `firmware/acquisition/frame.c`; record serialization
lives in `firmware/acquisition/record.c`. `hscope` implements the compatible
host decoders and higher-level controls. Maintenance tools use `tools/device.py`
for text commands and share its exclusive client lock with the viewer.

FRAME4–7 preserve explicit channel alignment metadata: bit 7 marks alignment
valid, bits 4–6 carry its value. Live clean data compensates the observed
one-sample CH2 lag while preserving 1998 paired samples. Do not apply this
correction indiscriminately to raw records or other acquisition paths.

## Application session and activity indicators

`CAPABILITIES` advertises `session=00000001`. A viewer or library opens its
session with `SESSION OPEN`, then consumes the `OK SESSION OPEN` reply. The LCD
icon becomes connected only when the controller confirms the host consumed an
IN packet. Enumeration or a configured USB interface alone leaves it dark.

Successfully consumed replies renew a three-second lease. Normal live frames
and status polling provide this automatically, including while stopped. An idle
client can send `SESSION PING` at intervals shorter than three seconds; its reply
is also `OK SESSION OPEN`. After expiry the indicators clear, and a later consumed
reply reactivates the requested session. This is connection presence, not access
control. Acquisition continues independently of the session.

`SESSION CLOSE` replies `OK SESSION CLOSED` and immediately clears session and
activity state. Disconnect, bus reset and configuration changes also clear it.
Unexpected application termination is covered by lease expiry. Clients should
close gracefully while the transport is healthy; do not send another command
after an uncertain failed transfer merely to update the icon.

`SESSION STATUS` reports hexadecimal `active`, `tx`, `rx` and `lease_ms` fields.
Querying status itself creates receive/transmit activity. The LCD refreshes this
state every 50 ms; the next off-screen presentation includes the tile, including
while stopped. TX is green (scope to
host), RX amber (host to scope); each activity holds for 250 ms. Sustained
traffic can keep a lamp lit. All indicators are hidden without an active session.

`DISPLAY STATUS` includes `buffer` (the most recently queued framebuffer index)
and `pending` (the display backend has not finished latching it). These are
hexadecimal fields alongside the existing frame, sequence and draw-time values.
Status-only presentations can advance the display frame counter without a new
acquisition sequence.
