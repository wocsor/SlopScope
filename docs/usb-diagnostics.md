# USB failure capture

The November 12 failure left the LCD/acquisition running while both PyUSB and
macOS IORegistry no longer enumerated the scope. Earlier PING replies were
followed by generic libusb I/O errors, even for short replies. A host USB reset
did not restore reliable communication. The last successful USB STATE is a
cached pre-failure snapshot; it is not evidence of the final endpoint state.
No controller/PHY root cause has been established.

The installed application has no UART logger or serial command interpreter.
It cannot be instrumented retroactively after USB disappears. Preserve host logs
and visible symptoms before a planned reset, then capture a fresh run with the
opt-in RAM image. Normal application output remains byte-identical when this
instrumentation is disabled.

## Wiring and receiver

Use the scope's existing debug TX pad (UART0/PF4 in the recovered board notes),
ESP32 GPIO18 as RX, and a common ground. Leave the scope UART RX disconnected.
GPIO17 is the CH2 pulse output and must not be reused as UART TX. The receiver
uses no TX pin and preserves the independent GPIO16/17 pulse generators.

The external bench receiver firmware accepts `UART ON`, `UART OFF` and `UART STATUS`.
Forwarding is on at startup. Complete lines are prefixed `SCOPE `; status reports
received bytes and dropped lines. Output is bounded and drops lines when the PC
serial queue is full. GPIO18 is pulled up, and partial/non-newline input still
increments the byte counter. Its lack of complete text is not a wiring test.
The overnight recorder enables `UART AUTO ON` through the receiver's PC console
so it can follow 57,600/115,200 text output across runtime and boot. This never
transmits on the scope UART. Raw-byte and manual-baud commands are documented in
the receiver's own README, maintained outside this repository. The bench setup was verified decoding at
57,600; the scope-side clock/divisor calculation still needs investigation.

## Arm before resetting the scope

The UART capture option needs pyserial in addition to the normal build tools.
The monitor avoids the USB lock, so it can stay armed through FEL loading:

```sh
.venv/bin/python -B tools/diagnose_usb.py --uart-only \
  --uart /dev/cu.usbmodem5ABA0686371 --seconds 3600
```

The tool timestamps raw received bytes, complete lines and decoded snapshots.
Keep another monitor from opening the same serial device. It never toggles the
scope reset or sends characters to the scope UART.

Prepare the diagnostic image, put the scope in FEL, then load it:

```sh
.venv/bin/python -B tools/build.py --ram --usb-diagnostics
.venv/bin/python -B tools/load.py --usb-diagnostics
```

This variant goes in `build/diagnostic-ram/`, uses volatile settings and cannot
be selected as a cold install. It changes neither scope flash nor saved factory
records. Confirm valid UART snapshots before starting failure reproduction.
The GPIO18 receiver has captured valid snapshots from this hardware at 115200 baud.
A power cycle returns to the installed normal firmware.

## Overnight recording

Arm the overnight recorder before loading the RAM build:

```sh
.venv/bin/python -u -B tools/overnight_usb.py --hours 12 \
  --uart /dev/cu.usbmodem5ABA0686371 --output build/overnight/session-name
```

Run it detached for an unattended session and keep the host awake for that
duration. The output directory records a fixed deadline; restarting with the
same directory resumes that deadline and appends evidence. `status.json` gives
recorder health, last firmware uptime, device enumeration and viewer state.
`events.jsonl` records boot text, uptime resets, receiver disconnect/reconnect,
recorder process exits and USB failure metadata. UART and kernel logs are saved
separately. UART reopens after serial disconnects, and child recorders restart
if they exit. Cursor/status files are replaced atomically.

The recorder enumerates USB devices and reads the viewer HTTP state; it does
not claim scope endpoints or send recovery/reset commands. Run the workload
viewer with `--no-usb-recovery --capture-usb-errors <directory>` so its first
error remains intact. A scope reboot loses the RAM diagnostic build and boots
the installed application. Recording continues to preserve BOOT0/BOOT1 text;
the recorder does not reload firmware, restart the viewer or reflash the unit.

For UART-only captures, `diagnose_usb.py --uart-only --reconnect-uart` accepts
up to 24 hours. Active USB probe runs remain limited to one hour.

## Preserve the first error

Start the Python viewer with recovery disabled for this capture:

```sh
slopscope-web --bridge --no-browser --no-usb-recovery
```

On a USB error or timeout it records the failure and pauses instead of resetting
or reopening USB. The HTTP server remains available. WebUSB also leaves failed
sessions for explicit user reconnection. Avoid automatically retrying a command
whose delivery is uncertain.

After the bulk client releases USB, capture EP0 and bulk observations:

```sh
.venv/bin/python -B tools/diagnose_usb.py --seconds 30 --vendor-snapshot
```

