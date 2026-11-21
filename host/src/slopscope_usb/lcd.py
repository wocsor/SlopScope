"""Native LCD controls and checked, bounded screenshot reads."""

import base64
import struct
import time
import zlib


def validate_lcd_action(action):
    op = action.get("op")
    if op == "lcd_theme" and action.get("value") in ("DARK", "LIGHT"):
        return {"op": op, "value": action["value"]}
    if op == "lcd_autoset" and action.get("key") in ("start", "cancel"):
        return {"op": op, "key": action["key"]}
    bounds = {
        "lcd_fft": {"source": (1, 2), "window": (0, 5), "logarithmic": (0, 1),
                    "range": (0, 13), "db_div": (5, 20), "reference_dbv": (-120, 120)},
        "lcd_brightness": {"value": (1, 100)},
        "lcd_cursors": {"mode": (0, 2), "source": (1, 2), "selected": (0, 1),
                        "xa": (0, 749), "xb": (0, 749), "ya": (40, 439), "yb": (40, 439)},
        "lcd_math": {"mode": (0, 6), "range": (0, 13), "probe": (1, 1000),
                     "position": (-4000, 4000), "reversed": (0, 1)},
    }
    if op in bounds:
        for key, (minimum, maximum) in bounds[op].items():
            value = action.get(key)
            if type(value) is not int or not minimum <= value <= maximum:
                raise ValueError(f"Invalid {op} {key}")
        if op == "lcd_math" and action["probe"] not in (1, 10, 100, 1000):
            raise ValueError("Invalid LCD Math probe factor")
        return {"op": op, **{key: action[key] for key in bounds[op]}}
    if op == "lcd_copy":
        key = action.get("key")
        if key in ("capture", "status"):
            return {"op": op, "key": key}
        if key == "read" and all(
            type(action.get(name)) is int and minimum <= action[name] <= maximum
            for name, minimum, maximum in (("id", 1, 0xffffffff), ("offset", 0, 767999))
        ):
            return {"op": op, "key": key, "id": action["id"], "offset": action["offset"]}
    raise ValueError("Invalid LCD control")


def lcd_command(action):
    action = validate_lcd_action(action)
    op = action["op"]
    if op == "lcd_theme":
        return f"DISPLAY THEME {action['value']}"
    if op == "lcd_brightness":
        return f"DISPLAY BRIGHTNESS {action['value']}"
    if op == "lcd_autoset":
        return f"AUTOSET {action['key'].upper()}"
    if op == "lcd_fft":
        return "PANEL FFT " + " ".join(str(action[key]) for key in
            ("source", "window", "logarithmic", "range", "db_div", "reference_dbv"))
    keys = ("mode", "source", "selected", "xa", "xb", "ya", "yb") if op == "lcd_cursors" else (
        "mode", "range", "probe", "position", "reversed")
    prefix = "PANEL CURSORS" if op == "lcd_cursors" else "PANEL MATH"
    return prefix + " " + " ".join(str(action[key]) for key in keys)


def lcd_settings(scope):
    from .device import fields

    result = {}
    for key, command in (("theme", "DISPLAY THEME"), ("brightness", "DISPLAY BRIGHTNESS"),
                         ("tools", "PANEL TOOLS"), ("autoset", "AUTOSET STATUS")):
        try:
            reply = scope.request(command)
        except RuntimeError as error:
            if not str(error).startswith("ERR "):
                raise
            continue
        result[key] = reply.split()[-1] if key == "theme" else fields(reply)
    tools = result.get("tools", {})
    if tools.get("fft_reference_dbv", 0) & 0x80000000:
        tools["fft_reference_dbv"] -= 0x100000000
    if tools.get("math_position", 0) & 0x80000000:
        tools["math_position"] -= 0x100000000
    return result


def screenshot_chunk(scope, snapshot_id, offset):
    if scope.pending:
        raise ValueError("Unexpected pending data before LCD screenshot read")
    command = f"SCREENSHOT READ {snapshot_id} {offset}"
    scope.last_operation = {"command": command, "phase": "read", "started_at": time.time()}
    scope.device.write(1, (command + "\n").encode("ascii"))
    deadline = time.monotonic() + 5
    data = bytearray()
    while len(data) < 32:
        if time.monotonic() >= deadline:
            raise TimeoutError("LCD screenshot read timed out")
        data.extend(scope._read_bulk(512))
        if data.startswith(b"ERR "):
            raise RuntimeError(data.decode("ascii").strip())
    magic, reply_id, reply_offset, count, total, width, height, checksum = struct.unpack(
        "<8I", data[:32])
    if (magic != 0x31534353 or reply_id != snapshot_id or reply_offset != offset or
            count != min(448, 768000 - offset) or total != 768000 or (width, height) != (800, 480)):
        raise ValueError("Invalid LCD screenshot header")
    while len(data) < 32 + count:
        if time.monotonic() >= deadline:
            raise TimeoutError("LCD screenshot read timed out")
        data.extend(scope._read_bulk(512))
    struct.pack_into("<I", data, 28, 0)
    if len(data) != 32 + count or zlib.crc32(data) != checksum:
        raise ValueError("LCD screenshot CRC or length mismatch")
    scope.last_operation["phase"] = "complete"
    return data[32:]


