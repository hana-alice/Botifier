#pragma once

#include <cstdint>

namespace nfc_sense {

// UID changes are intentionally irrelevant: only a valid, sustained absence
// ends a presentation. Errors and blocked reads never count as card removal.
class CardGate {
public:
    void reset() noexcept { *this = CardGate{}; }

    void block() noexcept
    {
        waiting_for_removal_ = true;
        clearing_ = false;
    }

    bool observe(std::uint32_t now_ms, bool present, bool reading_valid = true, bool blocked = false) noexcept
    {
        if (have_observation_) {
            const auto elapsed = now_ms - observed_ms_;
            if (elapsed == 0 || elapsed > 0x7fffffffu) {
                block();
                return false;
            }
        }
        have_observation_ = true;
        observed_ms_ = now_ms;
        if (!reading_valid || blocked) {
            block();
            return false;
        }
        if (present) {
            clearing_ = false;
            if (waiting_for_removal_) return false;
            waiting_for_removal_ = true;
            return true;
        }
        if (!waiting_for_removal_) return false;
        if (!clearing_) {
            clearing_ = true;
            clear_started_ms_ = now_ms;
        } else if (now_ms - clear_started_ms_ >= 300u) {
            waiting_for_removal_ = false;
            clearing_ = false;
        }
        return false;
    }

    bool waiting_for_removal() const noexcept { return waiting_for_removal_; }

private:
    bool waiting_for_removal_ = false;
    bool clearing_ = false;
    bool have_observation_ = false;
    std::uint32_t observed_ms_ = 0;
    std::uint32_t clear_started_ms_ = 0;
};

}  // namespace nfc_sense
