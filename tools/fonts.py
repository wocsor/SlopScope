"""Convert the pinned Adafruit classic font to the LCD's existing 5×7 cells."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build_hud_font():
    source = (ROOT / "vendor/adafruit-gfx-font/glcdfont.c").read_text()
    initializer = source.split("font[] PROGMEM = {", 1)[1].split("};", 1)[0]
    initializer = re.sub(r"/\*.*?\*/|//[^\n]*", "", initializer, flags=re.DOTALL)
    columns = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", initializer)]
    if len(columns) != 256 * 5:
        raise ValueError("Unexpected upstream 256-glyph font layout")
    lines = [
        "/* Generated ASCII subset of Adafruit classic font; transposed to seven rows. */",
        "/* Copyright (c) 2012 Adafruit Industries. BSD-2-Clause; see THIRD_PARTY_NOTICES.md. */",
        "static const unsigned char hud_font[128][7] = {",
    ]
    for character_code in range(32, 127):
        rows = [
            sum(
                ((columns[character_code * 5 + column] >> row) & 1) << (4 - column)
                for column in range(5)
            )
            for row in range(7)
        ]
        lines.append(f"    [{character_code}] = {{{', '.join(map(str, rows))}}},")
    lines.append("};")
    (ROOT / "firmware/tables/hud_font.h").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    build_hud_font()
