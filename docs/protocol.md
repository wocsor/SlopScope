# Talking to SlopScope over USB

This is SlopScope's own bulk protocol, not the stock USBTMC/SCPI interface. It
controls the scope and downloads captures. Math doesn't change what comes over
the wire: you still get both raw channels, and a host can calculate its own result.

## How do I connect?

| Item | Value |
| --- | --- |
| VID:PID | `DEAF:BABE` |
| USB serial | `SS-` followed by the flash chip's read-only unique ID as 32 hexadecimal characters |
| Configuration / interface | `1` / `0` |
| Bulk OUT / IN | `0x01` / `0x81` |
| Endpoint packet size | 512 bytes at high speed; 64 at full speed |

Commands are case-sensitive ASCII with a `\n` at the end. The limit is 128 bytes
before that newline. Text replies also end in `\n`; an error starts with `ERR `.
Send command arguments in decimal. Status replies normally use eight hexadecimal
digits, with signed values encoded as 32-bit two's complement.

Use one client per scope and send one command at a time. Read the whole reply
before sending the next command. A USB read isn't necessarily a whole reply: it
can split one, and you can get a zero-length packet. For binary replies, use the
length in the header instead of looking for a newline or counting USB packets.
If a transfer fails, don't blindly resend a command that might already have run.
Read the state back once the connection is working again.

Select a scope by its USB serial when more than one is connected. The serial
comes from the flash chip's read-only unique ID, so it survives firmware/settings
updates even if two scopes have identical stock identity records. A failed UID
read produces `UNAVAILABLE`; don't use that to pick an arbitrary unit when there
are duplicates.

## What can I send?

| Command | Purpose |
| --- | --- |
| `PING` | Replies `PONG` |
| `INFO`, `CAPABILITIES` | Firmware identity and supported formats/features |
| `USB SPEED`, `USB STATE` | Link speed and transport state |
| `DISPLAY STATUS`, `DISPLAY TIMING` | LCD frame/present state and renderer stage costs in 24 MHz ticks (clear, erase, traces, annotations, measurements, tools, overlay) |
| `CPU STATUS` | CP15 control register and raw CPU clock/PLL registers; bit 12 is instruction-cache enable |
| `UI REVISION` | Control revision and persistence/Autoset state; unchanged replies allow skipping a full settings read |
| `BOOT STATUS`, `SETTINGS STATUS`, `FACTORY STATUS` | Boot, saved settings and factory-record state |
| `REBOOT FEL`, `REBOOT NORMAL` | Replies `OK REBOOT FEL` / `OK REBOOT NORMAL`, then reboots |
| `LIVE ON`, `LIVE RUN`, `LIVE OFF`, `LIVE STATUS` | Acquisition control; `LIVE RUN` clears a latched Single state |
| `DISPLAY BRIGHTNESS`, `DISPLAY BRIGHTNESS <percent>` | Query/set backlight 1–100%; zero is rejected. Utility top softkey selects Backlight for editing; the knob adjusts in 1% steps until pressed again |
| `DISPLAY THEME`, `DISPLAY THEME DARK`, `DISPLAY THEME LIGHT` | Query/set the saved LCD theme; also available under Utility → Theme |
| `PANEL STATUS`, `PANEL TOOLS` | Physical events/errors and LCD cursor/Math state |
| `AUTOSET START`, `AUTOSET STATUS`, `AUTOSET CANCEL` | Non-blocking range/timebase selection and hardware-edge search |
| `CAPTURE CONFIG`, `CAPTURE RATE <index>` | Sample clock; indices 1–31, e.g. 10 selects 500 kS/s |
| `ROLL MODE LIVE`, `ROLL MODE TRIGGERED`, `ROLL STATUS` | Slow-acquisition behavior |
| `CH1 STATUS`, `CH1 VIEW` | Channel hardware/display state; use CH2 for channel 2 |
| `CH1 RANGE <index>` | Voltage range; the stock baseline supports indices 3–11 |
| `VOLTAGE BASELINE`, `VOLTAGE BASELINE STOCK/REFERENCE/ZERO` | Query/select voltage conversion: stock nominal gain, saved reference gains, or stock gain with saved zero correction |
| `HORIZONTAL STATUS`, `HORIZONTAL OFFSET <ns>`, `HORIZONTAL STOCK` | Query/set signed acquisition delay or restore stock placement; offset must fit the current capture window and ±1 second |
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

The slashes in the table mean alternatives, not literal command text. For example,
send `CH1 DISPLAY ON`, not `CH1 DISPLAY ON/OFF`. Read the configuration back after
a change; analog settings can take time to apply. The full command grammar is in
[firmware/protocol](../firmware/protocol/). Range indices 0–13 correspond to
1, 2, 5, 10, 20, 50, 100, 200, 500 mV/div, then 1, 2, 5, 10, 20 V/div.
Rate indices are defined in [sample_rates.h](../firmware/tables/sample_rates.h);
`CAPTURE CONFIG` reports the active numerator/denominator in samples per second.

