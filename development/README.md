# Development utilities

These tools are optional and do not participate in the production build.
Install `development/requirements.txt` for UART recording. The separate
`hscope` library is needed for host-side acquisition observations.

`observe_usb_responsiveness.py` performs bounded bench observations with exclusive
scope USB access. Its help output describes the requirements. Keep its output
under `build/`; the firmware build and installer do not run it.

The ESP32 stimulus/receiver firmware, its controller and its dependent sampled
Pulse observation script are maintained outside this repository in the research
workspace. They are not SlopScope source or release artifacts. Historical stock
comparisons and internal ARM harnesses are also kept in research.
