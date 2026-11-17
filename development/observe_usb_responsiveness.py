#!/usr/bin/env python3
"""Bounded development observation of USB during live, roll and deep capture.

Requires exclusive CFW USB access and a clean saved setup. Holds autosave and
recalls that setup afterwards; no scope flash writes or signal reference needed.
"""

import argparse
import json
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
from hscope.device import RamScope, fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--live-seconds", type=float, default=30)
    parser.add_argument("--roll-seconds", type=float, default=12)
    args = parser.parse_args()
    if not 0 < args.live_seconds <= 120 or not 0 < args.roll_seconds <= 60:
        parser.error("Use 0–120 seconds live and 0–60 seconds roll")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result = {"scope_flash_written": False, "samples": [], "cases": []}

    def save():
        args.output.write_text(json.dumps(result, indent=2) + "\n")

    with RamScope() as scope:
        original = scope.settings()
        result["original"] = original
        if original["persistence"].get("dirty") or not original["persistence"].get("sequence"):
            raise RuntimeError("Observation requires a clean saved setup")
        if fields(scope.request("CALIBRATION STATUS")).get("active"):
            raise RuntimeError("Another settings transaction is active")
        scope.request("CALIBRATION BEGIN")  # Hold autosave; no calibration.
        steps = None
        try:
            for name, rate, duration in [
                ("live", 10, args.live_seconds),
                ("two_second_roll", 18, args.roll_seconds),
            ]:
                scope.stop()
                scope.apply({"op": "rate", "index": rate})
                for key, value in [("family", "NORMAL"), ("mode", "EDGE"), ("sweep", "AUTO")]:
                    scope.apply({"op": "trigger", "key": key, "value": value})
                scope.apply({"op": "roll", "value": "LIVE"})
                scope.request("LIVE ON")
                start = time.monotonic()
                while time.monotonic() - start < duration:
                    at = time.monotonic()
                    pong = scope.request("PING")
                    ping_ms = (time.monotonic() - at) * 1000
                    metadata, _, _ = scope.frame()
                    row = dict(
                        case=name,
                        elapsed_s=time.monotonic() - start,
                        ping=pong,
                        ping_ms=ping_ms,
                        metadata=metadata,
                        activity=scope.activity(),
                        usb=fields(scope.request("USB STATE")),
                    )
                    result["samples"].append(row)
                    save()
                    time.sleep(0.04)
                rows = [row for row in result["samples"] if row["case"] == name]
                summary = dict(
                    case=name,
                    responses=len(rows),
                    max_ping_ms=max(row["ping_ms"] for row in rows),
                    first_sequence=rows[0]["metadata"]["sequence"],
                    last_sequence=rows[-1]["metadata"]["sequence"],
                    last_usb=rows[-1]["usb"],
                )
                result["cases"].append(summary)
                print(json.dumps(summary), flush=True)
            scope.stop()
            scope.apply({"op": "rate", "index": 16})  # 10K / 5 kS/s = 2 s.
            steps = scope.record_steps(1, 10000)
            started = time.monotonic()
            while True:
                try:
                    progress = next(steps)
                except StopIteration as done:
                    result["record_metadata"] = done.value["metadata"]
                    steps = None
                    break
                at = time.monotonic()
                pong = scope.request("PING")
                row = dict(
                    case="two_second_record",
                    elapsed_s=time.monotonic() - started,
                    ping=pong,
                    ping_ms=(time.monotonic() - at) * 1000,
                    progress=progress,
                    activity=scope.activity(),
                    usb=fields(scope.request("USB STATE")),
                )
                result["samples"].append(row)
                save()
                time.sleep(0.025)
            rows = [row for row in result["samples"] if row["case"] == "two_second_record"]
            result["cases"].append(
                dict(
                    case="two_second_record",
                    elapsed_s=time.monotonic() - started,
                    responses=len(rows),
                    max_ping_ms=max(row["ping_ms"] for row in rows),
                )
            )
            print(json.dumps(result["cases"][-1]), flush=True)
        except Exception as exc:
            result["error"] = dict(message=str(exc), operation=scope.last_operation)
            save()
            raise
        finally:
            if steps is not None:
                steps.close()
            scope.stop()
            scope.apply({"op": "device_load"})
            scope.request("CALIBRATION CANCEL")
            restored = scope.settings()
            result["restored"] = restored
            keys = (
                "channels",
                "rate_index",
                "calibration_baseline",
                "calibration",
                "reference_calibration",
                "horizontal",
                "view",
                "trigger_banks",
                "device_acquisition",
                "running",
            )
            result["restoration_matches"] = all(original[key] == restored[key] for key in keys)
            result["journal_unchanged"] = (
                original["persistence"]["sequence"] == restored["persistence"]["sequence"]
            )
            save()
            if not result["restoration_matches"] or not result["journal_unchanged"]:
                raise RuntimeError("Restored setup differs from the initial snapshot")


if __name__ == "__main__":
    main()