Autoset keeps the selected voltage baseline. It starts from the current record
and looks for a usable channel, range and timebase; it doesn't search every
possible setup, and it doesn't calibrate voltage gain. `STOCK` uses the stock
baseline, `REFERENCE` uses saved reference corrections, and `ZERO` keeps nominal
stock gain with a saved zero correction.

Horizontal delay applies to later acquisitions. Zero keeps the stock trigger
placement; full records have their own centered pre/post-trigger layout. Both
UIs show the effective delay as `H:`. Live roll temporarily uses zero delay but
keeps the saved value for later, so an old horizontal offset doesn't make a slow
rolling view wait for a full capture.

## When does the LCD USB icon light up?

Send `SESSION OPEN` and read `OK SESSION OPEN`. Reading that reply confirms that
a host is actually listening, which lights the LCD USB icon. A cable plugged in
by itself isn't enough.

Successful replies renew a three-second lease. If the client is idle, send
`SESSION PING` before that expires; it has the same reply. `SESSION STATUS`
reports `active`, `tx`, `rx` and `lease_ms`. `SESSION CLOSE` replies
`OK SESSION CLOSED` and clears the icon. Disconnects, resets and USB configuration
changes clear it too.

TX is scope-to-host and RX is host-to-scope. Each activity indicator stays lit
for 250 ms so you can see it. The session is a presence indicator, not a lock,
and acquisition keeps running if the lease expires.

## Getting a live capture

`FRAME7` gives you the latest completed snapshot, or `ERR no frame` if there isn't
one yet. It has a 128-byte header followed by 3,996 payload bytes: 1,998 signed
8-bit CH1 samples, then 1,998 CH2 samples. Header fields are little-endian 32-bit
words. You can get the same sequence twice or miss sequences between requests;
this isn't a queue of every acquisition.

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

## Reading a snapshot in chunks

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

The snapshot stays fixed while the scope keeps acquiring. You can send controls
between chunks once you've read each chunk's entire reply. Close early if you
want to skip the rest, or open a new snapshot for fresher data.

A new OPEN, bus reset, configuration change or transaction abort invalidates the
old ID. Reading the same valid ID and offset again gives you the same data.

## Getting a longer record

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

The programmed pretrigger count isn't a measurement of the exact trigger
position. Peak-mode samples are extrema, and their time ordering hasn't been
verified. Neither these records nor live previews form a continuous lossless stream.

Older FRAME/record formats are still there for compatibility. Use the versions
above for a new client. The serializers are in [frame.c](../firmware/acquisition/frame.c)
and [record.c](../firmware/acquisition/record.c).

## Buttons, menus and display tools

`PANEL STATUS` reports events/errors, `menu`, zero-based `selected`, `editing`,
and a one-based `page` (zero when closed). Clockwise moves forward or increases
an edited value. Press selects or finishes editing. Each page has up to four
items and Back on the fifth softkey; turn the knob to reach the next page.
Back finishes editing, returns to the parent menu, or closes a root menu.

Backlight only changes once selected and stays between 1–100%. Cursor/Math
position and scale, and trigger holdoff, use that same edit behavior. Run/Stop
and Force have their own buttons. Panel events 15/16 are anticlockwise/clockwise;
event 38 is Select.

`AUTOSET STATUS` uses these state values:

| Value | State |
| --- | --- |
| 0 | Idle |
| 1–3 | Centering, scaling, searching for an edge |
| 4 | Complete |
| 5–6 | No clear signal, no stable edge |
| 7–8 | Unavailable/error, cancelled |
| 9–10 | Checking a fresh capture, still clipped at the widest supported range |

The source is zero-based. Panel input cancels an ongoing Autoset search. USB
remains available while it runs.

`PANEL CURSORS <mode> <source> <selected> <xa> <xb> <ya> <yb>` sets the LCD
cursors together. Mode is 0 Off, 1 Time, or 2 Voltage. Source is 1 for CH1 or 2
for CH2; selected is 0 for A or 1 for B. Horizontal positions are plot-relative
pixels 0–749; vertical positions are LCD rows 40–439.

`PANEL TOOLS` reports the cursor and Math settings. These are session-only on the
scope; the web viewer can keep them in a setup profile.

## Math on the LCD and PC

`PANEL MATH <mode> <range> <probe> <position_mdiv> <reversed>` selects the LCD
Math trace. These are display controls; they don't replace the acquired channels.

| Mode | Operation |
| --- | --- |
| 0 | Off |
| 1 | CH1 + CH2 |
| 2 | CH1 − CH2, or CH2 − CH1 when reversed |
| 3 | FFT; use `PANEL FFT` below to set its options |
| 4 | CH1 × CH2 |
| 5 | CH1 ÷ CH2 |
| 6 | XY: CH1 on X, CH2 on Y |

