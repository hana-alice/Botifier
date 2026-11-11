#include "runtime_diagnostics.h"
#include "runtime_diagnostics_core.h"
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace {
using namespace botifier_diagnostics::detail;
RTC_NOINIT_ATTR Retained retained;
botifier_diag_boot_t previous{};
DRAM_ATTR alignas(4) std::uint32_t ready = 0;
DRAM_ATTR Gate gates[BOTIFIER_DIAG_LANE_COUNT]{};
} // namespace

extern "C" void botifier_diag_boot(void)
{
    const auto reason = esp_reset_reason();
    previous = boot(retained, static_cast<std::uint32_t>(reason), reason == ESP_RST_POWERON);
    store(ready, 1);
}

extern "C" void IRAM_ATTR botifier_diag_mark(std::uint32_t lane, std::uint32_t stage, std::int32_t error)
{
    if (lane >= BOTIFIER_DIAG_LANE_COUNT || !load(ready)) return;
    mark(retained.lanes[lane], gates[lane], stage, error);
}

extern "C" void botifier_diag_sample_memory(std::uint32_t lane)
{
    if (lane >= BOTIFIER_DIAG_LANE_COUNT || !load(ready)) return;
    const auto before = read_lane(retained.lanes[lane], load(retained.boot));
    if (!before.valid) return;
    const auto capabilities = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const auto free = heap_caps_get_free_size(capabilities);
    const auto largest = heap_caps_get_largest_free_block(capabilities);
    const auto minimum = heap_caps_get_minimum_free_size(capabilities);
    const auto uptime = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    sample(retained.lanes[lane], gates[lane], before.stage, before.marks, uptime, free, largest, minimum);
}

extern "C" botifier_diag_snapshot_t botifier_diag_snapshot(void)
{
    if (!load(ready)) return {};
    return {kVersion, read(retained), previous};
}
