# SlopScope release readiness

## Current installed build — 2026-11-17

Application SHA-256:
`cccac2e9589a0d9255ec446880d444e00deb4fa45b4495c6bddc6802c1e19e6c`
(259,532 bytes). The update changed 25 application sectors, passed readback,
matched the retained unit identity and preserved the live settings journal.
Normal boot reached acquisition with settings record 52 restored, no boot/save
error, factory records loaded and a CRC-valid bounded waveform over 480 Mb/s USB.
The application and compressed install payload passed build/round-trip checks.
No automated suite ran.

The build removes the redundant READING BOOT LOGO message and includes the
current MAX readouts and bounded-transfer support. Normal firmware omits the
opt-in UART diagnostic implementation. Earlier observations below are historical
and refer to their stated build hashes and settings records.

The November 14 four-hour diagnostic recording finished with no captured USB
transfer fault, a command counter of 907745 and continuing acquisition. This does
not resolve earlier intermittent failures. Background inventory now avoids
libusb enumeration; UART adds USB frame/SOF observations for subsequent captures.

GitHub publication remains paused at the owner's request. The
[code provenance inventory](docs/provenance.md) now distinguishes recovered
hardware data from actual stock instruction copies. SlopScope implementation
code is MIT-licensed. LittleFS source/license hashes were verified upstream,
and build/update outputs carry the project license and full third-party notices.
ESP32 fixture source and its dependent Pulse observation tool remain in research,
outside this repository. Hardware usability and redistribution review are
separate: `RELEASE_READY.json` records `publication_ready: false`.

Status: **READY for an initial experimental release**, verified 2026-11-10.
The machine-readable flag is [RELEASE_READY.json](RELEASE_READY.json).

This release targets everyday scope operation and PC control. The previous
installed build was qualified for an initial experimental release. The source
cleanup preserves that functionality; it does not claim complete stock parity
or independent accuracy certification.

## Implemented

- Two-channel live acquisition, LCD traces and USB waveform streaming.
- Voltage/time scales, coupling, probe factors, voltage positions, visibility,
  inversion and bandwidth controls.
- Edge Auto/Normal/Single, Run/Stop/Force and trigger voltage controls.
- Stock-derived Video controls with PAL acquisition observed; sampled Pulse
  triggering as a CFW addition.
- Cooperative rolling previews, 1K/10K records and capture progress reporting.
- Autonomous boot with progress console, saved settings and unit-local factory
  calibration reused in place. Optional zero/reference calibration is separate.
- Measurements, cursors, math, FFT/XY and exports through the separate `hscope`
  library and web UI projects, subject to acquisition-mode limitations.

## Cleanup verification

The frontend was divided into subsystem modules, runtime state was grouped,
names and formatting were clarified, and tooling was moved into this repository.
Serialized settings/value structures were compared against the previous source
and retained their layouts. ThinLTO retains optimization across the modules;
compiler ABI helpers live separately in `firmware/libc`.

Both normal and volatile RAM images build with warnings treated as errors.
A separate copy of only source, assets, tools and vendored dependencies produces
the same cold image; no research workspace or host project is needed to build.

The RAM build reached acquisition, restored journal record 38 and retained
factory-table bytes and reference/zero calibration. FRAME7 CRC decoding and
one-sample alignment metadata passed. Both channels showed 500-sample periods
at 500 kS/s with the 1 kHz stimulus. Run/Stop and two Single→Run cycles passed.
LCD draw time was approximately 34 ms, compared with 33 ms before cleanup.
The installed image passed sector readback and normal ROM/bootloader boot,
restoring settings record 38 and the same calibration identities. Bounded live,
two-second roll and 10K record observations completed with USB answering; the
original controls and journal were restored unchanged. No automated test suite
was run.

Verified application SHA-256:
`ffe54a09effc21b5e5a35779780b1277ee282b3c0a04158a62edd24738f5ee1a`
(259,380 bytes). Installation changed 25 application sectors; no logo or other
flash regions changed. The exact build was checked through watchdog/ROM boot;
a physical power-off cycle was not repeated on this image.

