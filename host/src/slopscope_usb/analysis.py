"""Waveform analysis on captured ADC samples.

Time quantities use the nominal sample rate. No streaming gap is treated as a
contiguous sample interval; only samples within a single record are measured.
Optional settings calibrate Math to voltage units; sample measurements remain in ADC codes.
"""

from __future__ import annotations

import collections

import numpy as np

DEFAULTS = {
    "mode": "sample",
    "averages": 16,
    "math": "off",
    "math_source": 0,
    "math_reversed": 0,
    "persistence": 0,
    "fft_window": "hann",
    "fft_units": "dbv",
    "style": "lines",
    "intensity": 90,
}


def validate_processing(key, value):
    choices = {
        "mode": ("sample", "average", "peak"),
        "math": ("off", "add", "subtract", "multiply", "divide", "fft", "xy"),
        "style": ("lines", "dots"),
        "fft_window": ("hann", "hamming", "blackman", "rectangular", "kaiser", "bartlett"),
        "fft_units": ("dbv", "vrms"),
    }
    limits = {
        "averages": (2, 256),
        "math_source": (0, 1),
        "math_reversed": (0, 1),
        "persistence": (0, 64),
        "intensity": (10, 100),
    }
    if key in choices and value in choices[key]:
        return value
    if key in limits and type(value) is int and limits[key][0] <= value <= limits[key][1]:
        return value
    raise ValueError("Invalid waveform processing setting")


def crossings(a, level, rising=True):
    candidates = np.flatnonzero(
        ((a[:-1] < level) & (a[1:] >= level)) if rising else ((a[:-1] >= level) & (a[1:] < level))
    )
    if not len(candidates):
        return np.array([], dtype=float)
    candidates = candidates[np.r_[True, np.diff(candidates) > 3]]
    return candidates + (level - a[candidates]) / (a[candidates + 1] - a[candidates])


def measure(samples, hz):
    a = np.asarray(samples, dtype=float)
    result = {
        "min": float(a.min()),
        "max": float(a.max()),
        "mean": float(a.mean()),
        "rms": float(np.sqrt(np.mean(a * a))),
        "ac_rms": float(a.std()),
        "pp": float(np.ptp(a)),
        "square_sum": float(np.sum(a * a)),
        "area_codes_s": float(a.sum() / hz),
        "frequency_hz": None,
        "period_s": None,
        "duty_percent": None,
        "positive_width_s": None,
        "negative_width_s": None,
        "rise_s": None,
        "fall_s": None,
        "cycle_rms": None,
        "cycle_mean": None,
        "clipped": bool(np.any((a <= -124) | (a >= 124))),
        "sample_count": len(a),
    }
    base, top = np.percentile(a, [10, 90])
    result.update(
        base=float(base),
        top=float(top),
        amplitude=float(top - base),
        overshoot_percent=float(max(0, a.max() - top) / (top - base) * 100)
        if top - base >= 12
        else None,
        undershoot_percent=float(max(0, base - a.min()) / (top - base) * 100)
        if top - base >= 12
        else None,
    )
    if top - base < 12 or len(a) < 12:
        return result
    smooth = np.convolve(a, np.ones(5) / 5, mode="valid")
    mid = (base + top) / 2
    rise = crossings(smooth, mid)
    fall = crossings(smooth, mid, False)
    if len(rise) < 2:
        return result
    periods = np.diff(rise)
    period = float(np.median(periods))
    if period < 8 or np.any(np.abs(periods - period) > period * 0.15):
        return result
    # Preserve fractional-sample periods: the median can lock to one sample bin.
    period = float((rise[-1] - rise[0]) / (len(rise) - 1))
    widths = []
    lows = []
    for start, end in zip(rise[:-1], rise[1:]):
        edges = fall[(fall > start) & (fall < end)]
        if len(edges) == 1:
            widths.append(float(edges[0] - start))
            lows.append(float(end - edges[0]))
    if not widths:
        return result
    result.update(
        frequency_hz=float(hz / period),
        period_s=period / hz,
        duty_percent=float(np.mean(widths) / period * 100),
        positive_width_s=float(np.mean(widths) / hz),
        negative_width_s=float(np.mean(lows) / hz),
    )
    start, end = int(np.ceil(rise[0])) + 2, int(np.floor(rise[-1])) + 2
    if end > start:
        result["cycle_rms"] = float(np.sqrt(np.mean(a[start:end] ** 2)))
        result["cycle_mean"] = float(np.mean(a[start:end]))
    # Edge speed is measured on the unsmoothed record; interpolation of an
    # under-resolved step is deliberately not reported as analog rise time.
    for rising, key in ((True, "rise_s"), (False, "fall_s")):
        first = crossings(a, base + (top - base) * (0.1 if rising else 0.9), rising)
        last = crossings(a, base + (top - base) * (0.9 if rising else 0.1), rising)
        durations = []
        for edge in first:
            following = last[(last >= edge) & (last - edge < period * 0.25)]
            if len(following) and following[0] - edge >= 2:
                durations.append(float(following[0] - edge))
        if durations:
            result[key] = float(np.median(durations) / hz)
    return result


