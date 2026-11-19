# Feature status

SlopScope is an experimental firmware for PC-controlled DOS1102 use.

## Implemented and observed working

- [x] Two-channel acquisition and live LCD grid/traces, with voltage/time scales.
- [x] USB full/high-speed operation, waveform transfer and remote controls.
- [x] Channel scale, position, probe factor, coupling, inversion and bandwidth controls.
- [x] Run/Stop, Single, Force, Edge triggering, trigger level and holdoff.
- [x] Slow rolling previews with USB remaining serviceable during acquisition.
- [x] 1K/10K records, Sample/Average modes and sampled Pulse triggering.
- [x] Saved settings, factory-calibration reuse and optional calibration controls.
- [x] Stock-updater installation reported working; FEL installation readback verified.

## Limited or still being qualified

- Intermittent USB I/O failures remain a known issue despite successful long runs.
- PAL Video triggering has been observed; NTSC/SECAM and broader field/line cases need qualification.
- Hardware Peak captures extrema, but its chronological sample interpretation is unverified.
- Native Slope/Runt/Spike/Timeout/Nth-edge triggers remain experimental.
- Absolute high-rate trigger position, phase correction and interpolation need more qualification.
- Live previews are snapshots, not a continuous lossless ADC stream. Records remain limited to 1K/10K samples per channel.

## Not implemented

- [ ] Physical front-panel buttons and rotary dials.
- [ ] USB host support in SlopScope: storage, peripherals and USB-drive firmware updates.
  The stock firmware switches its shared USB controller between host and PC-device roles.

Use PC controls for the current firmware. See the [USB protocol](protocol.md)
and [flashing guide](flashing.md).
