"""Fixed reference conversion shared with firmware/voltage.c, no per-frame fit."""

from __future__ import annotations

import numpy as np

from .device import SCALES_MV


def validate_calibration(cal):
    if not isinstance(cal, dict):
        raise ValueError("Invalid reference calibration")
    bounds = {
        "zero": (-128 * 65536, 127 * 65536),
        "gain": (1, 1024000000),
        "dac": (0, 65535),
        "slope": (-65536, 65536),
        "valid": (0, 2),
    }
    result = {}
    for key, (lo, hi) in bounds.items():
        value = cal.get(key)
        if type(value) is not int or not lo <= value <= hi:
            raise ValueError("Invalid calibration " + key)
        result[key] = value
    if "adc_scale" in cal or "adc_trim" in cal:
        for key in ("adc_scale", "adc_trim"):
            value = cal.get(key)
            if type(value) is not int or not 0 <= value <= 65535:
                raise ValueError("Invalid calibration " + key)
            result[key] = value
    return result


def channel_calibration(settings, metadata, ch):
    index = metadata[f"ch{ch + 1}_range"]
    table = settings.get("calibration")
    return (
        table[ch][index]
        if table
        else {"zero": 0, "gain": SCALES_MV[index] * 10240, "dac": 0, "slope": 0, "valid": 0}
    )


def coefficients(settings, metadata, ch):
    cal = channel_calibration(settings, metadata, ch)
    dac = (metadata["offsets_packed"] >> (ch * 16)) & 65535
    gain = cal["gain"] / 256000000 * settings["channels"][ch].get("probe", 1)
    offset = -(cal["zero"] + (dac - cal["dac"]) * cal["slope"]) / 65536 * gain
    return gain, offset


def to_volts(samples, settings, metadata, ch):
    gain, offset = coefficients(settings, metadata, ch)
    return np.asarray(samples, dtype=float) * gain + offset
