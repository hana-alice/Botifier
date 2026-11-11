#pragma once

#include "runtime_diagnostics.h"
#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include <esp_attr.h>
#include <sdkconfig.h>
#include <xt_utils.h>
#if !CONFIG_IDF_TARGET_ESP32S3
#error "Review breadcrumb memory primitives before enabling another ESP target"
#endif
#define BOTIFIER_DIAG_INLINE FORCE_INLINE_ATTR
#else
#include <atomic>
#define BOTIFIER_DIAG_INLINE inline
#endif

namespace botifier_diagnostics::detail {
inline constexpr std::uint32_t kMagic = 0x42444731;
inline constexpr std::uint32_t kVersion = 1;
enum Word : std::size_t {
    Sequence, Boot, Stage, Error, Marks, SampleStage, SampleMark, SampleTime,
    Free, Largest, Minimum, Checksum, WordCount
};
// POD is essential: no C++ constructor may clear the retained RTC contents.
struct Lane { alignas(4) std::uint32_t words[WordCount]; };
struct Retained {
    alignas(4) std::uint32_t magic, version, boot, reset, checksum;
    Lane lanes[BOTIFIER_DIAG_LANE_COUNT];
};
struct Gate { alignas(4) std::uint32_t word; };
static_assert(sizeof(Retained) == 116);
static_assert(alignof(Lane) >= 4 && alignof(Retained) >= 4 && alignof(Gate) >= 4);
#ifdef ESP_PLATFORM
static_assert(XCHAL_HAVE_S32C1I == 1, "Nonblocking DRAM gate requires native CAS");
#else
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
#endif

// Every access to concurrently observed words is atomic, not just the sequence.
// ESP32-S3 uses the GCC/Xtensa inline-assembly synchronization contract, not ISO
// C++ std::atomic: this build disables compiler hardware atomics to work around
// PSRAM CAS restrictions. All shared-word accesses here go through aligned
// L32I/S32I, with MEMW for CPU ordering and a "memory" clobber for the compiler.
// No ordinary C++ load/store races with these accesses. RTC only gets load/store;
// native S32C1I is confined to explicitly internal-DRAM Gate objects below.
// The portable host branch uses real seq_cst atomic_ref operations, so its
// concurrent tests exercise the record algorithm without plain-memory data races.
BOTIFIER_DIAG_INLINE std::uint32_t load(std::uint32_t& value)
{
#ifdef ESP_PLATFORM
    std::uint32_t result;
    __asm__ __volatile__("memw\n l32i %0, %1, 0\n memw\n"
                         : "=r"(result) : "r"(&value) : "memory");
    return result;
#else
    return std::atomic_ref<std::uint32_t>(value).load(std::memory_order_seq_cst);
#endif
}
BOTIFIER_DIAG_INLINE void store(std::uint32_t& value, std::uint32_t next)
{
#ifdef ESP_PLATFORM
    __asm__ __volatile__("memw\n s32i %1, %0, 0\n memw\n"
                         : : "r"(&value), "r"(next) : "memory");
#else
    std::atomic_ref<std::uint32_t>(value).store(next, std::memory_order_seq_cst);
#endif
}
BOTIFIER_DIAG_INLINE std::uint32_t mix(std::uint32_t hash, std::uint32_t value)
{
    return (hash ^ value) * 16777619u;
}
BOTIFIER_DIAG_INLINE std::uint32_t lane_checksum(Lane& lane, std::uint32_t sequence)
{
    auto hash = mix(kMagic, sequence);
    for (std::size_t i = Boot; i < Checksum; ++i) hash = mix(hash, load(lane.words[i]));
    return hash;
}
inline std::uint32_t header_checksum(std::uint32_t boot, std::uint32_t reset)
{
    return mix(mix(mix(kMagic, kVersion), boot), reset);
}

inline botifier_diag_lane_t read_lane(Lane& lane, std::uint32_t boot)
{
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto sequence = load(lane.words[Sequence]);
        if (sequence & 1u) continue;
        std::uint32_t words[WordCount]{};
        words[Sequence] = sequence;
        for (std::size_t i = Boot; i < WordCount; ++i) words[i] = load(lane.words[i]);
        if (sequence != load(lane.words[Sequence])) continue;
        auto hash = mix(kMagic, sequence);
        for (std::size_t i = Boot; i < Checksum; ++i) hash = mix(hash, words[i]);
        if (words[Boot] != boot || words[Checksum] != hash) return {};
        return {1, sequence, words[Stage], static_cast<std::int32_t>(words[Error]), words[Marks],
                words[SampleStage], words[SampleMark], words[SampleTime],
                words[Free], words[Largest], words[Minimum]};
    }
    return {};
}
inline botifier_diag_boot_t read(Retained& retained)
{
    const auto boot = load(retained.boot), reset = load(retained.reset);
    if (load(retained.magic) != kMagic || load(retained.version) != kVersion ||
        load(retained.checksum) != header_checksum(boot, reset)) return {};
    botifier_diag_boot_t result{};
    result.valid = 1; result.boot_counter = boot; result.reset_reason = reset;
    for (std::size_t i = 0; i < BOTIFIER_DIAG_LANE_COUNT; ++i)
        result.lanes[i] = read_lane(retained.lanes[i], boot);
    return result;
}
inline botifier_diag_boot_t boot(Retained& retained, std::uint32_t reset, bool power_on)
{
    const auto previous = power_on ? botifier_diag_boot_t{} : read(retained);
    const auto count = previous.valid ? previous.boot_counter + 1u : 1u;
    // Only called before any task/callback can use this module.
    store(retained.magic, 0);
    store(retained.version, kVersion); store(retained.boot, count); store(retained.reset, reset);
    for (auto& lane : retained.lanes) {
        for (auto& word : lane.words) store(word, 0);
        store(lane.words[Boot], count);
        store(lane.words[Checksum], lane_checksum(lane, 0));
    }
    store(retained.checksum, header_checksum(count, reset));
    store(retained.magic, kMagic);
    return previous;
}