def lcd_copy(scope, action):
    from .device import fields

    action = validate_lcd_action(action)
    if action["key"] == "capture":
        scope.request("SCREENSHOT CAPTURE")
    if action["key"] != "read":
        return fields(scope.request("SCREENSHOT STATUS"))
    pixels = bytearray()
    for _ in range(8):
        offset = action["offset"] + len(pixels)
        if offset >= 768000:
            break
        pixels.extend(screenshot_chunk(scope, action["id"], offset))
    return {"id": action["id"], "offset": action["offset"],
            "data": base64.b64encode(pixels).decode("ascii")}


def lcd_profile_actions(lcd):
    if not lcd:
        return []
    if not isinstance(lcd, dict):
        raise ValueError("Invalid LCD profile")
    actions = []
    if lcd.get("theme"):
        actions.append({"op": "lcd_theme", "value": lcd["theme"]})
    if lcd.get("brightness"):
        value = lcd["brightness"].get("percent")
        if type(value) is not int:
            raise ValueError("Invalid LCD brightness")
        actions.append({"op": "lcd_brightness", "value": max(1, value)})
    tools = lcd.get("tools", {})
    if "cursor_xa" in tools:
        required = ("cursors", "source", "cursor", "cursor_xa", "cursor_xb", "cursor_ya",
                    "cursor_yb", "math", "math_range", "math_probe", "math_position", "math_reversed")
        if any(name not in tools for name in required):
            raise ValueError("Incomplete LCD tools profile")
        actions.append({"op": "lcd_cursors", "mode": tools["cursors"],
                        "source": tools["source"] + 1, "selected": tools["cursor"],
                        "xa": tools["cursor_xa"], "xb": tools["cursor_xb"],
                        "ya": tools["cursor_ya"], "yb": tools["cursor_yb"]})
        actions.append({"op": "lcd_math", "mode": tools["math"], "range": tools["math_range"],
                        "probe": tools["math_probe"], "position": tools["math_position"],
                        "reversed": tools["math_reversed"]})
    if tools.get("math") == 3 and "fft_window" in tools:
        actions.append({"op": "lcd_fft", "source": tools["fft_source"] + 1,
                        "window": tools["fft_window"], "logarithmic": tools["fft_logarithmic"],
                        "range": tools["math_range"], "db_div": tools["fft_db_div"],
                        "reference_dbv": tools["fft_reference_dbv"]})
    return [validate_lcd_action(action) for action in actions]


MATH_OPERATIONS = {"off": 0, "add": 1, "subtract": 2, "fft": 3,
                   "multiply": 4, "divide": 5, "xy": 6}
FFT_WINDOWS = {"hamming": 0, "rectangular": 1, "blackman": 2,
               "hann": 3, "kaiser": 4, "bartlett": 5}


def mirrored_math_action(processing, settings, key):
    if key not in ("math", "math_source", "fft_window", "fft_units", "math_reversed"):
        return None
    tools = settings.get("lcd", {}).get("tools", {})
    if "math" not in tools:
        return None
    operation = processing["math"]
    mode = MATH_OPERATIONS[operation]
    supported = tools.get("math_modes", 15 if "fft_window" in tools else 7)
    if not supported & (1 << mode):
        return None
    if key != "math" and operation != "fft" and not (key == "math_reversed" and operation == "subtract"):
        return None
    source = processing["math_source"] if operation == "fft" else (1 if operation == "xy" else 0)
    channel = settings["channels"][source]
    changed = mode != tools["math"]
    dimensionless = operation in ("multiply", "divide")
    display_range = (9 if dimensionless else channel["range"]) if changed else tools["math_range"]
    if operation == "fft":
        return {"op": "lcd_fft", "source": source + 1, "window": FFT_WINDOWS[processing["fft_window"]],
                "logarithmic": int(processing.get("fft_units", "dbv") == "dbv"),
                "range": display_range, "db_div": tools.get("fft_db_div", 10),
                "reference_dbv": tools.get("fft_reference_dbv", 20)}
    return {"op": "lcd_math", "mode": mode, "range": display_range,
            "probe": 1 if dimensionless else channel.get("probe", 1),
            "position": 0 if changed else tools["math_position"],
            "reversed": processing.get("math_reversed", 0) if operation == "subtract" else 0}


def math_processing_from_lcd(settings):
    tools = (settings.get("lcd") or {}).get("tools", {})
    operations = {value: key for key, value in MATH_OPERATIONS.items()}
    if tools.get("math") not in operations:
        return {}
    result = {"math": operations[tools["math"]]}
    if tools["math"] == 2:
        result["math_reversed"] = tools.get("math_reversed", 0)
    if tools["math"] == 3 and "fft_window" in tools:
        windows = {value: key for key, value in FFT_WINDOWS.items()}
        result.update(math_source=tools["fft_source"], fft_window=windows[tools["fft_window"]],
                      fft_units="dbv" if tools["fft_logarithmic"] else "vrms")
    return result
