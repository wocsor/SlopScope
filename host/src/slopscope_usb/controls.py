"""Validation for device controls and portable setup profiles."""

from .analysis import validate_processing
from .voltage import validate_calibration


def validate_action(data):
    if not isinstance(data, dict):
        raise ValueError("Expected an action object")
    op = data.get("op")
    if isinstance(op, str) and op.startswith("lcd_"):
        from .lcd import validate_lcd_action

        return validate_lcd_action(data)
    if op == "video":
        standard, sync, count = data.get("standard"), data.get("sync"), data.get("count")
        if (
            type(standard) is not int
            or not 0 <= standard <= 2
            or type(sync) is not int
            or not 0 <= sync <= 4
            or type(count) is not int
            or not 1 <= count <= 625
        ):
            raise ValueError("Invalid video trigger settings")
        if sync == 4:
            count = min(count, 525 if standard == 0 else 625)
        return {"op": op, "standard": standard, "sync": sync, "count": count}
    if op in ("run", "stop", "autoset", "defaults", "fit", "device_save", "device_load"):
        return {"op": op}
    if op == "calibration_baseline" and data.get("value") in ("STOCK", "REFERENCE", "ZERO"):
        return {"op": op, "value": data["value"]}
    if op == "selfcal_start" and data.get("grounded") is True:
        return {"op": op}
    if op == "selfcal_cancel":
        return {"op": op}
    if op == "calibration":
        channel = data.get("channel")
        index = data.get("range")
        cal = validate_calibration(data.get("cal"))
        if (
            type(channel) is not int
            or channel not in (1, 2)
            or type(index) is not int
            or not 0 <= index < 14
            or cal["valid"] not in (0, 1, 2)
        ):
            raise ValueError("Invalid calibration target")
        return {"op": op, "channel": channel, "range": index, "cal": cal}
    if op == "profile":
        return {"op": op, "actions": validate_profile(data.get("profile"))}
    if op == "processing":
        key = data.get("key")
        value = validate_processing(key, data.get("value"))
        return {"op": op, "key": key, "value": value}
    if op == "acquire":
        key = data.get("key")
        value = data.get("value")
        if key == "mode" and value in ("SAMPLE", "PEAK", "AVERAGE"):
            return {"op": op, "key": key, "value": value}
        if key == "averages" and type(value) is int and value in (4, 16, 64, 128):
            return {"op": op, "key": key, "value": value}
        raise ValueError("Invalid device acquisition setting")
    if op == "roll" and data.get("value") in ("LIVE", "TRIGGERED"):
        return {"op": op, "value": data["value"]}
    if op == "record":
        channel = data.get("channel")
        depth = data.get("depth")
        force = data.get("force", False)
        if (
            (channel == "both" or type(channel) is int and channel in (1, 2))
            and type(depth) is int
            and depth in (1000, 10000)
            and type(force) is bool
        ):
            return {"op": op, "channel": channel, "depth": depth, "force": force}
        raise ValueError("Invalid record request")
    if op == "trigger_limits":
        lo = data.get("lower_uv")
        hi = data.get("upper_uv")
        if type(lo) is int and type(hi) is int and -1000000000 <= lo < hi <= 1000000000:
            return {"op": op, "lower_uv": lo, "upper_uv": hi}
        raise ValueError("Invalid trigger levels")
    if op == "horizontal":
        if data.get("stock") is True:
            return {"op": op, "stock": True}
        value = data.get("value")
        if type(value) is int and -1000000000 <= value <= 1000000000:
            return {"op": op, "value": value}
        raise ValueError("Invalid horizontal offset")
    if op == "view":
        start = data.get("start")
        points = data.get("points")
        if (
            type(start) is int
            and type(points) is int
            and 10 <= points <= 2000
            and 0 <= start <= 2000 - points
        ):
            return {"op": op, "start": start, "points": points}
        raise ValueError("Invalid capture window")
    if (
        op in ("position", "probe", "display", "inverse", "offset", "bandwidth")
        and type(data.get("channel")) is int
        and data["channel"] in (1, 2)
    ):
        value = data.get("value")
        if (
            (op == "position" and type(value) is int and -4000 <= value <= 4000)
            or (op == "offset" and type(value) is int and 0 <= value <= 65535)
            or (op == "probe" and type(value) is int and value in (1, 10, 100, 1000))
            or (op in ("display", "inverse", "bandwidth") and value in ("ON", "OFF"))
        ):
            return {"op": op, "channel": data["channel"], "value": value}
        raise ValueError("Invalid channel control")
    if op == "trigger":
        key = data.get("key")
        value = data.get("value")
        choices = {
            "enabled": ("ON", "OFF"),
            "source": ("CH1", "CH2"),
            "slope": ("RISE", "FALL"),
            "sweep": ("AUTO", "NORMAL", "SINGLE"),
            "coupling": ("DC", "AC"),
            "mode": ("EDGE", "VIDEO", "PULSE", "SLOPE", "RUNT", "SPIKE", "TIMEOUT", "NTHEDGE"),
            "family": ("NORMAL", "ALTERNATE"),
        }
        limits = {
            "level_uv": (-1000000000, 1000000000),
            "code": (-127, 126),
            "holdoff_ns": (100, 1000000000),
            "condition": (0, 5),
            "width_ns": (100, 1000000000),
            "lower_uv": (-1000000000, 1000000000),
            "upper_uv": (-1000000000, 1000000000),
            "count": (1, 65535),
            "video_count": (0, 65535),
            "video_sync": (0, 4),
            "video_standard": (0, 2),
        }
        if key in ("force", "single"):
            return {"op": op, "key": key}
        if key in choices and value in choices[key]:
            return {"op": op, "key": key, "value": value}
        if key in limits and type(value) is int and limits[key][0] <= value <= limits[key][1]:
            return {"op": op, "key": key, "value": value}
        raise ValueError("Invalid trigger setting")
    if op in ("rate", "timebase") and type(data.get("index")) is int and 1 <= data["index"] <= 31:
        return {"op": op, "index": data["index"]}
    if (
        op in ("scale", "coupling")
        and type(data.get("channel")) is int
        and data["channel"] in (1, 2)
    ):
        if op == "scale" and type(data.get("index")) is int and 0 <= data["index"] < 14:
            return {"op": op, "channel": data["channel"], "index": data["index"]}
        if op == "coupling" and data.get("value") in ("DC", "AC"):
            return {"op": op, "channel": data["channel"], "value": data["value"]}
    raise ValueError("Unsupported control setting")