// The writer gate is ordinary internal DRAM, never an atomic RMW on RTC memory.
// A pre-empting writer drops its best-effort mark instead of spinning in a critical section.
BOTIFIER_DIAG_INLINE bool acquire(Gate& gate)
{
#ifdef ESP_PLATFORM
    __asm__ __volatile__("memw\n" : : : "memory");
    // IDF's forced-inline implementation is one WSR SCOMPARE1 + one S32C1I.
    // Do not use a PSRAM or RTC address here, or add a retry/spin loop.
    const bool acquired = xt_utils_compare_and_set(&gate.word, 0, 1);
    __asm__ __volatile__("memw\n" : : : "memory");
    return acquired;
#else
    std::uint32_t expected = 0;
    return std::atomic_ref<std::uint32_t>(gate.word).compare_exchange_strong(
        expected, 1, std::memory_order_acquire, std::memory_order_relaxed);
#endif
}
BOTIFIER_DIAG_INLINE void finish(Lane& lane, std::uint32_t sequence,
                                    Gate& gate)
{
    store(lane.words[Checksum], lane_checksum(lane, sequence));
    store(lane.words[Sequence], sequence);
    store(gate.word, 0);
}
BOTIFIER_DIAG_INLINE void mark(Lane& lane, Gate& gate,
                                  std::uint32_t stage, std::int32_t error)
{
    if (!acquire(gate)) return;
    const auto sequence = load(lane.words[Sequence]) + 2u;
    store(lane.words[Sequence], sequence - 1u);
    store(lane.words[Stage], stage); store(lane.words[Error], static_cast<std::uint32_t>(error));
    store(lane.words[Marks], load(lane.words[Marks]) + 1u);
    finish(lane, sequence, gate);
}
inline void sample(Lane& lane, Gate& gate,
                   std::uint32_t stage, std::uint32_t mark, std::uint32_t uptime,
                   std::uint32_t free, std::uint32_t largest, std::uint32_t minimum)
{
    if (!acquire(gate)) return;
    const auto sequence = load(lane.words[Sequence]) + 2u;
    store(lane.words[Sequence], sequence - 1u);
    // Stage/mark captured BEFORE sampling: a concurrent newer mark is preserved
    // and these explicit fields show which earlier operation the sample belongs to.
    store(lane.words[SampleStage], stage); store(lane.words[SampleMark], mark);
    store(lane.words[SampleTime], uptime); store(lane.words[Free], free);
    store(lane.words[Largest], largest); store(lane.words[Minimum], minimum);
    finish(lane, sequence, gate);
}
} // namespace botifier_diagnostics::detail
