"""Strict parsing for the dedicated distance frames; other logs are discarded."""

from dataclasses import dataclass
import json

PREFIX = "SC_DISTANCE "
MAX_LINE_LENGTH = 1024


@dataclass(frozen=True)
class Sample:
    v: int
    seq: int
    uptime_ms: int
    valid: bool
    distance_mm: int | None
    raw_status: int
    state: str
    error: int


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON field")
        result[key] = value
    return result


def _reject_constant(value):
    raise ValueError("non-finite JSON number")


def parse_line(line: str) -> Sample | None:
    """Return a validated frame, without printing or retaining ordinary logs."""
    if not isinstance(line, str) or len(line) > MAX_LINE_LENGTH or not line.startswith(PREFIX):
        return None
    try:
        value = json.loads(line[len(PREFIX):], object_pairs_hook=_unique_object,
                           parse_constant=_reject_constant)
    except (ValueError, RecursionError):
        return None
    if not isinstance(value, dict):
        return None
    integers = ("v", "seq", "uptime_ms", "raw_status", "error")
    if any(type(value.get(name)) is not int for name in integers):
        return None
    if (value["v"] != 1 or not 0 <= value["seq"] <= 0xFFFFFFFF
            or not 0 <= value["uptime_ms"] <= 0xFFFFFFFFFFFFFFFF
            or not 0 <= value["raw_status"] <= 255):
        return None
    if type(value.get("valid")) is not bool or "distance_mm" not in value:
        return None
    distance = value["distance_mm"]
    if distance is not None and type(distance) is not int:
        return None
    if value["valid"] and (distance is None or not 40 <= distance <= 4000):
        return None
    state = value.get("state")
    if (not isinstance(state, str) or not 1 <= len(state) <= 64
            or not all(ch.isascii() and (ch.isalnum() or ch in "_-") for ch in state)):
        return None
    if value["valid"] and ((value["raw_status"] & 31) != 9 or state != "ranging" or value["error"] != 0):
        return None
    return Sample(value["v"], value["seq"], value["uptime_ms"], value["valid"],
                  distance, value["raw_status"], state, value["error"])
