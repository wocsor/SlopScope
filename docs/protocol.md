# USB protocol

## Transport

| Item | Value |
| --- | --- |
| VID:PID | `DEAF:BABE` |
| Configuration / interface | `1` / `0` |
| Bulk OUT / IN | `0x01` / `0x81` |
| Endpoint packet size | 512 bytes at high speed; 64 at full speed |

Commands are case-sensitive ASCII ending in `\n`, at most 128 bytes before the
newline. Text replies end in `\n`; failures start with `ERR `. Command numeric
arguments are decimal. Status fields normally use eight hexadecimal digits;
signed fields use 32-bit two's complement.

Use one client and one outstanding command. Consume the complete reply before
sending another command. USB reads can split a reply or return a zero-length
packet; binary lengths come from their headers, not newline or packet boundaries.
Never replay a command whose delivery is uncertain after an I/O error.

## Core commands

| Command | Purpose |
| --- | --- |
| `PING` | Replies `PONG` |
| `INFO`, `CAPABILITIES` | Firmware identity and supported formats/features |
| `USB SPEED`, `USB STATE` | Link speed and transport state |
| `BOOT STATUS`, `SETTINGS STATUS`, `FACTORY STATUS` | Boot, saved settings and factory-record state |
| `REBOOT FEL`, `REBOOT NORMAL` | Replies `OK REBOOT FEL` / `OK REBOOT NORMAL`, then reboots |
| `LIVE ON`, `LIVE RUN`, `LIVE OFF`, `LIVE STATUS` | Acquisition control; `LIVE RUN` clears a latched Single state |
| `CAPTURE CONFIG`, `CAPTURE RATE <index>` | Sample clock; indices 1–31, e.g. 10 selects 500 kS/s |
| `ROLL MODE LIVE`, `ROLL MODE TRIGGERED`, `ROLL STATUS` | Slow-acquisition behavior |
| `CH1 STATUS`, `CH1 VIEW` | Channel hardware/display state; use CH2 for channel 2 |
| `CH1 RANGE <index>` | Voltage range; the stock baseline supports indices 3–11 |
| `CH1 POSITION <mdiv>` | Signed vertical position in thousandths of a division |
| `CH1 PROBE <factor>` | Probe factor: 1, 10, 100 or 1000 |
| `CH1 COUPLING DC`, `CH1 COUPLING AC` | Input coupling |
| `CH1 DISPLAY ON/OFF`, `CH1 INVERSE ON/OFF`, `CH1 BANDWIDTH ON/OFF` | Channel options; choose one value after the command |
| `TRIGGER ENABLE ON/OFF`, `TRIGGER SOURCE CH1/CH2` | Enable and select trigger input |
| `TRIGGER SLOPE RISE/FALL`, `TRIGGER LEVEL <uv>` | Edge polarity and signed trigger level in microvolts |
| `TRIGGER SWEEP AUTO/NORMAL/SINGLE` | Trigger sweep |
| `TRIGGER HOLDOFF <ns>` | Holdoff, 100–1,000,000,000 ns |
| `TRIGGER SINGLE`, `TRIGGER FORCE`, `TRIGGER STATUS` | Arm Single, force a waiting capture or inspect state |
| `RECORD DEPTH 1000/10000`, `RECORD START CH1/CH2/BOTH` | Choose record length and start capture |
| `RECORD STATUS`, `RECORD CANCEL` | Poll `ready` or cancel an active record |

Slash-separated alternatives above are not literal wire text. Read configuration
back after changes: analog changes can be asynchronous. The full command grammar
is in [firmware/protocol](../firmware/protocol/). Range indices 0–13 correspond to
1, 2, 5, 10, 20, 50, 100, 200, 500 mV/div, then 1, 2, 5, 10, 20 V/div.
Rate indices are defined in [sample_rates.h](../firmware/tables/sample_rates.h);
`CAPTURE CONFIG` reports the active numerator/denominator in samples per second.

## Session and LCD USB indicators

Send `SESSION OPEN` and consume `OK SESSION OPEN`. A consumed reply confirms a
listening host and lights the LCD USB icon. Successful replies renew a three-second
lease; an idle client can send `SESSION PING` more frequently than that, with the
same reply. `SESSION STATUS` reports `active`, `tx`, `rx` and `lease_ms`.

