"""Tool discovery for ARM builds and FEL access."""

import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
CFLAGS = [
    "--target=armv5te-none-eabi",
    "-mcpu=arm926ej-s",
    "-marm",
    "-mfloat-abi=soft",
    "-Os",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-strict-aliasing",
    "-fno-common",
    "-ffunction-sections",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-mno-unaligned-access",
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
]


def find_tool(variable, name, *fallbacks):
    configured = os.environ.get(variable)
    if configured:
        return configured
    found = shutil.which(name)
    if found:
        return found
    for path in fallbacks:
        if Path(path).is_file():
            return str(path)
    raise RuntimeError(f"Install {name} or set {variable} to its executable")


def compiler():
    return find_tool("ARM_CLANG", "clang")


def linker():
    return find_tool(
        "ARM_LD_LLD", "ld.lld", "/opt/homebrew/bin/ld.lld", "/opt/homebrew/opt/llvm/bin/ld.lld"
    )


def fel():
    return find_tool(
        "SUNXI_FEL",
        "sunxi-fel",
        ROOT.parent / "sunxi-tools/sunxi-fel",
        ROOT.parent.parent / "sunxi-tools/sunxi-fel",
    )
