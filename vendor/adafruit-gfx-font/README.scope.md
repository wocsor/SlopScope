# Adafruit classic bitmap font

Source: Adafruit-GFX-Library commit
`f256ef6b0f248711a0cd354be0da5ccd224808c3`, `src/glcdfont.c`.
The source and BSD-2-Clause license are retained unchanged; `UPSTREAM.json`
records their URLs and SHA-256 hashes. No Adafruit rendering code is compiled.

`tools/fonts.py` selects printable ASCII 32–126, transposes five column bytes to
row masks and retains rows 0–6 for SlopScope's existing 5×7 drawing cells. Row 7
is omitted; some unused lowercase descenders therefore lose their bottom row.
The HUD's uppercase labels and existing m/k/u/n/s unit glyphs fit seven rows.
The generated header is `firmware/tables/hud_font.h`.

The complete upstream copyright, conditions and disclaimer are also included in
`THIRD_PARTY_NOTICES.md`, copied beside build/update outputs. SlopScope's MIT
license does not replace this font's BSD-2-Clause license.
