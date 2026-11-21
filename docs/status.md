# What works, and what's left?

SlopScope is usable on the DOS1102 units we've been working with, both from the
front panel and from a PC. That doesn't mean every combination of range, signal
and trigger setting has been checked, or that it's a complete stock replacement.

## What's working?

- Two-channel acquisition, a live LCD grid, and voltage/time scales.
- USB full-speed and high-speed connections, live waveform snapshots, and remote controls.
- Channel scale, position, coupling, probe factor, inversion and bandwidth controls.
- Run/Stop, Single, Force, Edge triggering, trigger voltage and holdoff.
- The physical buttons, dials, Run/Stop LED and front-panel menus. The multipurpose
  knob moves through items until you select something to edit. Clockwise advances
  or increases; pressing selects or finishes editing. Back is on the bottom softkey.
- Horizontal position from USB and the panel knob. Clockwise moved the waveform
  left on the panel unit, and anticlockwise moved it right.
- Slow rolling previews. The grid and acquisition indicator appear while the
  first capture is still filling, and USB stays responsive during long captures.
  A saved horizontal delay is suspended during live roll and retained for later use.
- 1K/10K records, Sample/Average modes, and sampled Pulse triggering.
- Autoset, time/voltage cursors, and frequency/period/mean/AC-RMS readouts.
- Add, Subtract, Multiply, Divide, XY and FFT Math. Web selections also select the
  LCD operation, and LCD selections update the host calculation. Arithmetic Math
  appears in magenta on the web waveform grid; FFT and XY use the main graph.
- LCD FFT with 1024 samples, either channel, six windows, and Vrms/dBV scaling.
  The PC calculates its own result from the incoming raw channels. It can use a
  different record length, so the two spectra don't have to be identical.
- Utility → Backlight, adjustable from 1–100% using the selected knob control or USB.
- Dark and light themes, with thicker traces in light mode.
- Saved scope settings, including theme and brightness, restored after a full reboot.
- Reading the existing factory calibration records in place, plus optional zero
  and external-reference calibration controls. Installing SlopScope doesn't by
  itself require recalibrating the scope.
- Copy to a screenshot in RAM, which a USB client can download.
- Installation through the stock updater, reported working on the other scope,
  and FEL installs checked by reading the written application back.

The panel menus have four items per page, with Back on the fifth softkey. Turn
the knob to reach another page. Acquire opens acquisition settings; Single is
on its second page. Most of this has been tried on real hardware, but the whole
stock menu tree hasn't been reproduced or qualified.

## What still needs checking?

- **USB reliability.** Long runs have worked, but intermittent I/O failures remain
  a known issue. That isn't a promise that every cable, host or session will work
  indefinitely.
- **Broader panel and signal coverage.** Autoset, cursors, Math and the main panel
  controls have been observed working. Unusual inputs, fast sequences of panel
  actions, and all the Single/Force/menu combinations need more coverage. Autoset
  starts from the current record; it doesn't search every possible input setup.
- **Calibration accuracy.** Reusing factory records preserves the stock baseline;
  it doesn't certify voltage accuracy. Optional reference calibration needs a
  suitable external reference. The stock baseline only supports ranges 3–11
  (10 mV/div through 5 V/div).
- **Video triggering.** PAL has been observed working. NTSC/SECAM and the broader
  field/line cases still need qualification.
- **Hardware Peak mode.** Extrema are captured, but their chronological sample
  interpretation isn't verified. Don't treat them as an ordinary time-ordered waveform.
- **Advanced native triggers.** Slope, Runt, Spike, Timeout and Nth-edge remain
  experimental. Sampled Pulse works on the captured record and can miss events
  between records.
- **Timing at higher rates.** Absolute trigger position, phase correction and
  interpolation need more checking. A known 1 kHz source has been used to check
  common rates and horizontal delay, not every timebase.

Cursor and Math display settings are session-only on the scope. The web viewer
can keep them in a setup profile. Normal scope settings use the on-device journal.

## How live is the live view?

USB sends snapshots of completed captures. It doesn't send every sample the ADCs
produce, and frames can be skipped. Full records are limited to 1K or 10K samples
per channel. The LCD has a 30 Hz presentation limit; that isn't a guaranteed rate
of 30 new acquisitions a second.

In one short check at 100 kS/s, with 1998 raw samples per channel, waveform viewing
and host-only FFT both managed about 7.7 fresh frames/s. With FFT also running on
the LCD, that was about 6.5 frames/s. Those are results from that capture setup,
not the USB link's bandwidth limit. Both raw channels are sent in every Math mode;
the LCD calculation still costs CPU time.

## What about the two USB ports?

SlopScope currently supports the PC/device role. USB host support isn't implemented,
so it can't read flash drives, write screenshots to them, or run a USB-drive updater.
Use FEL to update an already installed SlopScope build.

The stock V4 code switches the same USB controller between host and device roles.
Its host setup shuts down the PC-device side, and its device setup shuts down the
host side. That code path can't read a flash drive and keep talking to a PC at the
same time. This conclusion comes from the recovered initialization code; connector
routing and automatic cable-priority behavior haven't been established by it.

Role switching could be added to SlopScope later. Simultaneous PC-device and USB-host
operation would need another independent USB controller; a second connector isn't
another controller. Neither host support nor role switching is part of this release.

The [flashing guide](flashing.md) covers installation. The [USB protocol](https://github.com/wocsor/SlopScope/blob/HEAD/docs/protocol.md)
covers the controls and data formats.