Default probes read standard descriptors/configuration/endpoint-halt state and
issue ECHO, PING and status commands. After a bulk failure they stop bulk probing
and continue control observations, without clearing halts, changing configuration
or resetting. `--control-only` leaves bulk endpoints untouched. Neither mode
writes scope settings. Stop other clients before probing.

Interpret new observations rather than treating the old soak result as proof:

- UART loop/sequence progress with USB enumeration lost narrows the failure to
  link/controller/host attachment, rather than a stopped acquisition loop.
- EP0 snapshots succeeding while bulk reads fail distinguish endpoint/pipe or
  transaction state from complete USB inaccessibility.
- Increasing sequence with unchanged pending IN data can identify a host that
  stopped consuming replies; reset/disconnect counters distinguish bus events.
- Compare the same RAM image at full speed and high speed, and repeat with a
  known working cable/port only after preserving the first failure's evidence.

## Snapshot layout

`SCOPE DBG USB` is followed by 31 eight-digit hexadecimal words and the UART
snapshot-drop count. The host tool names/decodes these fields in
`DIAGNOSTIC_FIELDS`. They include uptime, main-loop count, acquisition sequence,
capture/boot stage, power/configuration/MPS, EP0/TX/RX status, queued bytes,
poll gaps, bus counters, DEVCTL/PHYCTL and last bus/setup/command tags.

The diagnostic image also supports vendor IN request `0xc0/0x5a`, wValue/wIndex
zero, length 124. It returns the same 31 little-endian words: magic `0x44425355`,
version 1. The trailing UART drop count is not part of that reply. Normal builds
stall this request. Snapshot reads preserve the selected endpoint index and do
not read the controller's interrupt-status registers. UART output is queued and
polls a bounded FIFO write without waiting for space.