Private verification evidence is retained under `build/verification/` and
`build/flash/installs/20261109T125730Z/`, both excluded from source publication.
Historical feature qualification remains in the separate research workspace.

## Remaining limits

- A historical 14.07-hour USB soak passed, but subsequent high-speed runs
  reproduced transaction errors with FRAME7 and bounded frame chunks. The USB
  root cause remains unresolved; that soak does not establish a fix. Full-speed
  comparisons passed, including an extended connected viewer run. See
  [USB diagnostics](docs/usb-diagnostics.md) for capture and recovery limits.
  Two later CPU boots followed host USB-C port power events; the captured logs
  do not establish their reset cause or clear the separate transaction failure.
- Programmed pretrigger counts are not exact measured trigger indices.
  Absolute high-rate phase corrections and broader interpolation need further
  qualification; period and relative-offset checks have separate evidence.
- PAL acquisition has been observed. NTSC/SECAM, absolute odd/even field
  identity and broader Video cases remain unverified.
- Hardware Peak reports extrema with unverified rolling sample interpretation.
  Use SAMPLE for chronological timing. The host suppresses Peak timing/RMS/FFT.
- Native Slope/Runt/Spike/Timeout/Nth-edge modes remain experimental and are
  disabled in the browser. Stock controls observed on this model expose Edge
  and Video. Sampled Pulse is unavailable in unverified Alternate mode.
- Reusing factory records provides the stock baseline; it does not certify a
  unit's absolute accuracy. External-reference calibration remains optional.
- Physical buttons/dials are outside the deployed hardware configuration.

## Repository boundary

Only CFW source, assets, development utilities and documentation belong in this
repository. The Python library and web UI have separate repository boundaries.
Stock dumps, analysis, full images, unit identity/calibration data, captures and
backups stay outside the source repositories or in ignored build directories. LittleFS retains its upstream license and integration notice.
No GitHub remote or publication is performed by this cleanup.

See [README.md](README.md) for build/load/install commands. Installation uses a
backup from the target unit, verifies changed-sector readback and preserves
bootloaders, identity, FPGA, factory filesystem and the settings journal.

The installed image includes SlopScope identification, the naming cleanup and
a USB symbol and separate TX/RX lamps in the upper-right LCD corner. They follow
an application session confirmed by a consumed reply, rather than enumeration.
They refresh while stopped as well as during acquisition.

## Naming follow-up

Register access uses `MMIO8`/`MMIO16`/`MMIO32`, and owned hardware helpers,
locals and parameters use descriptive names. Public headers name their
parameters. Standard C/ARM ABI names and conventional loop/coordinate names
remain intact. Naming conventions are recorded in `AGENTS.md` and
[the source conventions](docs/style.md).

Compiler AST comparison covered all 236 function definitions in 45 owned C
files and found unchanged operations and variable bindings. The cold image is
byte-identical to the pre-rename source build, including the pending SlopScope
INFO label: SHA-256
`4d950037c2c5fc9a65fc4b4332bf0d2c97626cc1f452575e6a929e58f5407fda`.
Cold, RAM and DRAM-helper builds pass. The codec preserves its previous
compressed output. The later USB-indicator installation includes this naming follow-up; the
verified installed image above is the current hardware baseline.

## PC project separation and WebUSB

The Python library and web UI are separate repositories and installable packages.
The library wheel contains no browser assets or HTTP server. The web UI ships as
static JavaScript/HTML with an optional Python bridge importing `hscope` normally.
Direct WebUSB streamed real high-speed FRAME7 data in Chrome on macOS with valid
CRC and calibration identity. Browser measurements and FFT agree with the Python
implementation on the checked records. Extended direct-control UI observations
still require the browser's device grant; they are not claimed complete here.
Grounded self-calibration remains available through the Python bridge.

The USB-indicator update passed flash readback, normal boot, settings/calibration
comparison and a matching source-only rebuild. Settings record 38 was restored
with its original controls and no pending save. The indicator's connected and
clear states were also observed by executing the compiled ARM drawing function
offline. The separate Python bridge's live page rendered without JavaScript
errors after the repository move.

## Application connection and incremental updates

