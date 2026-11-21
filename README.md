# SlopScope

What is it?

SlopScope is a replacement firmware for the HANMATEK DOS1102, and it's called SlopScope because it's almost completely vibe-coded using GPT6-astra, from a FEL-mode dump obtained by (and partially decompiled by) Claude Opus 5.5, with the express goal of feeding the scope data into Claude for hardware analysis / diagnostics.

Why is it?

The stock firmware on these scopes is usable if you have working buttons and can deal with getting data over USB slowly using the normal protocol. I dropped mine and broke the connector that goes to the buttons and dials PCB. The stock USB controls can do a decent amount: set the timebase, voltage scales, offsets, coupling and probe factors; configure the edge trigger; run or stop acquisition; and download captured waveforms. But repeated waveform downloads were slow and could wedge the stock USB connection in our setup, so that wasn't the live view I wanted. Also, I found the original firmware difficult to use, with idiosyncrasies across the UI that I wanted to change.

So what do you get with this firmware?

SlopScope keeps the existing factory calibration and adds:

- Live two-channel streaming over USB
- Scope control from your browser, with or without working buttons and dials
- Shared Math and FFT controls across the scope and PC
- Simpler menus, light/dark themes and finer backlight control
- Reboot-to-FEL for easier development and updates
- An included Python USB library and easy-to-run bridge

The [web viewer](https://wocsor.github.io/slopscope-web/) is a separate project.
Use Chrome to connect directly over WebUSB, or run `./bridge` with the included
[Python tools](host/README.md) for other browsers.
Streaming sends snapshots of completed captures;
the [status doc](docs/status.md) explains the rates and limits.

It's still experimental. USB host support isn't implemented, and some acquisition and trigger cases need more checking. I'm not claiming complete stock parity or independently verified measurement accuracy. The [status doc](docs/status.md) covers what works and what's left.

- [Flash or update the firmware](docs/flashing.md)
- [USB protocol](docs/protocol.md)
- [Working features and remaining gaps](docs/status.md)

Where do I get it?

The release files are `Scope.update` for the stock updater and `SlopScope-fel.bin` for flashing through FEL. Published builds go under [GitHub Releases](https://github.com/wocsor/SlopScope/releases); branch builds go under [Actions](https://github.com/wocsor/SlopScope/actions).

CI builds both firmware files and a wheel for the included Python USB library. A successful build on `release/X.Y.Z` is configured to publish `vX.Y.Z`. The release script refuses to replace an already published version, so the next release needs a new version number.

SlopScope code is [MIT-licensed](LICENSE). LittleFS and the LCD font keep their upstream licenses; see the [third-party notices](THIRD_PARTY_NOTICES.md).