Register references: the upstream Linux [suniv pin controller](https://github.com/torvalds/linux/blob/master/drivers/pinctrl/sunxi/pinctrl-suniv-f1c100s.c),
[clock/reset driver](https://github.com/torvalds/linux/blob/master/drivers/clk/sunxi-ng/ccu-suniv-f1c100s.c),
[UART device description](https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/allwinner/suniv-f1c100s.dtsi)
and [MUSB register/interrupt handling](https://github.com/torvalds/linux/blob/master/drivers/usb/musb/sunxi.c).
These establish register definitions, not the cause of this unit's failure.

## Event trace around a failure

The RAM diagnostic build keeps the most recent 64 USB events in a 4 KiB ring.
Each event records a 24 MHz timestamp, event number, arguments, EP0/TX/RX state,
RX byte count, power/DEVCTL, capture sequence, command tag and packet number.
Events cover RX packets, parsed commands, FIFO writes, TX arming, TXPKTRDY
clearing, bus flags, setup requests and transaction aborts. IRQ flags are passed
from the driver's existing read; tracing does not reread interrupt-status
registers, consume FIFO data or clear controller flags. Indexed reads preserve
the endpoint selector. The extra MMIO reads and checksums affect diagnostic
timing, so normal firmware excludes these hooks.

The ring freezes on a packet still pending after 500 ms, or suspend/reset/
disconnect while software tracks an outstanding packet. This is a diagnostic
trigger, not proof of a controller fault: a host which stops reading can cause
the same pending state. The transport is not reset, retried or aborted by the
trigger. The frozen ring is preserved through subsequent USB recovery events.

UART drains the dump without blocking, reserving queue space for the ordinary
heartbeat. `DBG FAULT` reports the trigger, counts and watchdog control registers;
`DBG CMD` contains the last command, with nonprintable bytes replaced by dots;
`DBG FROZEN` preserves the original packet trace. `DBG EVT` emits the ring from
oldest to newest. Structured field names and event names are in
`tools/diagnose_usb.py`. Event arguments depend on the kind: FIFO_WRITTEN stores
length/CRC/reply offset; BUS_FLAGS stores bus/TX/RX IRQ flags; SETUP stores the
request tag and index/length. TX_READY_CLEARED describes the observed bit change,
not a separately measured successful host read.

Once a second `DBG HEALTH` records raw CPU/peripheral PLL and bus clock setup,
watchdog configuration/mode, cached UART clock/divisor and transmitted UART bytes.
These values can distinguish a register change from an otherwise steady loop;
they do not measure supply voltage or identify a reset cause by themselves.

`DBG LINK` reports the read-only USB frame register, SOF observations, frame
change observations, their ages in milliseconds, POWER and DEVCTL. Counts are
poll observations, not exact counts of frames on the wire. Ages use the 24 MHz
timer and wrap after about 179 seconds without an observation. This adds no
interrupt-status reads: SOF uses the driver's existing bus flags. Continued frame
progress distinguishes an endpoint-only failure from stopped USB frame traffic.

The PC's libusb errno and macOS transaction result exist on the host, so those
remain in viewer/kernel logs. UART records the device side independently, with
event timestamps allowing a much finer comparison than the one-second heartbeat.
UART receive time can lag an event while its dump drains. A stopped CPU or power
loss can also prevent a queued dump from reaching UART. Reload the RAM image to
arm a new first-trigger trace; it does not automatically overwrite a frozen one.

Diagnostic request page 1 (wValue 1, length 96) reports UART setup and FIFO state,
including configured clock/divisor, transmit bytes, queue/drop counts, GPIOF
mux and clock/reset registers. It lets the host distinguish a UART logger that
isn't transmitting from missing receiver input. Names are in the host tool's
`UART_DIAGNOSTIC_FIELDS`.

Page 3 (wIndex zero, length 96) reports the last queued packet: queue/consumption
counters and times, reply offset/length, packet length, source CRC and first 16
bytes, endpoint FIFO sizes/addresses, MAXP/CSR values and SRAM ownership. The
source signature is captured after PIO writes and before TXPKTRDY. It does not
prove the FIFO contains those bytes or that the host acknowledged them.
`DBG TX` UART lines report the same 24 words plus the dropped-line count, so
the trace survives failure of EP0. Field names are `PACKET_DIAGNOSTIC_FIELDS`.

Page 2 returns up to 124 bytes of the last serialized FRAME response, with
wIndex selecting the byte offset. It reads the saved response without starting
a new capture or consuming bulk data. `tools/inspect_usb_frame.py` downloads it,
checks its CRC and compares it with captured host data. Pages above 3 are
rejected; only page 2 accepts a nonzero wIndex.

## November 12 observations

During a RAM diagnostic failure, acquisition and UART continued advancing.
EP0 remained responsive, neither bulk endpoint was halted, and an IN packet
remained queued. Reading the pending reply later succeeded without a reset.
This distinguishes an unfinished bulk transaction from a stopped CPU.

One failed 4096-byte host read reported zero bytes, although its native buffer
exactly matched the first 4096 bytes of the CRC-valid saved frame. Native buffer
contents from failed transfers are evidence only; they are not valid received
data and must never be accepted as a successful frame.

A separate run using 512-byte host reads also failed with macOS reporting
`device not responding` (`0xe00002ed`). That run followed a user-confirmed power
cycle into the installed firmware, so it has no live UART register snapshots
or saved-frame comparison. Smaller reads have not eliminated the failure.
The root cause remains unresolved; these observations do not establish a cable,
PHY, controller or host-driver fault.

A later instrumented run also failed on EP0. UART recorded a new suspend event,
no new reset/disconnect, and an unfinished FRAME7 reply with only its first
512-byte packet queued. The host error arrived about 11 ms after the command,
well before its timeout. The firmware setup count did not advance during failed
control probes. Host-cached enumeration/descriptor data is not proof of live EP0.
The failed native buffer included the outgoing command prefix; it remains
untrusted because the OS reported no valid received bytes. A subsequent host USB
reset restored communication, after which marker-based synchronization succeeded.

See [bounded frame transfer](bounded-frame-transfer.md) for the optional
ID/offset/CRC protocol and host buffer-isolation experiment. Compare legacy and
bounded transfers separately before attributing an improvement to either change.

## November 14 investigation

The November 13 high-resolution trace captured suspend before the failed chunk
was armed: the previous packet's TXPKTRDY cleared, suspend was observed about
5.56 ms later, and the next reply was armed about 60.46 ms after that observation.
All 64 events arrived with zero reported drops. Acquisition continued afterward.
This narrows the sequence but does not identify what interrupted the link.

A subsequent failed session was connected/enabled and not suspended according
to macOS GetUSBDeviceInformation. Explicit resume and suspend/resume requests
did not restore PING. One USB bus reset did restore it, allowing a software FEL
handoff and verified RAM-only diagnostic upload. On the new RAM image a planned
suspend/resume request produced device suspend/resume counters and PING still
worked. This does not reproduce or fix the intermittent failure.

`tools/darwin_usb_power.c` provides `status`, `suspend`, `resume` and `cycle` on
macOS. Status queries host state without opening the USB device or issuing
descriptor requests; mutations explicitly open the device without seizing it.
A successful suspend API result alone is not proof of a suspended wire state.
Build it with clang and the IOKit/CoreFoundation frameworks. The overnight
recorder's optional `--host-power-monitor` accepts its executable path and writes
timestamped results to `host-power.jsonl`.

The recorder now inventories devices through IORegistry properties instead of
PyUSB enumeration. The installed libusb 1.0.30 Darwin descriptor-cache path can
open/seize a device and change suspend state during error retries. Its use in
an observer was an avoidable source of interference, not an established cause
of the original USB bug. See the
[libusb Darwin implementation](https://github.com/libusb/libusb/blob/v1.0.30/libusb/os/darwin_usb.c).
Only the intended workload client now uses libusb during recording. Normal
firmware excludes the added link telemetry; flash and calibration are unchanged.
