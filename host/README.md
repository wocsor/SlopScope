# SlopScope USB

This is the Python library and bridge for SlopScope. It lives in the firmware
repo so you don't have to find and install a second library checkout. It only
connects to SlopScope's `DEAF:BABE` interface; stock USBTMC/SCPI isn't supported.

## Just open the viewer

From the firmware repo:

```sh
./bridge
```

The first run creates `.venv-usb` and installs the local package. Later runs reuse
it. The bridge opens http://127.0.0.1:8766/ and serves the viewer there. Pick a scope
by serial if more than one is connected. Close any other USB client for that scope.

The viewer's HTML/JavaScript still lives in the separate `slopscope-web` repo.
The bridge finds a sibling checkout automatically. Otherwise it fetches the
published viewer files from `https://wocsor.github.io/slopscope-web/`, keeping them
in memory for that session. Only viewer files are fetched; scope data stays local.
The published site must be deployed before that default URL can be used.
For an offline checkout or a different hosted URL:

```sh
./bridge --ui-dir /path/to/slopscope-web/src/slopscope_web/static
./bridge --viewer-url https://example.com/slopscope-web/
```

Use `--no-browser` to leave it running without opening a tab, or `--port 8767`
for a different port. `--serial SS-...` selects a particular scope at startup.
The listener is local-only, not a remotely accessible USB service.

Python 3.11+ and libusb are required on macOS/Linux. On macOS, install libusb with
`brew install libusb`; on Debian/Ubuntu, install `libusb-1.0-0` through your package
manager. Python dependencies are just NumPy and PyUSB. No VISA, plotting package,
Node server or separate web framework is needed.

## Use the library

CI also builds a `slopscope_usb-*.whl` release asset, installable with pip.
To install the source into your own Python environment:

```sh
python3 -m pip install -e ./host
slopscope-usb list
slopscope-usb info --serial SS-...
slopscope-bridge
```

```python
from slopscope_usb import SlopScope, list_scopes

print(list_scopes())
with SlopScope(serial_number="SS-...") as scope:
    scope.apply({"op": "timebase", "index": 12})
    metadata, channels, payload = scope.frame()
```

With one scope, `SlopScope()` selects it automatically. With several, it raises
`ScopeSelectionRequired` instead of guessing. Each connection stays pinned to its
serial, and one client owns each scope at a time. Serialize calls to a connection;
it doesn't add a background thread.

`channels` contains signed ADC samples. Convert using
`slopscope_usb.voltage.to_volts(samples, settings, metadata, channel_index)` with
matching settings and calibration metadata. The library also handles settings,
records, CRC-checked snapshots, LCD tools, calibration and mirrored Math. The
[protocol](../docs/protocol.md) describes the wire commands and formats.

The bridge keeps live view, both channels, Math/FFT, controls, screenshot downloads,
setup profiles and grounded self-calibration. The old stock-firmware CLI and viewer
aren't part of this package. Existing `hscope-ram-setup` files remain readable;
that file-format name is compatibility data, not a stock-firmware transport.