def channel_timing(channels, hz, measurements):
    """CH2 rising-edge lag relative to CH1, wrapped to half a common period."""
    if len(channels) != 2 or any(m.get("frequency_hz") is None for m in measurements):
        return None
    frequencies = [m["frequency_hz"] for m in measurements]
    if abs(frequencies[0] - frequencies[1]) > min(frequencies) * 0.02:
        return None
    period = hz / frequencies[0]
    edges = []
    for samples, m in zip(channels, measurements):
        samples = np.asarray(samples, dtype=float)
        edges.append(
            crossings(
                np.convolve(samples, np.ones(5) / 5, mode="valid"), (m["base"] + m["top"]) / 2
            )
        )
    if min(map(len, edges)) < 2:
        return None
    deltas = []
    for edge in edges[0]:
        nearest = edges[1][np.argmin(np.abs(edges[1] - edge))]
        deltas.append(float((nearest - edge + period / 2) % period - period / 2))
    median = float(np.median(deltas))
    residual = np.asarray(deltas) - median
    residual = (residual + period / 2) % period - period / 2
    if np.max(np.abs(residual)) > period * 0.1:
        return None
    return {
        "delay_s": median / hz,
        "phase_deg": median / period * 360,
        "cycles": len(deltas),
        "reference": "CH1",
        "channel": "CH2",
    }


