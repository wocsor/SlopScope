"""Stock-derived bounded zero/linearity calibration: 66274, 47e8, 792bc.
DOS1102 uses ranges 3..11, AC coupling, two baseline targets, 50-count maximum
DAC corrections, three settled readings and at most 100 iterations. This adapts
stock's processed int16 targets to our signed-byte wire samples. It preserves
voltage-reference gains and labels other ranges zero-adjusted with nominal gain.
"""

import copy
import datetime
import json
import math
import time
from pathlib import Path

import numpy as np

from .device import SCALES_MV, fields


def calibrate(scope):
    from ramview import validate_profile

    original = copy.deepcopy(scope.settings())
    original_table = original["calibration"]
    results = []
    committed = False
    held = False
    done = 0
    total = 36
    trace = []

    def restore(rollback):
        scope.stop()
        if rollback:
            # The pre-run journal snapshot is independent of mutable host caches.
            scope.apply({"op": "device_load"})
            return
        for action in validate_profile(
            {"format": "slopscope-setup", "version": 1, "settings": original}
        ):
            if action["op"] not in ("calibration", "processing", "offset"):
                scope.apply(action)

    def mean(channel, index, dac):
        scope.request(f"CH{channel} OFFSET {dac}")
        scope.wait_analog()
        time.sleep(0.08)
        samples = []
        previous = None
        deadline = time.monotonic() + 5
        while len(samples) < 4:
            metadata, channels, _ = scope.frame()
            if (
                metadata[f"ch{channel}_range"] == index
                and ((metadata["offsets_packed"] >> (16 * (channel - 1))) & 65535) == dac
                and metadata["sequence"] != previous
            ):
                samples.append(channels[channel - 1])
                previous = metadata["sequence"]
            if time.monotonic() > deadline:
                raise RuntimeError("No fresh calibration samples")
            time.sleep(0.025)
        a = np.asarray(samples, dtype=float).ravel()
        if np.percentile(a, 95) - np.percentile(a, 5) > 32:
            raise RuntimeError(f"CH{channel}: input is noisy or a signal is still connected")
        value = float(a.mean())
        clipped = bool(np.mean(np.abs(a) >= 124) > 0.01)
        trace.append(
            {"channel": channel, "range": index, "dac": dac, "mean": value, "clipped": clipped}
        )
        return value, clipped

    def solve(channel, index, dac, target, slope, phase):
        nonlocal done
        if not 10000 <= dac <= 50000:
            raise RuntimeError("Initial DAC is outside the stock calibration limits")
        settled = 0
        previous = None
        yield {
            "message": f"CH{channel} {SCALES_MV[index]} mV/div: measuring {phase}",
            "completed": done,
            "total": total,
        }
        initial, initial_clipped = mean(channel, index, dac)
        if abs(initial - target) > 8:
            # Raw-byte zero differs from stock's processed-int16 zero. Obtain
            # a coarse starting point; the stock-style fine solver remains bounded.
            distance = min(1000, max(128, round(20 / abs(slope))))
            probe = max(10000, min(50000, dac + (distance if initial > target else -distance)))
            measured, probe_clipped = mean(channel, index, probe)
            estimate = (measured - initial) / (probe - dac) if probe != dac else 0
            if not initial_clipped and not probe_clipped and -1 <= estimate < -0.00001:
                slope = estimate
            correction = max(-8192, min(8192, round((target - measured) / slope)))
            previous = (probe, measured)
            dac = max(10000, min(50000, probe + correction))
        for iteration in range(100):
            value, clipped = mean(channel, index, dac)
            error = value - target
            yield {
                "message": f"CH{channel} {SCALES_MV[index]} mV/div: {phase} ({iteration + 1})",
                "completed": done,
                "total": total,
                "channel": channel,
                "range": index,
            }
            if not clipped and abs(error) < 0.75:
                settled += 1
                if settled >= 3:
                    return dac, value, slope
            else:
                settled = 0
            if previous and abs(dac - previous[0]) >= 16:
                estimate = (value - previous[1]) / (dac - previous[0])
                if -1 <= estimate < -0.00001:
                    slope = estimate
            if not math.isfinite(slope) or not -1 <= slope < 0:
                raise RuntimeError("Invalid DAC response")
            if abs(error) < 0.75 and not clipped:
                continue
            step = max(-50, min(50, round(-error / slope)))
            if not step:
                step = 1 if error > 0 else -1
            next_dac = dac + step
            if not 10000 <= next_dac <= 50000:
                raise RuntimeError("Calibration reached the DAC limit")
            previous = (dac, value)
            dac = next_dac
        raise RuntimeError(f"CH{channel} range {index}: calibration did not converge")

    try:
        yield {"message": "Saving current setup", "completed": 0, "total": total}
        scope.apply({"op": "device_save"})
        scope.request("CALIBRATION BEGIN")
        held = True
        if original.get("device_acquisition"):
            scope.stop()
            scope.request("ACQUIRE MODE SAMPLE")
            for ch in (1, 2):
                scope.request(f"CH{ch} BANDWIDTH OFF")
                scope.wait_analog()
        scope.stop()
        scope.apply({"op": "rate", "index": 11})
        scope.request("TRIGGER ENABLE OFF")
        scope.request("LIVE RUN")
        for channel in (1, 2):
            scope.apply({"op": "coupling", "channel": channel, "value": "AC"})
            scope.apply({"op": "probe", "channel": channel, "value": 1})
            scope.apply({"op": "position", "channel": channel, "value": 0})
            for index in range(3, 12):
                old = original_table[channel - 1][index]
                gain = old["gain"]
                scope.apply({"op": "scale", "channel": channel, "index": index})
                scope.apply({"op": "position", "channel": channel, "value": 0})
                zero_seed = fields(scope.request(f"CH{channel} STATUS"))["offset_dac"]
                scope.apply({"op": "position", "channel": channel, "value": 3000})
                second_seed = fields(scope.request(f"CH{channel} STATUS"))["offset_dac"]
                target = min(90, 3 * SCALES_MV[index] * 256000 / gain)
                if not 20 <= target <= 105 or second_seed == zero_seed:
                    raise RuntimeError("Unsupported calibration geometry")
                slope = (
                    old["slope"] / 65536
                    if old["valid"] and old["slope"] < 0
                    else target / (second_seed - zero_seed)
                )
                zero_dac, zero_value, slope = yield from solve(
                    channel, index, zero_seed, 0, slope, "zero"
                )
                done += 1
                second_seed = max(10000, min(50000, round(zero_dac + target / slope)))
                shifted_dac, shifted_value, slope = yield from solve(
                    channel, index, second_seed, target, slope, "offset response"
                )
                done += 1
                if shifted_dac == zero_dac:
                    raise RuntimeError("Offset calibration has no span")
                response = (shifted_value - zero_value) / (shifted_dac - zero_dac)
                if not -0.999 <= response < -0.00001:
                    raise RuntimeError("Unusable calibrated DAC response")
                cal = {
                    "zero": round(zero_value * 65536),
                    "gain": gain,
                    "dac": zero_dac,
                    "slope": round(response * 65536),
                    "valid": 1 if old["valid"] == 1 else 2,
                }
                for key in ("adc_scale", "adc_trim"):
                    if key in old:
                        cal[key] = old[key]
                results.append({"channel": channel, "range": index, "cal": cal})
        scope.stop()
        for entry in results:
            scope.apply({"op": "calibration", **entry})
        yield {"message": "Restoring acquisition settings", "completed": done, "total": total}
        restore(False)
        scope.request("CALIBRATION END")
        held = False
        scope.apply({"op": "device_save"})
        committed = True
        yield {
            "message": "Zero and offset calibration saved; voltage-reference gains preserved",
            "completed": total,
            "total": total,
            "results": results,
            "finished": True,
        }
    finally:
        if not committed and held:
            try:
                restore(True)
            finally:
                scope.request("CALIBRATION CANCEL")
        folder = Path.home() / ".config/slopscope/calibration-traces"
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / (
            datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + ".json"
        )
        path.write_text(
            json.dumps(
                {"committed": committed, "measurements": trace, "results": results}, indent=2
            )
            + "\n"
        )
