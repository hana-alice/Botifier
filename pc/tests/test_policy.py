import dataclasses
import unittest

from pc.distance_reminder.policy import Config, ReminderPolicy


class ConfigTests(unittest.TestCase):
    def test_defaults_and_immutable_configuration(self):
        config = Config()
        self.assertEqual(
            (config.threshold_mm, config.hysteresis_mm, config.near_duration_ms,
             config.cooldown_ms, config.sample_timeout_ms),
            (400, 80, 5000, 60000, 1500),
        )
        with self.assertRaises(dataclasses.FrozenInstanceError):
            config.threshold_mm = 500

    def test_hardware_endpoints_and_duration_boundaries_are_available(self):
        Config(threshold_mm=40, hysteresis_mm=3960, near_duration_ms=1,
               cooldown_ms=0, sample_timeout_ms=1)
        Config(threshold_mm=4000, hysteresis_mm=0, near_duration_ms=3_600_000,
               cooldown_ms=86_400_000, sample_timeout_ms=60_000)

    def test_out_of_range_configuration_is_rejected(self):
        cases = (
            {"threshold_mm": 39}, {"threshold_mm": 4001},
            {"hysteresis_mm": -1}, {"hysteresis_mm": 3961},
            {"threshold_mm": 3950, "hysteresis_mm": 80},
            {"near_duration_ms": 0}, {"near_duration_ms": 3_600_001},
            {"cooldown_ms": -1}, {"cooldown_ms": 86_400_001},
            {"sample_timeout_ms": 0}, {"sample_timeout_ms": 60_001},
        )
        for values in cases:
            with self.subTest(values=values), self.assertRaises(ValueError):
                Config(**values)

    def test_all_configuration_fields_reject_bools_and_non_integers(self):
        for field in dataclasses.fields(Config):
            for value in (True, False, 400.0, "400", None):
                with self.subTest(field=field.name, value=value):
                    with self.assertRaises(TypeError):
                        Config(**{field.name: value})


