#pragma once
#include <cstdint>

namespace head_touch {

// Si12T: three two-bit channels. Confirm contact and release independently;
// a short dropout must not rearm a held touch as another Press.
class Filter {
public:
    bool sample(std::uint8_t raw, bool read_ok, std::uint32_t now_ms, std::uint8_t& filtered)
    {
        if (!read_ok) {
            pending_ = false;
            have_sample_ = false;
            quiet_pending_ = false;
            return false;
        }
        if (have_sample_ && now_ms - last_sample_ms_ > 200u) {
            pending_ = false;
            quiet_pending_ = false;
        }
        have_sample_ = true;
        last_sample_ms_ = now_ms;
        raw &= 0x3fu;
        if (isolated_) {
            if (raw != 0) quiet_pending_ = false;
            else {
                if (!quiet_pending_) {
                    quiet_pending_ = true;
                    quiet_since_ms_ = now_ms;
                }
                if (now_ms - quiet_since_ms_ >= 2000u) {
                    isolated_ = false;
                    quiet_pending_ = false;
                    burst_contacts_ = 0;
                    pending_ = false;
                }
            }
            // Deliver a release to the gesture recognizer, rather than leave
            // it latched in TOUCHED while this input is temporarily isolated.
            filtered = 0;
            return true;
        }
        const bool touched = raw != 0;
        const bool active = confirmed_ != 0;
        if (touched == active) {
            pending_ = false;
            if (active) confirmed_ = raw;
        } else {
            if (!pending_) {
                pending_ = true;
                candidate_ms_ = now_ms;
            }
            if (now_ms - candidate_ms_ >= (touched ? 100u : 200u)) {
                confirmed_ = raw;
                pending_ = false;
                if (touched) {
                    if (burst_contacts_ == 0 || now_ms - burst_since_ms_ > 4000u) {
                        burst_since_ms_ = now_ms;
                        burst_contacts_ = 1;
                    } else if (++burst_contacts_ == 3) {
                        isolated_ = true;
                        quiet_pending_ = false;
                        confirmed_ = 0;
                        ++suppressed_presses_;
                    }
                }
            }
        }
        filtered = confirmed_;
        return true;
    }

    bool isolated() const { return isolated_; }
    // Count the confirmed third contacts that entered isolation, not every
    // nonzero sensor sample subsequently ignored during that isolation.
    std::uint32_t suppressed_presses() const { return suppressed_presses_; }

private:
    std::uint32_t candidate_ms_ = 0;
    std::uint32_t last_sample_ms_ = 0;
    std::uint32_t burst_since_ms_ = 0;
    std::uint32_t quiet_since_ms_ = 0;
    std::uint32_t suppressed_presses_ = 0;
    std::uint8_t confirmed_ = 0;
    std::uint8_t burst_contacts_ = 0;
    bool pending_ = false;
    bool have_sample_ = false;
    bool isolated_ = false;
    bool quiet_pending_ = false;
};

} // namespace head_touch