def validate_profile(profile):
    if (
        not isinstance(profile, dict)
        or profile.get("format") not in ("slopscope-setup", "hscope-ram-setup")
        or profile.get("version") != 1
    ):
        raise ValueError("Unsupported setup format")
    settings = profile.get("settings")
    processing = profile.get("processing", {})
    if not isinstance(settings, dict) or not isinstance(processing, dict):
        raise ValueError("Invalid setup settings")
    channels = settings.get("channels")
    trigger = settings.get("trigger")
    view = settings.get("view")
    if (
        not isinstance(channels, list)
        or len(channels) != 2
        or not all(isinstance(c, dict) for c in channels)
        or not isinstance(trigger, dict)
        or not isinstance(view, dict)
        or type(settings.get("running")) is not bool
    ):
        raise ValueError("Incomplete setup")
    selected_trigger = trigger
    banks = settings.get("trigger_banks", {})
    if banks and (
        not isinstance(banks, dict)
        or set(banks) != set(("NORMAL", "CH1", "CH2"))
        or not all(isinstance(v, dict) for v in banks.values())
    ):
        raise ValueError("Invalid trigger banks")
    actions = [{"op": "stop"}]
    baseline = settings.get("calibration_baseline")
    if baseline is not None and (type(baseline) is not int or baseline not in (0, 1, 2)):
        raise ValueError("Invalid calibration baseline")
    if baseline is not None or settings.get("calibration_model") == 2:
        actions.append({"op": "calibration_baseline", "value": "REFERENCE"})
    if banks:
        actions.append({"op": "trigger", "key": "family", "value": "NORMAL"})
        trigger = {**banks["NORMAL"], "en": selected_trigger.get("en"), "advanced_supported": True}

    table = settings.get("reference_calibration") or settings.get("calibration")
    if table is not None and settings.get("calibration_model") == 2:
        if (
            not isinstance(table, list)
            or len(table) != 2
            or any(not isinstance(c, list) or len(c) != 14 for c in table)
        ):
            raise ValueError("Invalid calibration table")
        for ch in range(2):
            for index in range(14):
                cal = validate_calibration(table[ch][index])
                actions.append({"op": "calibration", "channel": ch + 1, "range": index, "cal": cal})
    for n, ch in enumerate(channels, 1):
        if (
            ch.get("coupling") not in (0, 1)
            or ch.get("visible") not in (0, 1)
            or ch.get("inverse") not in (0, 1)
        ):
            raise ValueError("Invalid channel settings")
        if ch.get("range") in (0, 1, 12, 13) and ch.get("position_mdiv"):
            raise ValueError("Position calibration unavailable for this range")
        actions.extend(
            [
                {"op": "position", "channel": n, "value": 0},
                {"op": "scale", "channel": n, "index": ch.get("range")},
                {"op": "coupling", "channel": n, "value": ("DC", "AC")[ch["coupling"]]},
                {"op": "position", "channel": n, "value": ch.get("position_mdiv")},
                {"op": "probe", "channel": n, "value": ch.get("probe")},
                {"op": "display", "channel": n, "value": "ON" if ch["visible"] else "OFF"},
                {"op": "inverse", "channel": n, "value": "ON" if ch["inverse"] else "OFF"},
            ]
        )
        if "bandwidth" in ch:
            if ch["bandwidth"] not in (0, 1):
                raise ValueError("Invalid bandwidth state")
            actions.append(
                {"op": "bandwidth", "channel": n, "value": "ON" if ch["bandwidth"] else "OFF"}
            )
        if ch.get("position_mdiv") == 0 and "offset_dac" in ch:
            actions.append({"op": "offset", "channel": n, "value": ch["offset_dac"]})
    actions.extend(
        [
            {"op": "rate", "index": settings.get("rate_index")},
            {"op": "view", "start": view.get("start"), "points": view.get("points")},
        ]
    )
    for key, field, values in [
        ("enabled", "en", ("OFF", "ON")),
        ("source", "src", ("CH1", "CH2")),
        ("slope", "slope", ("RISE", "FALL")),
        ("sweep", "sweep", ("AUTO", "NORMAL", "SINGLE")),
        ("coupling", "coupling", ("DC", "AC")),
    ]:
        index = trigger.get(field)
        if type(index) is not int or not 0 <= index < len(values):
            raise ValueError("Invalid trigger setup")
        actions.append({"op": "trigger", "key": key, "value": values[index]})
    actions.append({"op": "trigger", "key": "holdoff_ns", "value": trigger.get("holdoff_ns")})
    if trigger.get("raw") == 1:
        code = trigger.get("code")
        if type(code) is int and code >= 0x80000000:
            code -= 0x100000000
        actions.append({"op": "trigger", "key": "code", "value": code})
    else:
        level = trigger.get("uv")
        if type(level) is int and level >= 0x80000000:
            level -= 0x100000000
        actions.append({"op": "trigger", "key": "level_uv", "value": level})
    if trigger.get("advanced_supported"):
        modes = ("EDGE", "VIDEO", "PULSE", "SLOPE", "RUNT", "SPIKE", "TIMEOUT", "NTHEDGE")
        mode = trigger.get("mode")
        if type(mode) is not int or not 0 <= mode < 8:
            raise ValueError("Invalid trigger mode")
        actions.append({"op": "trigger", "key": "mode", "value": modes[mode]})
        for key, field in [
            ("condition", "condition"),
            ("width_ns", "width_ns"),
            ("count", "count"),
            ("video_count", "video_count"),
            ("video_sync", "sync"),
            ("video_standard", "standard"),
        ]:
            actions.append({"op": "trigger", "key": key, "value": trigger.get(field)})
        lo = trigger.get("lower_uv")
        hi = trigger.get("upper_uv")
        if type(lo) is int and lo >= 0x80000000:
            lo -= 0x100000000
        if type(hi) is int and hi >= 0x80000000:
            hi -= 0x100000000
        actions.append({"op": "trigger_limits", "lower_uv": lo, "upper_uv": hi})
        horizontal = settings.get("horizontal", {})
        if horizontal.get("enabled"):
            value = horizontal.get("offset_ns", 0)
            value = value - 0x100000000 if value >= 0x80000000 else value
            actions.append({"op": "horizontal", "value": value})
        else:
            actions.append({"op": "horizontal", "stock": True})
    if banks:
        modes = ("EDGE", "VIDEO", "PULSE", "SLOPE", "RUNT", "SPIKE", "TIMEOUT", "NTHEDGE")
        actions.extend(
            [
                {"op": "trigger", "key": "sweep", "value": "AUTO"},
                {"op": "trigger", "key": "family", "value": "ALTERNATE"},
            ]
        )
        for name in ("CH1", "CH2"):
            bank = banks[name]
            actions.append({"op": "trigger", "key": "source", "value": name})
            mode = bank.get("mode")
            if type(mode) is not int or not 0 <= mode < 8:
                raise ValueError("Invalid bank mode")
            actions.append({"op": "trigger", "key": "mode", "value": modes[mode]})
            for key, field, choices in [
                ("slope", "slope", ("RISE", "FALL")),
                ("coupling", "coupling", ("DC", "AC")),
            ]:
                value = bank.get(field)
                if type(value) is not int or not 0 <= value < len(choices):
                    raise ValueError("Invalid bank setting")
                actions.append({"op": "trigger", "key": key, "value": choices[value]})
            for key, field in [
                ("holdoff_ns", "holdoff_ns"),
                ("condition", "condition"),
                ("width_ns", "width_ns"),
                ("count", "count"),
                ("video_count", "video_count"),
                ("video_sync", "sync"),
                ("video_standard", "standard"),
            ]:
                actions.append({"op": "trigger", "key": key, "value": bank.get(field)})
            field = "code" if bank.get("raw") else "uv"
            value = bank.get(field)
            if type(value) is int and value >= 0x80000000:
                value -= 0x100000000
            actions.append(
                {"op": "trigger", "key": "code" if field == "code" else "level_uv", "value": value}
            )
            lo, hi = bank.get("lower_uv"), bank.get("upper_uv")
            if type(lo) is int and lo >= 0x80000000:
                lo -= 0x100000000
            if type(hi) is int and hi >= 0x80000000:
                hi -= 0x100000000
            actions.append({"op": "trigger_limits", "lower_uv": lo, "upper_uv": hi})
        actions.append({"op": "trigger", "key": "family", "value": "NORMAL"})
        sweep = trigger.get("sweep")
        if type(sweep) is not int or not 0 <= sweep < 3:
            raise ValueError("Invalid saved sweep")
        actions.append(
            {"op": "trigger", "key": "sweep", "value": ("AUTO", "NORMAL", "SINGLE")[sweep]}
        )
        if selected_trigger.get("alternate"):
            actions.extend(
                [
                    {"op": "trigger", "key": "family", "value": "ALTERNATE"},
                    {
                        "op": "trigger",
                        "key": "source",
                        "value": "CH2" if selected_trigger.get("src") else "CH1",
                    },
                ]
            )
    device = settings.get("device_acquisition", {})
    if device:
        mode = device.get("mode")
        if type(mode) is not int or not 0 <= mode < 3:
            raise ValueError("Invalid device acquisition mode")
        actions.extend(
            [
                {"op": "acquire", "key": "mode", "value": ("SAMPLE", "PEAK", "AVERAGE")[mode]},
                {"op": "acquire", "key": "averages", "value": device.get("averages")},
            ]
        )
    from .lcd import lcd_profile_actions

    actions.extend(lcd_profile_actions(settings.get("lcd")))
    actions.extend(
        {"op": "processing", "key": key, "value": value} for key, value in processing.items()
    )
    if baseline is not None or settings.get("calibration_model") == 2:
        actions.append(
            {
                "op": "calibration_baseline",
                "value": "ZERO" if baseline == 2 else "REFERENCE" if baseline == 1 else "STOCK",
            }
        )
    if settings["running"]:
        actions.append(
            {"op": "trigger", "key": "single"} if selected_trigger["sweep"] == 2 else {"op": "run"}
        )
    return [validate_action(action) for action in actions]
