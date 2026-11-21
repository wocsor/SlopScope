#!/usr/bin/env python3
"""Build a C diagnostic plus the audited, relocated BOOT0 DRAM instruction block.
The stock DDR range 0x7ec..0x146b contains all DDR routines and literal pools.
Its sole external dependency is unsigned divide (0x2ac0..0x2c43).
Copy both blocks unchanged except two BL instructions at 0x109c/0x10c0.
No attempt is made to execute the whole bootloader or its flash/CPU-clock code.
"""

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

import toolchain
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "firmware/startup/dram"

OUT = ROOT / "build/dram"


def build(backup=None):
    OUT.mkdir(parents=True, exist_ok=True)
    if backup is not None:
        flash = Path(backup).read_bytes()
        if len(flash) != 0x1000000:
            raise ValueError("Expected this scope's 16 MiB flash backup")
        boot = flash[:0x3400]
    else:
        raise ValueError("Pass --backup with this scope's full flash backup")
    lo, hi = 0x7EC, 0x146C
    divlo, divhi = 0x2AC0, 0x2C44
    address = 0x2800
    blob = bytearray(boot[lo:hi] + boot[divlo:divhi])
    patches = []
    for site in (0x109C, 0x10C0):
        word = struct.unpack_from("<I", boot, site)[0]
        assert word >> 24 == 0xEB
        off = word & 0xFFFFFF
        off = off - (1 << 24) if off & (1 << 23) else off
        assert site + 8 + off * 4 == 0x2C20
        newsite = address + site - lo
        target = address + (hi - lo) + (0x2C20 - divlo)
        newword = 0xEB000000 | (((target - newsite - 8) // 4) & 0xFFFFFF)
        struct.pack_into("<I", blob, site - lo, newword)
        patches.append(
            {
                "original_site": site,
                "relocated_site": newsite,
                "target": target,
                "old_word": word,
                "new_word": newword,
            }
        )
    (OUT / "stock_dram.bin").write_bytes(blob)
    asm = OUT / "stock_dram.S"
    asm.write_text(
        '.syntax unified\n.cpu arm926ej-s\n.arm\n.section .stock_dram,"ax",%progbits\n.balign 4\n.global stock_dram_init\n.type stock_dram_init,%function\nstock_dram_base:\n.incbin "'
        + str(OUT / "stock_dram.bin")
        + '"\n.set stock_dram_init, stock_dram_base + '
        + str(0x1368 - lo)
        + "\n"
    )
    objects = []
    for source in (HERE / "entry.S", HERE / "dram.c", ROOT / "firmware/display/panel.c", asm):
        obj = OUT / (source.name + ".o")
        objects.append(str(obj))
        subprocess.run(
            [
                toolchain.compiler(),
                *toolchain.CFLAGS,
                "-I" + str(ROOT / "firmware/include"),
                "-c",
                str(source),
                "-o",
                str(obj),
            ],
            check=True,
        )
    elf = OUT / "bringup.elf"
    subprocess.run(
        [
            toolchain.linker(),
            "--fatal-warnings",
            "--gc-sections",
            "-T",
            str(HERE / "memory.ld"),
            "-Map=" + str(OUT / "bringup.map"),
            "-o",
            str(elf),
            *objects,
        ],
        check=True,
    )
    segments = []
    with elf.open("rb") as f:
        e = ELFFile(f)
        assert e["e_entry"] == 0x2000
        for section in e.iter_sections():
            if section["sh_flags"] & 2 and section["sh_size"]:
                assert section["sh_type"] != "SHT_NOBITS"
                data = section.data()
                name = "bringup" + section.name + ".bin"
                (OUT / name).write_bytes(data)
                segments.append(
                    {
                        "address": section["sh_addr"],
                        "size": len(data),
                        "file": name,
                        "sha256": hashlib.sha256(data).hexdigest(),
                    }
                )
    manifest = {
        "entry": 0x2000,
        "mailbox": 0x4800,
        "mailbox_size": 72,
        "request": 0x49F0,
        "dram_megabytes": 32,
        "segments": segments,
        "stock_dram_original_range": [lo, hi],
        "stock_divide_original_range": [divlo, divhi],
        "boot0_sha256": hashlib.sha256(boot).hexdigest(),
        "relocations": patches,
        "writes_flash": False,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Built bring-up segments:", [(hex(s["address"]), s["size"]) for s in segments])
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--backup", type=Path, required=True, help="This scope's full 16 MiB flash backup"
    )
    args = parser.parse_args()
    build(args.backup)
