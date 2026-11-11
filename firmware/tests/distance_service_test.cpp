// Exercise the production process-lifetime task, publication and schedule
// integration. Only platform I/O and the separately-tested sensor driver are
// replaced. Including the source also permits a clean boot between scenarios.
#include "../main/distance_sensor/service.cpp"

#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>

struct FakeDistanceBus {};
struct FakeDistanceDevice {};

namespace {

// Independently fixed fixture: Monday 2026-11-02 00:00:00 in Beijing.
constexpr std::int64_t monday_midnight_utc = 1793548800;
constexpr std::int64_t local_time(int day, int hour, int minute = 0, int second = 0)
{
    return (monday_midnight_utc + day * 86400 + hour * 3600 + minute * 60 + second) * 1000;
}

unsigned checks = 0;
std::int64_t wall_ms = 0;
std::uint64_t uptime_ms = 0;
unsigned delay_calls = 0, bus_creates = 0, device_adds = 0, probes = 0;
unsigned sensor_initializes = 0, sensor_polls = 0, sensor_stops = 0, session_samples = 0;
unsigned nvs_read_opens = 0, nvs_write_opens = 0, nvs_writes = 0, nvs_commits = 0;
bool sensor_running = false;
FakeDistanceBus fake_bus;
FakeDistanceDevice fake_device;
TaskFunction_t created_task = nullptr;
void* task_parameter = nullptr;
std::function<void(TickType_t)> after_delay;
struct StopTask {};

void check(bool condition, const char* description)
{
    ++checks;
    if (!condition) throw std::runtime_error(description);
}

void reset(std::int64_t initial_wall_ms)
{
    distance_sensor::sampler_task = nullptr;
    distance_sensor::sequence = 0;
    distance_sensor::policy = distance_sensor::ProximityPolicy{};
    distance_sensor::latest_snapshot = distance_sensor::Snapshot{};
    distance_sensor::requested_config = distance_sensor::ProximityConfig{};
    distance_sensor::schedule_policy = distance_sensor::SchedulePolicy{};
    distance_sensor::applied_schedule_generation = 0;
    wall_ms = initial_wall_ms;
    uptime_ms = 1000;
    delay_calls = bus_creates = device_adds = probes = 0;
    sensor_initializes = sensor_polls = sensor_stops = session_samples = 0;
    nvs_read_opens = nvs_write_opens = nvs_writes = nvs_commits = 0;
    sensor_running = false;
    created_task = nullptr;
    task_parameter = nullptr;
    after_delay = {};
}

void jump_forward(std::int64_t new_wall_ms)
{
    check(new_wall_ms >= wall_ms, "test wall jump must advance monotonically");
    uptime_ms += static_cast<std::uint64_t>(new_wall_ms - wall_ms);
    wall_ms = new_wall_ms;
}

void check_no_alert(const char* description)
{
    const auto state = distance_sensor::snapshot();
    check(state.proximity_state != distance_sensor::ProximityState::Alert &&
              !distance_sensor::alert_active(), description);
}

void check_disabled()
{
    const auto state = distance_sensor::snapshot();
    check(!state.schedule.enabled() && !state.valid && state.raw_mm == -1 && state.filtered_mm == -1 &&
              state.near_elapsed_ms == 0 && state.proximity_state == distance_sensor::ProximityState::Waiting,
          "disabled snapshot exposes an earlier measurement or proximity latch");
    check(std::strcmp(state.sensor_state, "schedule_off") == 0, "disabled snapshot does not report schedule_off");
    check(!distance_sensor::alert_active(), "disabled service keeps its previous alert active");
}

void check_fresh_session()
{
    const auto state = distance_sensor::snapshot();
    check(state.valid && state.raw_mm == 250 && !state.stale, "fresh sample was not published by real task");
    if (session_samples < 3) {
        check(state.filtered_mm == -1 && state.proximity_state == distance_sensor::ProximityState::Waiting,
              "a restarted session reused its old median window");
        check_no_alert("fewer than three new samples revived the old alert");
    } else {
        check(state.filtered_mm == 250 && state.proximity_state == distance_sensor::ProximityState::Alert &&
                  distance_sensor::alert_active(), "three fresh near samples did not activate alert");
    }
}

void execute_task()
{
    check(distance_sensor::start() == ESP_OK && created_task != nullptr, "start did not create the real sampler task");
    check(distance_sensor::start() == ESP_ERR_INVALID_STATE, "second start created another sampler");
    bool stopped = false;
    try {
        created_task(task_parameter);
    } catch (const StopTask&) {
        stopped = true;
    }
    check(stopped, "scripted real task did not reach its terminal checkpoint");
    check(nvs_read_opens == 1 && nvs_write_opens == 0 && nvs_writes == 0 && nvs_commits == 0,
          "automatic schedule or head touch wrote persistent settings");
}

void weekday_open_close_and_outside_touch()
{
    reset(local_time(0, 9, 29, 59) + 500);
    unsigned off_delays = 0;
    bool reached_close = false;
    after_delay = [&](TickType_t ticks) {
        if (wall_ms < local_time(0, 9, 30)) {
            check(ticks == 100 && bus_creates == 0 && sensor_initializes == 0 && sensor_polls == 0,
                  "service initialized or polled before weekday opening");
            check_disabled();
            return;
        }
        if (!reached_close && sensor_initializes == 1) {
            check(ticks == 20 && distance_sensor::snapshot().schedule.mode == distance_sensor::ScheduleMode::Automatic,
                  "weekday opening failed to enter automatic ranging");
            check_fresh_session();
            if (session_samples == 3) {
                reached_close = true;
                jump_forward(local_time(0, 21, 30));
                // Read before the task resumes: consumers must already stop
                // rendering an alert at 21:30, even before sensor.stop().
                check_disabled();
            }
            return;
        }
        if (reached_close) {
            check(ticks == 100 && sensor_stops == 1 && !sensor_running && sensor_polls == 3,
                  "closing time failed to stop hardware polling");
            check_disabled();
            if (++off_delays == 2) {
                distance_sensor::enable_from_head_touch();
                check_disabled();
            }
            if (off_delays == 5) {
                check(bus_creates == 1 && device_adds == 1 && probes == 1 && sensor_initializes == 1,
                      "out-of-window touch restarted the stopped sensor");
                throw StopTask{};
            }
        }
    };
    execute_task();
}

void weekend_waits_for_touch()
{
    reset(local_time(5, 10));
    unsigned waiting = 0;
    after_delay = [&](TickType_t ticks) {
        if (!sensor_initializes) {
            check(ticks == 100 && bus_creates == 0 && device_adds == 0 && probes == 0 && sensor_polls == 0,
                  "weekend startup touched the sensor before a head press");
            check_disabled();
            if (++waiting == 4) {
                distance_sensor::enable_from_head_touch();
                check(distance_sensor::snapshot().schedule.mode == distance_sensor::ScheduleMode::Manual,
                      "weekend touch did not enable monitoring");
            }
            return;
        }
        check_fresh_session();
        if (session_samples == 3) {
            check(sensor_initializes == 1 && sensor_stops == 0, "weekend manual session unexpectedly restarted");
            throw StopTask{};
        }
    };
    execute_task();
}

void manual_session_stops_at_close_and_does_not_carry_over()
{
    reset(local_time(5, 21, 29, 59));
    distance_sensor::enable_from_head_touch();
    unsigned off_delays = 0;
    bool reached_close = false;
    after_delay = [&](TickType_t ticks) {
        if (!reached_close) {
            check_fresh_session();
            if (session_samples == 3) {
                reached_close = true;
                jump_forward(local_time(5, 21, 30));
                check_disabled();
            }
            return;
        }
        check(ticks == 100 && sensor_stops == 1 && !sensor_running && sensor_polls == 3 && sensor_initializes == 1,
              "21:30 did not stop the manual session or continued to poll");
        check_disabled();
        ++off_delays;
        if (off_delays == 1) {
            distance_sensor::enable_from_head_touch();
            check_disabled();
            jump_forward(local_time(6, 0));
            check_disabled();
        } else if (off_delays == 2) {
            jump_forward(local_time(6, 10));
            check_disabled();
        } else if (off_delays == 4) {
            throw StopTask{};
        }
    };
    execute_task();
}

void stop_and_touch_in_one_sampler_gap_cannot_revive_alert()
{
    reset(local_time(5, 21, 29, 59) + 900);
    distance_sensor::enable_from_head_touch();
    bool touched_next_day = false;
    after_delay = [&](TickType_t ticks) {
        check(ticks == 20, "immediate restart unexpectedly entered the sensor-error backoff");
        if (!touched_next_day) {
            check_fresh_session();
            if (session_samples == 3) {
                touched_next_day = true;
                const auto old_generation = distance_sensor::snapshot().schedule.generation;
                // A clock correction crosses into a new weekend day, without
                // advancing uptime enough to make the old sample stale. No
                // sampler/snapshot runs between the clock correction and press.
                wall_ms = local_time(6, 10);
                distance_sensor::enable_from_head_touch();
                const auto state = distance_sensor::snapshot();
                check(state.schedule.mode == distance_sensor::ScheduleMode::Manual &&
                          state.schedule.generation != old_generation && !state.valid && state.filtered_mm == -1,
                      "off/on within one sampler gap kept the previous sample generation");
                check_no_alert("off/on within one sampler gap revived the previous alert");
            }
            return;
        }
        check(sensor_initializes == 2 && sensor_stops == 1,
              "generation change did not stop and reinitialize the sensor");
        check_fresh_session();
        if (session_samples == 3) {
            check(sensor_polls == 6, "a new manual session inherited or skipped measurements");
            throw StopTask{};
        }
    };
    execute_task();
}

void nfc_pause_resume_and_next_weekday_open()
{
    reset(local_time(0, 10));
    unsigned phase = 0, waiting = 0;
    after_delay = [&](TickType_t ticks) {
        if (phase == 0 || phase == 2 || phase == 4) {
            check(ticks == 20, "enabled NFC session did not resume hardware polling");
            check_fresh_session();
            if (phase == 4) {
                check(distance_sensor::snapshot().schedule.mode == distance_sensor::ScheduleMode::Automatic,
                      "next weekday did not clear the temporary card pause");
                if (session_samples == 3) {
                    check(sensor_initializes == 3 && sensor_stops == 2 && sensor_polls == 9,
                          "card toggle or next weekday reused the old sensor session");
                    throw StopTask{};
                }
            } else if (session_samples == 3) {
                const auto previous = distance_sensor::snapshot().schedule.generation;
                const auto paused = distance_sensor::toggle_from_nfc();
                check(paused.mode == distance_sensor::ScheduleMode::ManualOff &&
                          paused.generation == previous + 1, "NFC did not immediately pause the active schedule");
                check_disabled();
                distance_sensor::enable_from_head_touch();
                check_disabled();
                ++phase; waiting = 0;
            }
            return;
        }
        check(ticks == 100 && !sensor_running, "NFC pause did not stop the physical sensor");
        check_disabled();
        if (++waiting != 2) return;
        if (phase == 1) {
            const auto reopened = distance_sensor::toggle_from_nfc();
            check(reopened.enabled(), "second card tap failed to reopen reminders");
        } else {
            jump_forward(local_time(1, 9, 30));
        }
        const auto fresh = distance_sensor::snapshot();
        check(fresh.schedule.enabled() && !fresh.valid && fresh.filtered_mm == -1,
              "reopening exposed the earlier measurement before new samples");
        check_no_alert("reopening revived the previous too-close alert");
        ++phase;
    };
    execute_task();
}

void two_card_toggles_in_one_sampler_gap_discard_old_alert()
{
    reset(local_time(0, 10));
    bool toggled = false;
    after_delay = [&](TickType_t ticks) {
        check(ticks == 20, "immediate NFC off/on unexpectedly remained disabled");
        if (!toggled) {
            check_fresh_session();
            if (session_samples == 3) {
                const auto generation = distance_sensor::snapshot().schedule.generation;
                check(!distance_sensor::toggle_from_nfc().enabled(), "first NFC did not disable");
                check_disabled();
                const auto reopened = distance_sensor::toggle_from_nfc();
                check(reopened.enabled() && reopened.generation == generation + 2,
                      "NFC off/on did not invalidate both measurement edges");
                check(!distance_sensor::snapshot().valid, "NFC off/on retained a valid old sample");
                check_no_alert("NFC off/on revived an old alert within one sampler gap");
                toggled = true;
            }
            return;
        }
        check(sensor_initializes == 2 && sensor_stops == 1,
              "NFC off/on failed to restart the hardware session");
        check_fresh_session();
        if (session_samples == 3) throw StopTask{};
    };
    execute_task();
}

} // namespace

