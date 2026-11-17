"""Package the transparent icon as alpha-composited native LCD artwork."""

from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def build_logo():
    from PIL import Image

    icon = Image.open(ROOT / "assets/boot-logo.png").convert("RGBA")
    icon = icon.crop(icon.getchannel("A").getbbox())
    icon.thumbnail((160, 160), Image.Resampling.LANCZOS)
    height, width = icon.height, icon.width
    black = Image.new("RGBA", icon.size, (0, 0, 0, 255))
    black.alpha_composite(icon)
    indexed = black.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    indices = np.asarray(indexed, dtype=np.uint8)
    colors = np.asarray(indexed.getpalette(), dtype=np.uint16).reshape(-1, 3)
    palette = np.zeros(256, dtype="<u2")
    palette[: len(colors)] = (
        ((colors[:, 0] >> 3) << 11) | ((colors[:, 1] >> 2) << 5) | (colors[:, 2] >> 3)
    )
    rgb565_pixels = palette[indices]
    canvas = np.zeros((480, 800), dtype="<u2")
    canvas[
        240 - height // 2 : 240 - height // 2 + height, 400 - width // 2 : 400 - width // 2 + width
    ] = rgb565_pixels
    out = ROOT / "build"
    out.mkdir(parents=True, exist_ok=True)
    (out / "boot_logo.bin").write_bytes(canvas.tobytes())
    (ROOT / "firmware/tables/boot_logo_dimensions.h").write_text(
        f"#define BOOT_LOGO_WIDTH {width}u\n#define BOOT_LOGO_HEIGHT {height}u\n"
    )
    header_lines = [
        "/* Generated boot artwork. */",
        "#include <stdint.h>",
        f"#define BOOT_LOGO_WIDTH {width}u",
        f"#define BOOT_LOGO_HEIGHT {height}u",
        "const uint16_t boot_logo_palette[]={",
    ]
    for i in range(0, 256, 16):
        header_lines.append(",".join(hex(int(v)) for v in palette[i : i + 16]) + ",")
    header_lines.append("};")
    header_lines.append("const uint8_t boot_logo_pixels[]={")
    pixels = indices.ravel()
    for i in range(0, len(pixels), 32):
        header_lines.append(",".join(str(int(v)) for v in pixels[i : i + 32]) + ",")
    header_lines.extend(["};"])
    (ROOT / "firmware/tables/boot_logo.h").write_text("\n".join(header_lines) + "\n")


if __name__ == "__main__":
    build_logo()
