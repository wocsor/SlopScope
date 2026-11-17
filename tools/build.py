#!/usr/bin/env python3
"""Build the standalone scope application from source."""

import argparse
import hashlib
import json
import shutil
import subprocess

from elftools.elf.elffile import ELFFile
from toolchain import BUILD, CFLAGS, ROOT, compiler, linker

from assets import build_logo
from fonts import build_hud_font

ENTRY_ADDRESS = 0x80080000
INCLUDE_DIRECTORIES = [
    ROOT / "firmware/include",
    ROOT / "firmware/tables",
    ROOT / "firmware/libc",
    ROOT / "vendor/littlefs",
]


def compile_sources(output, defines):
    sources = sorted(path for path in (ROOT / "firmware").rglob("*.c") if "dram" not in path.parts)
    if "-DUSB_DIAGNOSTICS=1" not in defines:
        sources = [source for source in sources if source.name != "diagnostics.c"]
    sources.extend([ROOT / "vendor/littlefs/lfs.c", ROOT / "vendor/littlefs/lfs_util.c"])
    sources.append(ROOT / "firmware/startup/entry.S")
    objects = []
    flags = [*CFLAGS, *(f"-I{path}" for path in INCLUDE_DIRECTORIES), *defines]
    clang = compiler()
    for source in sources:
        relative = source.relative_to(ROOT)
        obj = output / "objects" / relative.with_suffix(source.suffix + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        optimization = (
            ["-flto=thin"] if source.suffix == ".c" and source.parent.name != "libc" else []
        )
        subprocess.run(
            [clang, *flags, *optimization, "-c", str(source), "-o", str(obj)], check=True
        )
        objects.append(obj)
    return objects


def link_image(output, objects):
    elf_path = output / "scope.elf"
    subprocess.run(
        [
            linker(),
            "--fatal-warnings",
            "--gc-sections",
            "-T",
            str(ROOT / "firmware/startup/memory.ld"),
            "-Map=" + str(output / "scope.map"),
            "-o",
            str(elf_path),
            *(str(obj) for obj in objects),
        ],
        check=True,
    )
    with elf_path.open("rb") as stream:
        elf = ELFFile(stream)
        sections = [
            section
            for section in elf.iter_sections()
            if section["sh_flags"] & 2 and section["sh_size"] and section["sh_type"] != "SHT_NOBITS"
        ]
        image_end = max(section["sh_addr"] + section["sh_size"] for section in sections)
        image = bytearray(image_end - ENTRY_ADDRESS)
        for section in sections:
            start = section["sh_addr"] - ENTRY_ADDRESS
            image[start : start + section["sh_size"]] = section.data()
        symbols = {
            symbol.name: symbol["st_value"]
            for symbol in elf.get_section_by_name(".symtab").iter_symbols()
        }
    (output / "scope.bin").write_bytes(image)
    return image, symbols


def build(
    high_speed=True,
    cold_boot=True,
    volatile_settings=False,
    trigger_research=False,
    usb_diagnostics=False,
):
    if trigger_research and (cold_boot or not volatile_settings):
        raise ValueError("Trigger research requires a volatile RAM build")
    if usb_diagnostics and (cold_boot or not volatile_settings):
        raise ValueError("USB diagnostics requires a volatile RAM build")
    output = BUILD / ("diagnostic-ram" if usb_diagnostics else "cold" if cold_boot else "ram")
    output.mkdir(parents=True, exist_ok=True)
    build_logo()
    build_hud_font()
    defines = [
        "-DLFS_READONLY",
        "-DLFS_NO_MALLOC",
        "-DLFS_NO_ASSERT",
        "-DLFS_NO_DEBUG",
        "-DLFS_NO_WARN",
        "-DLFS_NO_ERROR",
        f"-DUSB_HIGH_SPEED={int(high_speed)}",
        f"-DSCOPE_VOLATILE_SETTINGS={int(volatile_settings)}",
        f"-DSCOPE_TRIGGER_RESEARCH={int(trigger_research)}",
        f"-DUSB_DIAGNOSTICS={int(usb_diagnostics)}",
    ]
    if cold_boot:
        defines.append("-DSCOPE_COLD_BOOT=1")
    image, symbols = link_image(output, compile_sources(output, defines))
    manifest = {
        "entry": ENTRY_ADDRESS,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "bss_start": symbols["__bss_start"],
        "bss_end": symbols["__bss_end"],
        "vid": 0xDEAF,
        "pid": 0xBABE,
        "bulk_out": 1,
        "bulk_in": 0x81,
        "packet_size": 512 if high_speed else 64,
        "full_speed_packet_size": 64,
        "speed": "high-capable" if high_speed else "full",
        "stats": 0x800A0000,
        "flash_written": False,
        "cold_boot": cold_boot,
        "volatile_settings": volatile_settings,
        "trigger_research": trigger_research,
        "usb_diagnostics": usb_diagnostics,
        "optimization": "ThinLTO; compiler runtime compiled separately",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    shutil.copyfile(ROOT / "THIRD_PARTY_NOTICES.md", output / "THIRD_PARTY_NOTICES.md")
    shutil.copyfile(ROOT / "LICENSE", output / "LICENSE")
    print(f"Built {output.name} firmware: {len(image)} bytes, SHA-256 {manifest['sha256']}")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ram", action="store_true", help="Build a volatile RAM development image")
    parser.add_argument(
        "--cold-boot", action="store_true", help="Build the installed entry (default)"
    )
    parser.add_argument("--full-speed", action="store_true")
    parser.add_argument(
        "--high-speed", action="store_true", help="High-speed capable USB (default)"
    )
    parser.add_argument("--volatile-settings", action="store_true")
    parser.add_argument("--trigger-research", action="store_true")
    parser.add_argument("--usb-diagnostics", action="store_true")
    args = parser.parse_args()
    if args.ram and args.cold_boot:
        parser.error("Choose either --ram or --cold-boot")
    build(
        not args.full_speed,
        not args.ram,
        args.ram or args.volatile_settings,
        args.trigger_research,
        args.usb_diagnostics,
    )
