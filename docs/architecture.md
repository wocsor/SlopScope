# Architecture

`firmware/core/runtime.c` runs the cooperative frontend scheduler. Each polling
step advances bounded work so the USB driver can service the host between steps.
`scope_initialize()` establishes defaults before either boot path starts.

| Directory | Responsibility |
| --- | --- |
| `core` | Runtime defaults, scheduler and lifecycle |
| `acquisition` | Capture phases, roll, records, averaging and trigger planning |
| `calibration` | ADC gain and voltage/position conversion |
| `display` | Boot console, LCD rendering, grid and status |
| `hardware` | Analog/ADC setup, FPGA configuration and register transport |
| `platform` | Timer, GPIO and SPI peripheral access |
| `protocol` | Text command parsing, channel/trigger/acquisition control |
| `storage` | SPI flash, settings journal and factory filesystem |
| `usb` | USB device transport and endpoint servicing |
| `startup` | ARM entry, linker layout and separate FEL DRAM helper |
| `include` | Public interfaces, internal state and value types |
| `tables` | Hardware setup data and generated artwork |
| `libc` | Small freestanding runtime support |

`scope.h` is the USB driver's frontend interface. `scope_internal.h` exposes
interfaces within the frontend. `scope_state.h` groups runtime state by subsystem;
this structure lives in BSS and is never serialized. Polling code remains
single-threaded. USB service during long work must not recursively dispatch a
frontend command.

`scope_saved_settings` and its nested value types define the existing journal
payload. Runtime field grouping does not change this on-flash format. Factory
records remain in their original filesystem; optional corrections are separate.
Wire headers and explicit frame alignment metadata retain their existing ABI.
The application uses ThinLTO to retain optimization across source modules.

`tools/` contains standalone build/load/install tools. `development/` contains
optional bounded observation scripts. ESP32 fixture firmware stays outside this
repository. The observations use
the Python control library; building and installing SlopScope does not.

Build products, full images, captures and environments belong in ignored
`build/` or outside this repository. Historical ARM comparisons and decompilation
are kept outside the repository; they are not a dependency of the source build.

## Display presentation

Waveforms, annotations and activity indicators are composed in the off-screen
RGB565 buffer. Status polling marks overlay changes; it never clears the visible
buffer. Per-buffer frame and control revisions allow status-only refreshes to
reuse an unchanged plot. The renderer queues a completed frame and returns to
the cooperative scheduler while the display engine latches it. It checks the
pending load before writing into the former front buffer. The boot console uses
the same buffer ownership rule and preserves the intended cursor blink.

The register-load bits are documented by the upstream Linux
[display backend register definitions](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/sun4i/sun4i_backend.h)
and its [commit and pending-load handling](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/sun4i/sun4i_backend.c).
SlopScope uses the register interface with its own cooperative renderer.
