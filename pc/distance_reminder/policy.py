"""Distance reminder decisions driven exclusively by PC monotonic timestamps."""

from collections import deque
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    threshold_mm: int = 400
    hysteresis_mm: int = 80
    near_duration_ms: int = 5000
    cooldown_ms: int = 60000
    sample_timeout_ms: int = 1500

    def __post_init__(self) -> None:
        limits = (
            ("threshold_mm", 40, 4000),
            ("hysteresis_mm", 0, 3960),
            ("near_duration_ms", 1, 3_600_000),
            ("cooldown_ms", 0, 86_400_000),
            ("sample_timeout_ms", 1, 60_000),
        )
        for name, minimum, maximum in limits:
            value = getattr(self, name)
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(f"{name} must be an integer")
            if not minimum <= value <= maximum:
                raise ValueError(f"{name} must be between {minimum} and {maximum}")
        if self.threshold_mm + self.hysteresis_mm > 4000:
            raise ValueError("threshold_mm + hysteresis_mm must not exceed 4000")


class ReminderPolicy:
    """Median-of-three proximity latch with continuous dwell and global cooldown.

    The third valid sample establishes the first filtered distance. Dwell starts
    when that filtered distance first falls below the threshold. An initial
    filtered value in the hysteresis band is safe until proximity is established.
    A discontinuity clears the samples and dwell; only a clock rollback resets
    the reminder timestamp, since its previous monotonic epoch is no longer valid.
    """

    def __init__(self, config: Config) -> None:
        if not isinstance(config, Config):
            raise TypeError("config must be a Config")
        self.config = config
        self._samples: deque[int] = deque(maxlen=3)
        self._paused = False
        self._state = "waiting"
        self._filtered_mm: int | None = None
        self._near_since_ms: int | None = None
        self._near_elapsed_ms = 0
        self._last_sample_ms: int | None = None
        self._last_now_ms: int | None = None
        self._last_reminder_ms: int | None = None

    @property
    def state(self) -> str:
        return self._state

    @property
    def filtered_mm(self) -> int | None:
        return self._filtered_mm

    @property
    def near_elapsed_ms(self) -> int:
        return self._near_elapsed_ms

    def invalidate(self) -> None:
        """Discard measurements and dwell while preserving pause and cooldown."""
        self._samples.clear()
        self._filtered_mm = None
        self._near_since_ms = None
        self._near_elapsed_ms = 0
        self._last_sample_ms = None
        self._state = "paused" if self._paused else "waiting"

    def _observe_time(self, now_ms: int) -> bool:
        if isinstance(now_ms, bool) or not isinstance(now_ms, int):
            raise TypeError("now_ms must be an integer PC monotonic timestamp")
        if now_ms < 0:
            raise ValueError("now_ms must not be negative")
        rolled_back = self._last_now_ms is not None and now_ms < self._last_now_ms
        self._last_now_ms = now_ms
        if rolled_back:
            self.invalidate()
            self._last_reminder_ms = None
        return rolled_back

    def set_paused(self, paused: bool, now_ms: int) -> None:
        if not isinstance(paused, bool):
            raise TypeError("paused must be a bool")
        self._observe_time(now_ms)
        if paused != self._paused:
            self._paused = paused
            self.invalidate()

    def feed(self, distance_mm: int | None, now_ms: int) -> bool:
        """Consume one new measurement; return True only when a reminder is due.

        Invalid distances and the first measurement after a clock rollback never
        trigger. The first valid measurement after a sampling gap starts a fresh
        three-sample window. Paused samples never fill that window.
        """
        if self._observe_time(now_ms) or self._paused:
            return False
        if (
            isinstance(distance_mm, bool)
            or not isinstance(distance_mm, int)
            or not 40 <= distance_mm <= 4000
        ):
            self.invalidate()
            return False
        if (
            self._last_sample_ms is not None
            and now_ms - self._last_sample_ms > self.config.sample_timeout_ms
        ):
            self.invalidate()
        self._last_sample_ms = now_ms
        self._samples.append(distance_mm)
        if len(self._samples) < 3:
            return False

        self._filtered_mm = sorted(self._samples)[1]
        if self._state == "near":
            if self._filtered_mm >= self.config.threshold_mm + self.config.hysteresis_mm:
                self._state = "safe"
                self._near_since_ms = None
                self._near_elapsed_ms = 0
        elif self._filtered_mm < self.config.threshold_mm:
            self._state = "near"
            self._near_since_ms = now_ms
        else:
            self._state = "safe"

        if self._state != "near":
            return False
        assert self._near_since_ms is not None
        self._near_elapsed_ms = now_ms - self._near_since_ms
        if self._near_elapsed_ms < self.config.near_duration_ms:
            return False
        if (
            self._last_reminder_ms is not None
            and now_ms - self._last_reminder_ms < self.config.cooldown_ms
        ):
            return False
        self._last_reminder_ms = now_ms
        return True
