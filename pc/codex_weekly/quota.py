"""Validated weekly snapshots and the small ASCII USB protocol."""

from dataclasses import dataclass
import json
import math
import threading
import time


TTL_MS = 900_000
U32 = 0xffffffff


class QuotaError(Exception):
    """Fixed, credential-free reason; never include an upstream message."""

    def __init__(self, code, rpc_code=None):
        super().__init__(code)
        self.code = code
        self.rpc_code = rpc_code


def uint(value):
    return type(value) is int and 0 <= value <= U32


@dataclass(frozen=True)
class WeeklyQuota:
    remaining_percent: int
    reset_at: int
    observed_at: int
    observed_monotonic: float

    def remaining_ttl_ms(self, wall, monotonic):
        if wall < self.observed_at or monotonic < self.observed_monotonic:
            return 0
        return max(0, math.floor(min(
            (self.observed_at + TTL_MS / 1000 - wall) * 1000,
            (self.reset_at - wall) * 1000,
            TTL_MS - (monotonic - self.observed_monotonic) * 1000)))

    def fresh(self, wall, monotonic):
        return self.remaining_ttl_ms(wall, monotonic) > 0


def select_weekly(result, observed_at, observed_monotonic):
    if not uint(observed_at) or not isinstance(result, dict):
        raise QuotaError('invalid_metadata')
    buckets = result.get('rateLimitsByLimitId')
    if buckets is not None:
        if not isinstance(buckets, dict) or 'codex' not in buckets:
            raise QuotaError('codex_bucket_unavailable')
        bucket = buckets['codex']
    else:
        bucket = result.get('rateLimits')
        if not isinstance(bucket, dict) or bucket.get('limitId') != 'codex':
            raise QuotaError('codex_bucket_unavailable')
    if not isinstance(bucket, dict) or bucket.get('limitId', 'codex') != 'codex':
        raise QuotaError('invalid_codex_bucket')
    windows = []
    for name in ('primary', 'secondary'):
        window = bucket.get(name)
        if window is None:
            continue
        if not isinstance(window, dict) or type(window.get('windowDurationMins')) is not int:
            raise QuotaError('invalid_window')
        if window['windowDurationMins'] == 10080:
            windows.append(window)
    if len(windows) != 1:
        raise QuotaError('weekly_window_ambiguous' if windows else 'weekly_window_unavailable')
    window = windows[0]
    used, reset = window.get('usedPercent'), window.get('resetsAt')
    if (type(used) not in (int, float) or not 0 <= used <= 100 or not math.isfinite(used)
            or not uint(reset)):
        raise QuotaError('invalid_weekly_window')
    if reset <= observed_at:
        raise QuotaError('weekly_reset_elapsed')
    return WeeklyQuota(math.floor(100 - used), reset, observed_at, observed_monotonic)


@dataclass(frozen=True)
class Frame:
    sequence: int
    quota: WeeklyQuota | None

    def encode(self, ttl_ms=TTL_MS):
        value = dict(v=1, type='weekly', available=self.quota is not None, sequence=self.sequence)
        if self.quota is not None:
            value.update(remaining_percent=self.quota.remaining_percent, reset_at=self.quota.reset_at,
                         observed_at=self.quota.observed_at, ttl_ms=ttl_ms)
        return ('SC_IDLE ' + json.dumps(value, separators=(',', ':'), allow_nan=False) + '\n').encode('ascii')


class SnapshotStore:
    """Shared one-frame mailbox. Expiry/error replace, rather than refresh, data."""

    def __init__(self, wall=time.time, monotonic=time.monotonic):
        self.wall, self.monotonic = wall, monotonic
        self._lock = threading.Lock()
        self._sequence = 0
        self._frame = self._new(None)

    def _new(self, quota):
        sequence = max(int(self.wall()), self._sequence + 1)
        if not uint(sequence):
            raise QuotaError('clock_out_of_range')
        self._sequence = sequence
        return Frame(sequence, quota)

    def publish(self, quota):
        with self._lock:
            if quota is not None and not quota.fresh(self.wall(), self.monotonic()):
                quota = None
            self._frame = self._new(quota)
            return self._frame

    def _current_locked(self, minimum_ttl_ms):
        quota = self._frame.quota
        if quota is not None and quota.remaining_ttl_ms(self.wall(), self.monotonic()) < max(1, minimum_ttl_ms):
            self._frame = self._new(None)
        return self._frame

    def current(self):
        with self._lock:
            return self._current_locked(0)

    def packet(self):
        """Shorten every retransmission's TTL, including after a device reboot.

        No-RTC firmware anchors its first packet at observed_at. A shortened TTL
        prevents that bootstrap from granting an old cache a fresh 15 minutes.
        Existing firmware anchors can expire early; they can never be extended.
        """
        with self._lock:
            frame = self._frame
            ttl = frame.quota.remaining_ttl_ms(self.wall(), self.monotonic()) if frame.quota else TTL_MS
            if frame.quota is not None and ttl < 60000:
                self._frame = frame = self._new(None)
            return frame.sequence, frame.encode(ttl)


def safe_summary(frame):
    result = dict(available=frame.quota is not None)
    if frame.quota is not None:
        result.update(remaining_percent=frame.quota.remaining_percent, reset_at=frame.quota.reset_at,
                      observed_at=frame.quota.observed_at)
    return result