namespace std {
time_t time(time_t* result)
{
    const auto now = static_cast<time_t>(wall_ms / 1000);
    if (result) *result = now;
    return now;
}
} // namespace std

std::int64_t esp_timer_get_time() { return static_cast<std::int64_t>(uptime_ms * 1000); }
const char* esp_err_to_name(esp_err_t) { return "FAKE_ERROR"; }

BaseType_t xTaskCreate(TaskFunction_t function, const char* name, std::uint32_t stack_words,
                       void* parameter, unsigned priority, TaskHandle_t* handle)
{
    check(std::strcmp(name, "distance_sensor") == 0 && stack_words == 4096 && priority == 2,
          "unexpected sampler task configuration");
    created_task = function;
    task_parameter = parameter;
    *handle = &fake_device;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks)
{
    check(++delay_calls < 1000, "task never reached scripted completion (bounded test failure)");
    wall_ms += ticks;
    uptime_ms += ticks;
    check(static_cast<bool>(after_delay), "task ran without a delay script");
    after_delay(ticks);
}

esp_err_t i2c_master_get_bus_handle(int port, i2c_master_bus_handle_t* result)
{
    check(port == I2C_NUM_0 && bus_creates == 0, "service attempted to replace an existing I2C bus");
    *result = nullptr;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* configuration, i2c_master_bus_handle_t* result)
{
    check(configuration->sda_io_num == GPIO_NUM_2 && configuration->scl_io_num == GPIO_NUM_1 &&
              configuration->flags.enable_internal_pullup, "unexpected external sensor bus configuration");
    ++bus_creates;
    *result = &fake_bus;
    return ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t* configuration,
                                  i2c_master_dev_handle_t* result)
{
    check(bus == &fake_bus && configuration->device_address == 0x29 && configuration->scl_speed_hz == 100000,
          "unexpected ToF I2C device configuration");
    ++device_adds;
    *result = &fake_device;
    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t)
{
    throw std::runtime_error("successful ranging scenarios must retain the owned bus");
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, std::uint16_t address, int timeout_ms)
{
    check(bus == &fake_bus && address == 0x29 && timeout_ms == 50, "unexpected ToF probe");
    check(distance_sensor::snapshot().schedule.enabled(), "sensor probed while monitoring was disabled");
    ++probes;
    return ESP_OK;
}

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle)
{
    check(std::strcmp(name, "sc_distance") == 0, "unexpected distance NVS namespace");
    *handle = 1;
    if (mode == NVS_READONLY) {
        ++nvs_read_opens;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    ++nvs_write_opens;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, std::size_t*) { return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t) { ++nvs_writes; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t) { ++nvs_commits; return ESP_OK; }
void nvs_close(nvs_handle_t) {}

namespace distance_sensor {

esp_err_t Vl53l1x::initialize(i2c_master_dev_handle_t device)
{
    check(device == &fake_device && !sensor_running, "sensor restarted without a stop");
    check(snapshot().schedule.enabled(), "sensor initialized while monitoring was disabled");
    ++sensor_initializes;
    session_samples = 0;
    sensor_running = true;
    return ESP_OK;
}

esp_err_t Vl53l1x::poll(Reading& reading, bool& ready)
{
    check(sensor_running, "sensor polled before initialize/after stop");
    check(snapshot().schedule.enabled(), "sensor polled while monitoring was disabled");
    ++sensor_polls;
    ++session_samples;
    reading = Reading{250, 9, true};
    ready = true;
    return ESP_OK;
}

esp_err_t Vl53l1x::stop()
{
    check(sensor_running, "sensor stop called without an active session");
    ++sensor_stops;
    sensor_running = false;
    return ESP_OK;
}

} // namespace distance_sensor

int main()
{
    try {
        weekday_open_close_and_outside_touch();
        weekend_waits_for_touch();
        manual_session_stops_at_close_and_does_not_carry_over();
        stop_and_touch_in_one_sampler_gap_cannot_revive_alert();
        nfc_pause_resume_and_next_weekday_open();
        two_card_toggles_in_one_sampler_gap_discard_old_alert();
        std::cout << "distance service integration: " << checks << " checks passed across 6 task scenarios\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "distance service integration failed: " << error.what() << '\n';
        return 1;
    }
}
