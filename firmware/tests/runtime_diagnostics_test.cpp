// Actual RTC implementation; only the heap/time/reset inputs are substituted.
// This validates retention logic, not physical RTC persistence across a board reset.
#include "../main/apps/common/runtime_diagnostics.cpp"
#include <iostream>
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
unsigned checks = 0, heap_calls = 0, timer_calls = 0;
esp_reset_reason_t reset_reason = ESP_RST_POWERON;
std::uint64_t time_ms = 1234;
void check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)

void clean_boot()
{
    reset_reason = ESP_RST_POWERON;
    botifier_diag_boot();
    heap_calls = timer_calls = 0;
}

void boot_and_sample()
{
    // Before app_main initialization, callbacks cannot invent a valid trace.
    CHECK(botifier_diag_snapshot().version == 0);
    botifier_diag_mark(0, 9, -1);
    clean_boot();
    auto state = botifier_diag_snapshot();
    CHECK(state.version == 1 && state.current.valid == 1 && state.previous.valid == 0);
    CHECK(state.current.boot_counter == 1 && state.current.reset_reason == ESP_RST_POWERON);
    CHECK(state.current.lanes[0].valid == 1 && state.current.lanes[1].valid == 1);
    CHECK(state.current.lanes[0].marks == 0);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 15, -42);
    CHECK(heap_calls == 0 && timer_calls == 0);
    botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
    state = botifier_diag_snapshot();
    const auto& lane = state.current.lanes[0];
    CHECK(lane.valid && lane.stage == 15 && lane.error == -42 && lane.marks == 1);
    CHECK(lane.sample_stage == 15 && lane.sample_mark == 1 && lane.sample_uptime_ms == time_ms);
    CHECK(lane.free_internal == 12000 && lane.largest_internal == 5000 && lane.minimum_internal == 3000);
    CHECK(heap_calls == 3 && timer_calls == 1);
    botifier_diag_mark(0xffffffffu, 123, -2);
    botifier_diag_sample_memory(0xffffffffu);
    CHECK(heap_calls == 3 && timer_calls == 1);
    CHECK(botifier_diag_snapshot().current.lanes[1].marks == 0);

    reset_reason = ESP_RST_INT_WDT;
    botifier_diag_boot();
    state = botifier_diag_snapshot();
    CHECK(state.current.boot_counter == 2 && state.current.reset_reason == ESP_RST_INT_WDT);
    CHECK(state.previous.valid && state.previous.reset_reason == ESP_RST_POWERON);
    CHECK(state.previous.lanes[0].stage == 15 && state.previous.lanes[0].error == -42);
    CHECK(state.current.lanes[0].marks == 0 && state.current.lanes[0].sample_mark == 0);
    botifier_diag_mark(0, 99, 0);
    CHECK(botifier_diag_snapshot().previous.lanes[0].stage == 15);
    reset_reason = ESP_RST_POWERON;
    botifier_diag_boot();
    CHECK(botifier_diag_snapshot().previous.valid == 0);
    CHECK(botifier_diag_snapshot().current.boot_counter == 1);
}

void corrupt_and_interrupted()
{
    using namespace botifier_diagnostics::detail;
    clean_boot();
    botifier_diag_mark(0, 4, 0); botifier_diag_mark(1, 8, -7);
    auto& lane = retained.lanes[0];
    // Every lane word is either sequence-checked or checksum-protected.
    for (std::size_t i = 0; i < WordCount; ++i) {
        const auto original = load(lane.words[i]);
        store(lane.words[i], original ^ 1u);
        const auto damaged = botifier_diag_snapshot();
        CHECK(damaged.current.valid && !damaged.current.lanes[0].valid);
        CHECK(damaged.current.lanes[1].valid && damaged.current.lanes[1].stage == 8);
        store(lane.words[i], original);
    }
    store(lane.words[Sequence], 5); // interrupted write: must never spin/wait
    reset_reason = ESP_RST_PANIC; botifier_diag_boot();
    auto state = botifier_diag_snapshot();
    CHECK(state.previous.valid && !state.previous.lanes[0].valid && state.previous.lanes[1].valid);
    CHECK(state.current.lanes[0].valid && state.current.lanes[0].marks == 0);
    store(retained.version, kVersion + 1);
    botifier_diag_boot();
    CHECK(!botifier_diag_snapshot().previous.valid && botifier_diag_snapshot().current.boot_counter == 1);
    store(retained.magic, 0x12345678);
    botifier_diag_boot();
    CHECK(!botifier_diag_snapshot().previous.valid);
    store(retained.checksum, load(retained.checksum) ^ 1);
    botifier_diag_boot();
    CHECK(!botifier_diag_snapshot().previous.valid);
}

void concurrent_consistency()
{
    using namespace botifier_diagnostics::detail;
    clean_boot();
    store(gates[0].word, 1);
    botifier_diag_mark(0, 17, -17);
    CHECK(botifier_diag_snapshot().current.lanes[0].marks == 0); // bounded nonblocking contention
    store(gates[0].word, 0);
    botifier_diag_mark(0, 21, -21);
    const auto older = botifier_diag_snapshot().current.lanes[0];
    botifier_diag_mark(0, 22, -22);
    sample(retained.lanes[0], gates[0], older.stage, older.marks, 456, 10, 9, 8);
    const auto newer = botifier_diag_snapshot().current.lanes[0];
    CHECK(newer.stage == 22 && newer.error == -22 && newer.marks == 2);
    CHECK(newer.sample_stage == 21 && newer.sample_mark == 1);

    auto writer = [](unsigned base) {
        for (unsigned n = 1; n <= 20000; ++n)
            botifier_diag_mark(0, base + n, -static_cast<std::int32_t>(base + n));
    };
    std::thread first(writer, 100000), second(writer, 200000);
    bool consistent = true;
    for (unsigned n = 0; n < 20000; ++n) {
        const auto lane = botifier_diag_snapshot().current.lanes[0];
        if (lane.valid && (lane.error != -static_cast<std::int32_t>(lane.stage) || (lane.sequence & 1u)))
            consistent = false;
    }
    first.join(); second.join();
    CHECK(consistent);
    const auto final = botifier_diag_snapshot();
    CHECK(final.current.lanes[0].valid && final.current.lanes[0].marks > 2);
    CHECK(final.current.lanes[1].valid && final.current.lanes[1].marks == 0);
    CHECK(heap_calls == 0 && timer_calls == 0);
}
} // namespace

esp_reset_reason_t esp_reset_reason() { return reset_reason; }
std::int64_t esp_timer_get_time() { ++timer_calls; return static_cast<std::int64_t>(time_ms * 1000); }
std::size_t heap_caps_get_free_size(unsigned) { ++heap_calls; return 12000; }
std::size_t heap_caps_get_largest_free_block(unsigned) { ++heap_calls; return 5000; }
std::size_t heap_caps_get_minimum_free_size(unsigned) { ++heap_calls; return 3000; }

int main()
{
    try {
        boot_and_sample(); corrupt_and_interrupted(); concurrent_consistency();
        std::cout << checks << " runtime diagnostic checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