Range is 0–13, probe is 1/10/100/1000, position is −4000..4000 millidivisions,
and reversed is 0/1. The `math_modes` bitmask in `PANEL TOOLS` advertises supported
modes using bits 0–6. Divide skips zero denominators. Multiply uses V² for scale
and position; Divide uses a dimensionless ratio. XY uses CH1 volts/div on X and
the Math scale/position on Y.

The web viewer calculates Math from the incoming raw channels. Its selections
also select the LCD operation, and reading back panel/LCD changes updates the
host calculation. That includes subtraction order and FFT source, window and
Vrms/dBV units. Arithmetic Math appears in magenta on the main waveform grid;
FFT and XY use the main graph. Changing Math also recalculates a stopped capture.

The scope still sends two raw ADC channels with calibration metadata. It doesn't
send screenshots or substitute Math/spectrum samples for those channels. The
host and LCD calculate their own results; different FFT lengths and window
sampling mean the spectra needn't be bit-identical.

### FFT

`PANEL FFT <source> <window> <logarithmic> <range> <db_div> <reference_dbv>` enables
LCD FFT. Source is 1 or 2. Logarithmic is 1 for dBV or 0 for linear Vrms. Range
uses the usual 0–13 index with the selected input's probe factor. The logarithmic
grid accepts 5–20 dB/div and a top-of-grid reference of −120..120 dBV.

| Window | Name |
| --- | --- |
| 0 | Hamming |
| 1 | Rectangle |
| 2 | Blackman |
| 3 | Hann |
| 4 | Kaiser, beta 6.5 |
| 5 | Bartlett |

`FFT STATUS` reports readiness, source/window/scaling, point count, strongest
non-DC bin, its amplitude in microvolts RMS, frame sequence, and transform time
in microseconds. Values use the usual hexadecimal status encoding. `PANEL TOOLS`
also includes `fft_source` (zero-based), `fft_window`, `fft_logarithmic`,
`fft_db_div` and signed `fft_reference_dbv`.

LCD FFT uses the latest 1024 contiguous valid samples. It removes DC before
windowing and corrects amplitude for the window's coherent gain. The span is DC
to Nyquist and the bin spacing is sample rate / 1024. A bin's amplitude isn't an
integrated broadband RMS measurement. Off-bin tones have leakage/scalloping, and
signals above Nyquist can alias.

During initial roll fill, LCD FFT waits for 1024 samples. It doesn't use deep
records. Time/voltage cursors and channel/trigger arrows are hidden in FFT and XY
because those markers belong to the time-domain view. Acquisition and raw USB
waveform transfer keep running. The LCD reuses FFT magnitudes between display
buffers; a changed frame, source, window, probe factor or calibration gain causes
a new transform. Local FFT still costs CPU time; see the [rate check](status.md#how-live-is-the-live-view).

On the panel, Math → Operation cycles OFF / ADD / SUBTRACT / FFT / MULTIPLY /
DIVIDE / XY. FFT has Source, Window, Units and Scale; Scale is on the next page.

## Saving settings

Stop acquisition and wait for any active capture/analog setup to finish before
sending `SETTINGS SAVE` or `SETTINGS LOAD`. `SETTINGS SAVE RUN` saves a setup that
will start in Run at the next boot; it doesn't itself start acquisition. Loading
is asynchronous, so read `SETTINGS STATUS` until restoration has finished.

`SETTINGS STATUS` reports journal base (`journal`), legacy migration state
(`migration`) and storage protection (`write_blocked`). Theme and brightness are
saved with the normal scope settings. Cursor/Math display settings aren't.

The journal uses two 4 KiB sectors at `0x517000` and `0x518000`, outside the stock
filesystem and beyond the release application payload. The first save can
initialize erased space or verified unused zero padding after a valid application.
Unknown content is protected; a blocked journal isn't retried during acquisition.
Older records at `0xffe000`/`0xfff000` can be read and migrated without modifying
those legacy sectors.

## Downloading a screenshot

Copy keeps one LCD snapshot in RAM. `SCREENSHOT CAPTURE` requests the same thing
over USB; `SCREENSHOT STATUS` reports `ready`, `busy`, `id`, `bytes` and `chunk`.
Rendering pauses for the bounded copy while acquisition continues. Nothing is
written to a USB drive, and a new capture replaces the previous snapshot.

`SCREENSHOT READ <id> <offset>` uses decimal arguments and returns up to 448
payload bytes following eight little-endian 32-bit header words:

| Word | Value |
| --- | --- |
| 0 | Magic `0x31534353` (SCS1) |
| 1–3 | Snapshot ID, byte offset, payload byte count |
| 4–6 | Total pixel bytes (768000), width (800), height (480) |
| 7 | CRC32 of header plus payload, with this word zeroed |

Pixels are top-down, row-major, little-endian RGB565, with 1600 bytes per row.
