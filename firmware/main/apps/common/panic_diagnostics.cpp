#include "panic_diagnostics.h"
#include "runtime_diagnostics_core.h"
#include <esp_attr.h>
#include <esp_private/panic_internal.h>
#include <esp_system.h>
#include <soc/soc.h>
#include <xtensa_context.h>
#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include <esp_idf_version.h>
static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 5, 4),
              "Re-audit the private panic ABI before changing the pinned IDF version");
#endif

namespace {
// Reuse the already audited load/store/CAS primitives, with completely separate
// storage and a one-shot gate. Panic recording never takes a breadcrumb gate.
using botifier_diagnostics::detail::load;
using botifier_diagnostics::detail::store;
using botifier_diagnostics::detail::mix;
using botifier_diagnostics::detail::acquire;
using botifier_diagnostics::detail::Gate;
constexpr std::uint32_t panic_magic = 0x42504e31;
constexpr std::uint32_t panic_version = 1;
enum PanicWord : std::size_t {
    Magic, Version, Sequence, InfoValid, Core, Exception, Address, Pseudo, Aborted,
    FrameValid, Pc, ExcCause, ExcVaddr, A0, A1, Checksum, PanicWordCount
};
struct PanicRetained { alignas(4) std::uint32_t words[PanicWordCount]; };
static_assert(sizeof(PanicRetained) == 64 && alignof(PanicRetained) >= 4);
RTC_NOINIT_ATTR PanicRetained panic_retained;
DRAM_ATTR Gate panic_claim{};
DRAM_ATTR alignas(4) std::uint32_t panic_ready = 0;
DRAM_ATTR botifier_panic_record_t previous_panic{};

BOTIFIER_DIAG_INLINE bool internal_structure(const void* pointer, std::size_t size, std::size_t alignment = 4)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto low = static_cast<std::uintptr_t>(SOC_DRAM_LOW);
    const auto high = static_cast<std::uintptr_t>(SOC_DRAM_HIGH);
    // Subtraction avoids wrapping address + size. Validate the entire object,
    // not just its first byte, before any member is dereferenced.
    return pointer && !(address & (alignment - 1u)) && high >= low && size <= high - low &&
           address >= low && address <= high - size;
}

BOTIFIER_DIAG_INLINE std::uint32_t panic_checksum()
{
    auto hash = mix(mix(panic_magic, panic_version), 2);
    for (std::size_t i = InfoValid; i < Checksum; ++i)
        hash = mix(hash, load(panic_retained.words[i]));
    return hash;
}

BOTIFIER_DIAG_INLINE void record_panic(const panic_info_t* info)
{
    if (!load(panic_ready) || !acquire(panic_claim)) return;
    // Claim is intentionally never released: a second CPU, nested panic or a
    // returning JTAG branch must not replace the first failure this boot.
    store(panic_retained.words[Sequence], 1);
    store(panic_retained.words[Magic], panic_magic);
    store(panic_retained.words[Version], panic_version);
    const bool info_valid = internal_structure(info, sizeof(*info), alignof(panic_info_t));
    const volatile auto* bounded_info = info_valid ? info : nullptr;
    const void* frame_pointer = bounded_info ? bounded_info->frame : nullptr;
    const bool frame_valid = internal_structure(frame_pointer, sizeof(XtExcFrame), alignof(XtExcFrame));
    const volatile auto* frame = frame_valid ? static_cast<const XtExcFrame*>(frame_pointer) : nullptr;
    store(panic_retained.words[InfoValid], info_valid);
    store(panic_retained.words[Core], bounded_info ? static_cast<std::uint32_t>(bounded_info->core) : UINT32_MAX);
    store(panic_retained.words[Exception], bounded_info ? static_cast<std::uint32_t>(bounded_info->exception) : UINT32_MAX);
    store(panic_retained.words[Address], bounded_info ? static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(bounded_info->addr)) : 0);
    store(panic_retained.words[Pseudo], bounded_info && bounded_info->pseudo_excause);
    store(panic_retained.words[Aborted], g_panic_abort);
    store(panic_retained.words[FrameValid], frame_valid);
    store(panic_retained.words[Pc], frame ? static_cast<std::uint32_t>(frame->pc) : 0);
    store(panic_retained.words[ExcCause], frame ? static_cast<std::uint32_t>(frame->exccause) : 0);
    store(panic_retained.words[ExcVaddr], frame ? static_cast<std::uint32_t>(frame->excvaddr) : 0);
    store(panic_retained.words[A0], frame ? static_cast<std::uint32_t>(frame->a0) : 0);
    store(panic_retained.words[A1], frame ? static_cast<std::uint32_t>(frame->a1) : 0);
    store(panic_retained.words[Checksum], panic_checksum());
    store(panic_retained.words[Sequence], 2);
}

botifier_panic_record_t read_panic()
{
    if (load(panic_retained.words[Magic]) != panic_magic ||
        load(panic_retained.words[Version]) != panic_version ||
        load(panic_retained.words[Sequence]) != 2) return {};
    std::uint32_t words[Checksum - InfoValid]{};
    auto hash = mix(mix(panic_magic, panic_version), 2);
    for (std::size_t i = InfoValid; i < Checksum; ++i) {
        const auto value = load(panic_retained.words[i]);
        words[i - InfoValid] = value; hash = mix(hash, value);
    }
    if (hash != load(panic_retained.words[Checksum]) || load(panic_retained.words[Sequence]) != 2) return {};
    return {1, words[0], static_cast<std::int32_t>(words[1]), static_cast<std::int32_t>(words[2]),
            words[3], words[4], words[5], words[6], words[7], words[8], words[9], words[10], words[11]};
}
} // namespace

extern "C" void botifier_panic_boot(void)
{
    store(panic_ready, 0);
    previous_panic = esp_reset_reason() == ESP_RST_POWERON ? botifier_panic_record_t{} : read_panic();
    for (auto& word : panic_retained.words) store(word, 0);
    store(panic_claim.word, 0);
    store(panic_ready, 1);
}

extern "C" botifier_panic_snapshot_t botifier_panic_snapshot(void)
{
    if (!load(panic_ready)) return {};
    return {panic_version, read_panic(), previous_panic};
}

extern "C" void __real_esp_panic_handler(panic_info_t* info);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info)
{
    record_panic(info);
    // Do not declare noreturn: IDF's OCDAWARE/JTAG path legitimately returns.
    __real_esp_panic_handler(info);
}