class ReminderPolicyTests(unittest.TestCase):
    def setUp(self):
        self.config = Config(near_duration_ms=500, cooldown_ms=2000,
                             sample_timeout_ms=250)
        self.policy = ReminderPolicy(self.config)

    def feed_near(self, timestamps):
        return [now for now in timestamps if self.policy.feed(300, now)]

    def assert_waiting(self):
        self.assertEqual(self.policy.state, "waiting")
        self.assertIsNone(self.policy.filtered_mm)
        self.assertEqual(self.policy.near_elapsed_ms, 0)

    def test_three_fresh_measurements_are_required_before_dwell_starts(self):
        self.assert_waiting()
        self.assertFalse(self.policy.feed(300, 0))
        self.assertFalse(self.policy.feed(300, 100))
        self.assert_waiting()
        self.assertFalse(self.policy.feed(300, 200))
        self.assertEqual(self.policy.state, "near")
        self.assertEqual(self.policy.filtered_mm, 300)
        self.assertEqual(self.policy.near_elapsed_ms, 0)

    def test_default_five_second_dwell_triggers_at_its_boundary(self):
        policy = ReminderPolicy(Config())
        reminders = [now for now in range(0, 9001, 1000) if policy.feed(300, now)]
        self.assertEqual(reminders, [7000])
        self.assertEqual(policy.near_elapsed_ms, 7000)

    def test_one_close_spike_does_not_interrupt_a_safe_trajectory(self):
        trajectory = [(0, 600), (100, 600), (200, 600), (300, 100),
                      (400, 600), (500, 600), (600, 600), (700, 600),
                      (800, 600), (900, 600), (1000, 600)]
        reminders = [now for now, distance in trajectory if self.policy.feed(distance, now)]
        self.assertEqual(reminders, [])
        self.assertEqual(self.policy.state, "safe")
        self.assertEqual(self.policy.filtered_mm, 600)
        self.assertEqual(self.policy.near_elapsed_ms, 0)

    def test_median_delays_proximity_and_rejects_one_far_spike(self):
        trajectory = [(0, 600), (100, 600), (200, 600), (300, 300),
                      (400, 300), (500, 300), (600, 300), (700, 600),
                      (800, 300), (900, 300)]
        reminders = [now for now, distance in trajectory if self.policy.feed(distance, now)]
        self.assertEqual(reminders, [900])
        self.assertEqual(self.policy.filtered_mm, 300)

    def test_invalid_measurements_break_dwell_and_require_three_new_samples(self):
        for invalid in (None, 39, 4001, -1, True, False, 300.0, "300"):
            with self.subTest(distance=invalid):
                self.policy = ReminderPolicy(self.config)
                self.assertEqual(self.feed_near(range(0, 601, 100)), [])
                self.assertFalse(self.policy.feed(invalid, 650))
                self.assert_waiting()
                self.assertEqual(self.feed_near([700, 800]), [])
                self.assert_waiting()
                self.assertEqual(self.feed_near(range(900, 1401, 100)), [1400])

    def test_sampling_gap_cannot_count_as_continuous_proximity(self):
        self.assertEqual(self.feed_near(range(0, 601, 100)), [])
        self.assertFalse(self.policy.feed(300, 900))
        self.assert_waiting()
        self.assertFalse(self.policy.feed(300, 1000))
        self.assert_waiting()
        self.assertEqual(self.feed_near(range(1100, 1601, 100)), [1600])

    def test_gap_equal_to_timeout_is_still_continuous(self):
        self.assertEqual(self.feed_near([0, 100, 200, 450, 700]), [700])

    def test_hysteresis_retains_near_until_exit_then_retains_safe_in_dead_band(self):
        self.assertEqual(self.feed_near(range(0, 701, 100)), [700])
        for now in (800, 900):
            self.assertFalse(self.policy.feed(430, now))
        self.assertEqual(self.policy.filtered_mm, 430)
        self.assertEqual(self.policy.state, "near")
        for now in (1000, 1100):
            self.assertFalse(self.policy.feed(480, now))
        self.assertEqual(self.policy.state, "safe")
        self.assertEqual(self.policy.near_elapsed_ms, 0)
        for now in (1200, 1300):
            self.assertFalse(self.policy.feed(430, now))
        self.assertEqual(self.policy.state, "safe")
        for now in (1400, 1500):
            self.assertFalse(self.policy.feed(399, now))
        self.assertEqual(self.policy.state, "near")
        self.assertEqual(self.policy.near_elapsed_ms, 0)

    def test_threshold_equality_and_initial_dead_band_are_safe(self):
        for distance in (400, 430, 480, 4000):
            with self.subTest(distance=distance):
                policy = ReminderPolicy(self.config)
                for now in range(0, 1001, 100):
                    self.assertFalse(policy.feed(distance, now))
                self.assertEqual(policy.state, "safe")

    def test_persistent_proximity_can_repeat_after_global_cooldown(self):
        self.assertEqual(self.feed_near(range(0, 4801, 100)), [700, 2700, 4700])
        self.assertEqual(self.policy.near_elapsed_ms, 4600)

    def test_invalidation_preserves_cooldown_after_reconnection(self):
        self.assertEqual(self.feed_near(range(0, 701, 100)), [700])
        self.policy.invalidate()
        self.assert_waiting()
        self.assertEqual(self.feed_near(range(800, 2801, 100)), [2700])

    def test_invalid_sample_preserves_cooldown(self):
        self.assertEqual(self.feed_near(range(0, 701, 100)), [700])
        self.assertFalse(self.policy.feed(None, 750))
        self.assertEqual(self.feed_near(range(800, 2801, 100)), [2700])

    def test_pause_discards_dwell_and_samples_until_resumed(self):
        self.assertEqual(self.feed_near([0, 100, 200, 300]), [])
        self.policy.set_paused(True, 350)
        self.assertEqual(self.policy.state, "paused")
        self.assertEqual(self.feed_near([400, 500]), [])
        self.assertIsNone(self.policy.filtered_mm)
        self.assertEqual(self.policy.near_elapsed_ms, 0)
        self.policy.set_paused(False, 600)
        self.assert_waiting()
        self.assertEqual(self.feed_near(range(600, 1301, 100)), [1300])

    def test_pause_and_resume_cannot_bypass_cooldown(self):
        self.assertEqual(self.feed_near(range(0, 701, 100)), [700])
        self.policy.set_paused(True, 750)
        self.policy.invalidate()
        self.assertEqual(self.policy.state, "paused")
        self.policy.set_paused(False, 800)
        self.assertEqual(self.feed_near(range(800, 2801, 100)), [2700])

    def test_repeated_resume_call_does_not_restart_existing_dwell(self):
        self.assertEqual(self.feed_near([0, 100, 200, 300]), [])
        self.policy.set_paused(False, 350)
        self.assertEqual(self.feed_near([400, 500, 600, 700]), [700])

    def test_clock_rollback_discards_current_sample_and_restarts_time_epoch(self):
        self.assertEqual(self.feed_near(range(0, 701, 100)), [700])
        self.assertFalse(self.policy.feed(300, 650))
        self.assert_waiting()
        self.assertEqual(self.feed_near(range(750, 1451, 100)), [1450])

    def test_clock_rollback_during_pause_stays_paused(self):
        self.policy.set_paused(True, 1000)
        self.assertFalse(self.policy.feed(300, 900))
        self.assertEqual(self.policy.state, "paused")
        self.policy.set_paused(False, 950)
        self.assert_waiting()

    def test_invalid_clock_or_pause_types_are_rejected(self):
        for now in (True, False, 1.5, "100", None):
            with self.subTest(now=now):
                with self.assertRaises(TypeError):
                    self.policy.feed(300, now)
                with self.assertRaises(TypeError):
                    self.policy.set_paused(True, now)
        with self.assertRaises(ValueError):
            self.policy.feed(300, -1)
        for paused in (0, 1, "yes", None):
            with self.subTest(paused=paused), self.assertRaises(TypeError):
                self.policy.set_paused(paused, 0)


if __name__ == "__main__":
    unittest.main()
