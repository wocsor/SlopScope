"""Sector boundaries shared by install planning and readback verification."""

FLASH_BYTES = 0x1000000
SECTOR_BYTES = 4096
APPLICATION_START = 0x349000
APPLICATION_END = 0x517000
JOURNAL_START = 0x517000
JOURNAL_BYTES = 8192
LEGACY_JOURNAL_START = 0xFFE000


def changed_runs(current, target):
    if len(current) != FLASH_BYTES or len(target) != FLASH_BYTES:
        raise ValueError("Both flash images must be exactly 16 MiB")
    runs = []
    start = None
    for offset in range(0, FLASH_BYTES, SECTOR_BYTES):
        changed = current[offset : offset + SECTOR_BYTES] != target[offset : offset + SECTOR_BYTES]
        if changed and start is None:
            start = offset
        elif not changed and start is not None:
            runs.append((start, offset))
            start = None
    if start is not None:
        runs.append((start, FLASH_BYTES))
    return runs