class Processor:
    def __init__(self):
        self.config = dict(DEFAULTS)
        self.generation = 0
        self.reset()

    def reset(self):
        self.history = collections.deque()
        self.total = None
        self.signature = None

    def configure(self, key, value):
        self.config[key] = validate_processing(key, value)
        self.generation += 1
        self.reset()
        return dict(self.config)

    def process(self, channels, hz, metadata, settings=None):
        if metadata.get("valid_count", len(channels[0])) < len(channels[0]):
            start = metadata.get("valid_from", 0)
            count = metadata["valid_count"]
            self.reset()
            readings = [
                measure(ch[start : start + count], hz) if count >= 2 else {} for ch in channels
            ]
            if metadata.get("acquire_mode") == 1:
                for reading in readings:
                    for key in reading:
                        if key not in ("min", "max", "pp", "clipped", "sample_count"):
                            reading[key] = None
            return {
                "measurements": readings,
                "processing_count": 0,
                "processing_generation": self.generation,
                "measurement_note": f"Rolling preview: {count} fresh samples; uncollected history is blank.",
            }
        raw = np.asarray(channels, dtype=float)
        signature = (
            raw.shape,
            hz,
            metadata["ch1_range"],
            metadata["ch2_range"],
            metadata["ch1_coupling"],
            metadata["ch2_coupling"],
            metadata["offsets_packed"],
            metadata.get("acquire_mode", 0),
            metadata.get("device_averages"),
            metadata.get("bandwidth_bits"),
            metadata.get("trigger_family"),
            metadata.get("horizontal_ns"),
            metadata.get("roll", False),
        )
        if signature != self.signature:
            self.reset()
            self.signature = signature
        mode = "sample" if metadata.get("roll") else self.config["mode"]
        processed = raw
        envelope = None
        if mode != "sample":
            self.history.append(raw)
            self.total = raw.copy() if self.total is None else self.total + raw
            if len(self.history) > self.config["averages"]:
                self.total -= self.history.popleft()
            if mode == "average":
                processed = self.total / len(self.history)
            else:
                stack = np.stack(self.history)
                envelope = {"low": stack.min(axis=0).tolist(), "high": stack.max(axis=0).tolist()}
        result = {
            "measurements": [measure(ch, hz) for ch in processed],
            "processing_count": len(self.history) if mode != "sample" else 1,
            "processing_generation": self.generation,
        }
        if metadata.get("acquire_mode") == 1:
            # Extrema are useful, but the peak stream has no validated uniform
            # time/amplitude distribution for period, RMS or FFT calculations.
            meaningful = {"min", "max", "pp", "clipped", "sample_count"}
            for m in result["measurements"]:
                for key in m:
                    if key not in meaningful:
                        m[key] = None
            result["measurement_note"] = (
                "Hardware peak detect: extrema only; timing and RMS unavailable."
            )
            return result
        result["channel_timing"] = channel_timing(processed, hz, result["measurements"])
        if mode == "average":
            result["display_channels"] = processed.tolist()
        if envelope:
            result["envelope"] = envelope
        op = self.config["math"]
        math_channels = processed
        if settings is not None:
            from .voltage import to_volts
            math_channels = [to_volts(channel, settings, metadata, index)
                             for index, channel in enumerate(processed)]
        a, b = math_channels
        if op in ("add", "subtract", "multiply", "divide"):
            values = {
                "add": lambda: a + b,
                "subtract": lambda: a - b,
                "multiply": lambda: a * b,
                "divide": lambda: np.divide(
                    a, b, out=np.full_like(a, np.nan), where=np.abs(b) > 1e-12
                ),
            }[op]()
            if op == "subtract" and self.config["math_reversed"]:
                values = -values
            result["math"] = {
                "kind": op,
                "values": [float(v) if np.isfinite(v) else None for v in values],
                "unit": ("V" if settings is not None else "ADC codes")
                if op in ("add", "subtract")
                else ("V²" if settings is not None else "code²")
                if op == "multiply"
                else "ratio",
            }
        elif op == "fft":
            source = math_channels[self.config["math_source"]]
            n = len(source)
            window = {
                "hann": np.hanning,
                "hamming": np.hamming,
                "blackman": np.blackman,
                "rectangular": np.ones,
                "kaiser": lambda count: np.kaiser(count, 6.5),
                "bartlett": np.bartlett,
            }[self.config["fft_window"]](n)
            amplitude = 2 * np.abs(np.fft.rfft((source - source.mean()) * window)) / window.sum()
            amplitude[0] /= 2
            if n % 2 == 0:
                amplitude[-1] /= 2
            unit = "ADC codes peak"
            if settings is not None:
                amplitude[1:-1 if n % 2 == 0 else None] /= np.sqrt(2)
                unit = "V RMS"
                if self.config["fft_units"] == "dbv":
                    amplitude = 20 * np.log10(np.maximum(amplitude, 1e-12))
                    unit = "dBV"
            result["math"] = {
                "kind": "fft",
                "frequencies": np.fft.rfftfreq(n, 1 / hz).tolist(),
                "values": amplitude.tolist(),
                "unit": unit,
                "window": self.config["fft_window"],
                "bin_hz": hz / n,
            }
        elif op == "xy":
            result["math"] = {
                "kind": "xy",
                "x": a.tolist(),
                "values": b.tolist(),
                "unit": "V" if settings is not None else "ADC codes",
            }
        return result
