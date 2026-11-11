// Execute the actual retained-record implementation and panic wrapper. No raw
// stack or diagnostic strings are dereferenced, including deliberately invalid ones.
#include "../main/apps/common/panic_diagnostics.cpp"
#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace codex_weekly::network_debug {
#include "panic_status.inc"
}

std::uintptr_t botifier_test_dram_low = 0, botifier_test_dram_high = 0;
extern "C" { bool g_panic_abort = false; }
namespace {
unsigned checks = 0, original_calls = 0;
panic_info_t* original_info = nullptr;
esp_reset_reason_t fake_reset = ESP_RST_POWERON;
void check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)
struct Arena { panic_info_t info{}; XtExcFrame frame{}; };
void configure(Arena& arena)
{
    botifier_test_dram_low = reinterpret_cast<std::uintptr_t>(&arena);
    botifier_test_dram_high = botifier_test_dram_low + sizeof(arena);
    arena.info.core = 1; arena.info.exception = PANIC_EXCEPTION_IWDT;
    arena.info.addr = nullptr; arena.info.frame = &arena.frame; arena.info.pseudo_excause = true;
    arena.info.reason = reinterpret_cast<const char*>(1);
    arena.info.description = reinterpret_cast<const char*>(3);
    arena.info.details = reinterpret_cast<panic_info_dump_fn_t>(5);
    arena.info.state = reinterpret_cast<panic_info_dump_fn_t>(7);
    arena.frame.pc = 0x40371234; arena.frame.exccause = 6; arena.frame.excvaddr = 0x12345678;
    arena.frame.a0 = 0x820abcde; arena.frame.a1 = 0x3fcda100;
}
void fresh()
{
    fake_reset = ESP_RST_POWERON; botifier_panic_boot();
    g_panic_abort = false; original_calls = 0; original_info = nullptr;
}
void capture_and_reboot()
{
    Arena arena; configure(arena);
    CHECK(botifier_panic_snapshot().version == 0);
    __wrap_esp_panic_handler(&arena.info);
    CHECK(original_calls == 1 && original_info == &arena.info); // recorder not ready, original still called
    fresh();
    CHECK(botifier_panic_snapshot().version == 1);
    CHECK(!botifier_panic_snapshot().current.valid && !botifier_panic_snapshot().previous.valid);
    g_panic_abort = true;
    __wrap_esp_panic_handler(&arena.info);
    CHECK(original_calls == 1 && original_info == &arena.info); // return path is preserved
    const auto record = botifier_panic_snapshot().current;
    CHECK(record.valid && record.info_valid && record.frame_valid);
    CHECK(record.core == 1 && record.exception == PANIC_EXCEPTION_IWDT && record.pseudo_excause == 1);
    CHECK(record.addr == 0 && record.aborted == 1); // null addr is valid for pseudo causes
    CHECK(record.pc == arena.frame.pc && record.exccause == arena.frame.exccause);
    CHECK(record.excvaddr == arena.frame.excvaddr && record.a0 == arena.frame.a0 && record.a1 == arena.frame.a1);
    CHECK(arena.info.exception == PANIC_EXCEPTION_IWDT && arena.info.addr == nullptr);
    arena.info.core = 0; arena.frame.pc = 0x42000000;
    __wrap_esp_panic_handler(&arena.info);
    CHECK(original_calls == 2 && botifier_panic_snapshot().current.pc == record.pc);
    fake_reset = ESP_RST_INT_WDT; botifier_panic_boot();
    CHECK(!botifier_panic_snapshot().current.valid && botifier_panic_snapshot().previous.valid);
    CHECK(botifier_panic_snapshot().previous.pc == record.pc && botifier_panic_snapshot().previous.core == 1);
    __wrap_esp_panic_handler(&arena.info);
    CHECK(botifier_panic_snapshot().current.pc == arena.frame.pc);
    CHECK(botifier_panic_snapshot().previous.pc == record.pc);
    fake_reset = ESP_RST_POWERON; botifier_panic_boot();
    CHECK(!botifier_panic_snapshot().previous.valid && !botifier_panic_snapshot().current.valid);
}
void unsafe_sources()
{
    Arena arena; configure(arena);
    const auto low = botifier_test_dram_low, high = botifier_test_dram_high;
    for (const auto bad : {std::uintptr_t{0}, std::uintptr_t{1}, low - 4, high - 4,
                          reinterpret_cast<std::uintptr_t>(&arena.frame) + 1,
                          std::numeric_limits<std::uintptr_t>::max() - 3}) {
        fresh(); arena.info.frame = reinterpret_cast<void*>(bad);
        __wrap_esp_panic_handler(&arena.info);
        const auto record = botifier_panic_snapshot().current;
        CHECK(record.valid && record.info_valid && !record.frame_valid);
        CHECK(record.pc == 0 && record.exccause == 0 && record.excvaddr == 0 && record.a0 == 0 && record.a1 == 0);
        CHECK(original_calls == 1 && original_info == &arena.info);
    }
    // Address arithmetic itself is checked at the exact valid/invalid boundaries.
    CHECK(internal_structure(reinterpret_cast<void*>(low), sizeof(arena)));
    CHECK(!internal_structure(reinterpret_cast<void*>(low), sizeof(arena) + 1));
    CHECK(!internal_structure(reinterpret_cast<void*>(low), std::numeric_limits<std::size_t>::max()));
    for (const auto bad : {std::uintptr_t{0}, std::uintptr_t{1}, low - 4, high - 4}) {
        fresh(); auto* pointer = reinterpret_cast<panic_info_t*>(bad);
        __wrap_esp_panic_handler(pointer);
        const auto record = botifier_panic_snapshot().current;
        CHECK(record.valid && !record.info_valid && !record.frame_valid);
        CHECK(record.core == -1 && record.exception == -1 && record.pc == 0);
        CHECK(original_calls == 1 && original_info == pointer);
    }
}
void corrupt_retention()
{
    Arena arena; configure(arena); fresh(); __wrap_esp_panic_handler(&arena.info);
    for (std::size_t i = 0; i < PanicWordCount; ++i) {
        const auto original = load(panic_retained.words[i]);
        store(panic_retained.words[i], original ^ 1u);
        CHECK(!botifier_panic_snapshot().current.valid);
        store(panic_retained.words[i], original);
    }
    store(panic_retained.words[Sequence], 1); // interrupted write never appears valid
    fake_reset = ESP_RST_PANIC; botifier_panic_boot();
    CHECK(!botifier_panic_snapshot().previous.valid);
    __wrap_esp_panic_handler(&arena.info);
    store(panic_retained.words[Version], 99);
    botifier_panic_boot(); CHECK(!botifier_panic_snapshot().previous.valid);
}
void first_writer_only()
{
    std::array<Arena, 2> arenas;
    configure(arenas[0]); configure(arenas[1]);
    botifier_test_dram_low = reinterpret_cast<std::uintptr_t>(arenas.data());
    botifier_test_dram_high = botifier_test_dram_low + sizeof(arenas);
    arenas[0].info.core = 0; arenas[0].frame.pc = 0x40370000;
    arenas[1].info.core = 1; arenas[1].frame.pc = 0x40371111;
    fresh();
    std::thread first([&] { record_panic(&arenas[0].info); });
    std::thread second([&] { record_panic(&arenas[1].info); });
    first.join(); second.join();
    const auto record = botifier_panic_snapshot().current;
    CHECK(record.valid && record.info_valid && record.frame_valid);
    CHECK((record.core == 0 && record.pc == 0x40370000) || (record.core == 1 && record.pc == 0x40371111));
    CHECK(load(panic_claim.word) == 1);
}
void status_shape()
{
    botifier_panic_record_t record{1, 1, 1, 4, 0xffffffffu, 1, 1, 1,
                                  0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    JsonDocument doc;
    auto panic = doc["panic"].to<JsonObject>(); panic["v"] = 1;
    codex_weekly::network_debug::panic_record(panic["current"].to<JsonObject>(), record);
    codex_weekly::network_debug::panic_record(panic["previous"].to<JsonObject>(), record);
    std::string encoded; serializeJson(doc, encoded);
    CHECK(!doc.overflowed() && encoded.size() < 768);
    CHECK(panic["current"].as<JsonObject>().size() == 13);
    CHECK(panic["previous"]["pc"].as<std::uint32_t>() == 0xffffffffu);
    CHECK(encoded.find("reason") == std::string::npos && encoded.find("description") == std::string::npos);
}
} // namespace

esp_reset_reason_t esp_reset_reason() { return fake_reset; }
extern "C" void __real_esp_panic_handler(panic_info_t* info) { ++original_calls; original_info = info; }

int main()
{
    try {
        capture_and_reboot(); unsafe_sources(); corrupt_retention(); first_writer_only(); status_shape();
        std::cout << checks << " panic diagnostic checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