`SESSION CLOSE` replies `OK SESSION CLOSED` and clears the icon. Disconnect,
reset and configuration changes also clear it. TX means scope-to-host and RX
means host-to-scope; activity stays visible for 250 ms. Sessions indicate presence,
not access control, and do not stop acquisition when they expire.

## Live snapshots

`FRAME7` returns the latest completed snapshot, or `ERR no frame`. The binary
reply has a 128-byte header and 3,996 payload bytes: 1,998 signed 8-bit CH1 samples,
then 1,998 CH2 samples. Every header field is a little-endian 32-bit word.

| Word | Value |
| --- | --- |
| 0–3 | Magic `0x31435348` (`HSC1`), version 7, header bytes 128, payload bytes 3996 |
| 4–7 | Sequence, legacy nominal rate, capture ticks, CSI flags |
| 8–11 | CH1/CH2 range indices, CH1/CH2 coupling (0 DC, 1 AC) |
| 12–15 | ADC mode, packed DAC offsets (CH1 low 16 bits), rate index, payload CRC32 |
| 16–19 | Acquisition mode (0 Sample, 1 Peak, 2 Average), average count, bandwidth bits, trigger family |
| 20–23 | Signed horizontal offset in ns, forced flag, trigger mode, capture epoch |
| 24–27 | Flags, valid-start sample index, valid sample count, snapshot ticks |
| 28–31 | Sample-rate numerator, denominator, CH1/CH2 calibration identifiers |

Use words 28/29 for the actual rate, including fractional rates. Timestamps are
24 MHz counters with wraparound. CRC32 uses the reflected IEEE polynomial
`0xedb88320`, initial value `0xffffffff` and final XOR `0xffffffff`.

Word 24 bits: 0 sampled Pulse, 1 roll, 2 filling, 3 rolling preview, 4–6 channel
alignment, 7 alignment valid. Serialized samples are already aligned; do not shift
CH2 again. Respect the valid window while filling. Use the captured range/DAC and
matching calibration when converting codes to voltage, rather than newer settings.

## Chunked snapshots

When `CAPABILITIES` advertises `frame_chunks=00000001`:

1. `FRAME OPEN` pins a FRAME7 snapshot and replies
   `FRAME id=XXXXXXXX bytes=XXXXXXXX sequence=XXXXXXXX chunk=000001C0`.
2. Convert the hexadecimal ID to decimal. Send `FRAME READ <id> <offset>` with
   decimal offsets, starting at zero and advancing by the returned payload length.
3. Reassemble the snapshot, validate its CRC and send `FRAME CLOSE <id>`; reply `OK`.

Each read returns a 32-byte header followed by up to 448 bytes. Its eight
little-endian words are magic `0x314b4843` (`CHK1`), ID, sequence, offset, total
snapshot bytes, payload bytes, version 1 and CRC32. The chunk CRC covers header
and payload with its CRC word zeroed. A high-speed reply is at most 480 bytes.

The snapshot stays fixed while acquisition continues. Controls may run between
fully consumed chunks. Close early to skip the rest, or OPEN a fresh snapshot.
A new OPEN, bus reset, configuration change or transaction abort invalidates the
old ID. Repeated reads of the same valid ID/offset return the same data.

## Records

After `RECORD STATUS` reports `ready`, `RECORD FRAME CH1` or `RECORD FRAME CH2`
returns a version-4 record: a 108-byte header followed by `depth` signed 8-bit
samples. Only requested/captured channels are available. Header words:

| Word | Value |
| --- | --- |
| 0–3 | Magic `0x31525348` (`HSR1`), version 4, header bytes 108, sample bytes/depth |
| 4–7 | Sequence, legacy nominal rate, completion ticks, CSI flags |
| 8–11 | Channel (0 or 1), range index, coupling, DAC offset |
| 12–15 | Depth, trigger mode, signed horizontal ns, sample-payload CRC32 |
| 16–19 | Rate numerator, denominator, forced flag, capture epoch |
| 20–23 | Programmed pretrigger/posttrigger counts, armed ticks, completed ticks |
| 24–26 | Sampled-Pulse flag, source channel, calibration identifier |

Programmed pretrigger counts are not an exact measured trigger position. Peak
samples are extrema with unverified chronological ordering. Preview snapshots
and finite records are not a continuous lossless stream. Older FRAME/record
formats remain in the firmware for compatibility; new clients should use the
formats above. Binary serialization is in [frame.c](../firmware/acquisition/frame.c)
and [record.c](../firmware/acquisition/record.c).