The installed image passed application-sector readback and normal boot with
settings record 38 unchanged, matching factory-table SHA and calibration/control
values. RAM observations covered enumeration without a session, reply-confirmed
connection, activity expiry, three-second idle lease expiry and explicit close.
Ordinary commands after a closed session did not activate the indicator. The
compiled ARM renderer was checked for connected, disconnected, TX/RX and expired
states. Python context exit cleared the session. The restarted bridge rendered
live high-speed data, and browser transport switching released and reclaimed USB
without changing saved settings or producing JavaScript errors. The WebUSB client
also sends the session handshake/close; its new handshake has not separately
been observed with a physical browser grant.

Update preparation now accepts a reusable compact unit snapshot instead of a
fresh full-flash image. The snapshot keeps identity/configuration, container
header and initial journal, with an optional validated factory-file export.
The factory export is decoded data, not a raw LittleFS recovery image; retain
existing full recovery backups. Live settings are checked on each installation
and never replaced by the old snapshot. Targeted reads cover identity and
candidate application/logo sectors; candidate regions are backed up before changed sectors are written
and read back. The connected unit completed a no-change install through this
path, reading 1,082,640 bytes before comparison, writing no sectors and booting
back into acquisition. The display-refresh update then exercised the changed-sector path on hardware: 25
application sectors were programmed and read back, with the settings journal unchanged. Verification artifacts are in
`build/usb-session-verification/`, excluded from publication.

## Display refresh

Runtime status text and USB activity updates now share the off-screen waveform
presentation path. Each buffer retains its own waveform and annotation version,
so a status-only update need not redraw an unchanged trace. The visible buffer
is not cleared or edited during ordinary refreshes. Presentation uses explicit
backend register loading, and rendering waits for an outstanding load to finish
before reusing the old buffer; polling continues without a busy wait. Boot log
and cursor updates also prepare the other buffer first, and SCREEN ON reuses an
already initialized boot display.

The user confirmed that refresh flashing was gone in the RAM build. Bounded
hardware observations covered live streaming, stopped overlay updates and a
channel-position change while stopped. The installed cold image passed targeted
sector readback, normal boot and FRAME7 decoding, restoring settings record 38
with unchanged controls and factory-table hash. A source-only rebuild matched.
The web server was restarted in Python bridge mode. Evidence is retained under
`build/display-refresh-verification/`. No automated suite was run.

## Acquisition UI cleanup

Both UIs retain ACQUIRING and a blinking dot throughout ordinary trigger/buffer
waits. Errors, lost heartbeats and STOP remain distinct. Both channel peak-to-peak
readings are shown below the plot on the LCD and web UI; the LCD excludes invalid
samples, applies the channel voltage gain and probe factor, and marks clipping.

The web header reserves status space, and focused timebase options are not
rewritten during live updates. Browser observations showed a steady label,
stationary header controls, zero focused-option mutations and both Vpp readouts,
without JavaScript errors. Frequency/period presentation uses a short median of
up to five distinct valid captures, with acquisition-change resets and immediate
clearing on invalid timing. SI formatting selects its prefix after rounding, so
readings close to 1 kHz do not alternate between Hz and kHz. Raw frequency
measurements and captured data remain unchanged. Python and WebUSB calibration
caches now invalidate when boot/settings restore state changes, preventing
pre-restore defaults from remaining in the reference-calibration UI.

The cold image passed targeted application-sector readback and normal boot,
retaining journal record 38, factory status and all reference/effective calibration
values. Artifacts are under `build/ui-cleanup-verification/` and the web UI's
`build/ui-cleanup/`. These were bounded observations and builds; no automated
suite was run.

## Installed font update — documented provenance

The LCD's former hand-coded glyph table is replaced with the Adafruit classic
font, pinned to an upstream commit with unchanged source and BSD-2-Clause
license. The ASCII subset is converted into the same 5×7 cells; layout and
character advance are unchanged. RAM and cold builds passed, and a generated
label preview was inspected. This font change passed flash readback and normal
boot to acquisition, with record 52 restored and CRC-valid high-speed USB frames.

Installed cold application SHA-256:
`cccac2e9589a0d9255ec446880d444e00deb4fa45b4495c6bddc6802c1e19e6c`.
No automated suite was run.
