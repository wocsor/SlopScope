# SlopScope

Replacement firmware for the HANMATEK DOS1102, focused on PC control, USB waveform
streaming and a live display on the scope. It reuses the unit's factory calibration.

- [Flash or update the firmware](docs/flashing.md)
- [USB protocol](docs/protocol.md)
- [Working features and remaining gaps](docs/status.md)

Download `Scope.update` or `SlopScope-fel.bin` from
[GitHub Releases](https://github.com/wocsor/SlopScope/releases).
Builds from ordinary branches are available under
[Actions](https://github.com/wocsor/SlopScope/actions).

CI builds both installation files. Pushing `release/X.Y.Z` publishes release
`vX.Y.Z` after the build succeeds; each published version stays unchanged.
Physical buttons/dials and USB host support are not implemented in SlopScope.

SlopScope code is [MIT-licensed](LICENSE). LittleFS and the LCD font retain their
upstream licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
