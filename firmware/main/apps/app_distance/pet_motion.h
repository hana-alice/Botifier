#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace distance_pet {

enum class Mood { Idle, Curious, Happy, Affection, Shy, Sad, Sleepy, Surprise, Wink, Alert, Relief };
enum class Signal { Ready, Alert, Unavailable };

struct EyePose {
    float x = 92;
    float y = 96;
    float width = 46;
    float height = 58;
    float tilt_deg = 0;
    float smile = 0;
};

struct Frame {
    EyePose left_eye;
    EyePose right_eye{228, 96, 46, 58, 0, 0};
    float mouth_x = 160;
    float mouth_y = 153;
    float mouth_width = 26;
    float mouth_height = 7;
    float mouth_curve = 0.3f;
    float mouth_open = 0.08f;
    float cheek = 0.08f;
    float tear = 0;
    Mood mood = Mood::Idle;
};

// A caller-owned performance can direct the eyes without restarting the pet's
// mood or changing its ordinary animation history. The default cue is neutral.
struct EyeCue {
    float x = 0;
    float y = 0;
    float openness = 1;
    bool directed = false;
};

inline Frame with_eye_cue(Frame frame, EyeCue cue) noexcept
{
    if (frame.mood == Mood::Alert) return frame;
    const auto x = std::clamp(cue.x, -12.0f, 12.0f);
    const auto y = std::clamp(cue.y, -8.0f, 8.0f);
    const auto openness = std::clamp(cue.openness, 0.0f, 1.0f);
    for (auto* eye : {&frame.left_eye, &frame.right_eye}) {
        eye->x += x;
        eye->y += y;
        eye->height = std::max(2.0f, eye->height * openness);
    }
    return frame;
}

// Absolute pixel geometry only. The caller supplies the sensor's verdict and
// owns rendering; animation never decides whether the measured distance is safe.
class Controller {
public:
    explicit Controller(std::uint32_t seed = 1) noexcept : rng_(seed ? seed : 0xA341316Cu) {}

    void play(Mood mood, std::uint32_t now_ms) noexcept
    {
        initialize(now_ms);
        // Alert belongs exclusively to Signal; a queued idle action cannot
        // interrupt it, and a caller cannot manufacture a sensor alert here.
        if (in_alert_ || mood == Mood::Alert) return;
        reaction_active_ = false;
        begin(mood, now_ms);
        schedule_mood(now_ms);
        pending_play_ = true;
    }

    void react(std::uint32_t now_ms) noexcept
    {
        initialize(now_ms);
        if (in_alert_) return;
        reaction_active_ = true;
        reaction_start_ = now_ms;
        begin(Mood::Surprise, now_ms);
        schedule_mood(now_ms);
        pending_play_ = true;
    }

    Frame update(std::uint32_t now_ms, Signal signal, bool hold_mood = false,
                 bool directed_gaze = false) noexcept
    {
        initialize(now_ms);
        const auto delta = now_ms - last_ms_;
        last_ms_ = now_ms;
        if (signal == Signal::Alert) {
            reaction_active_ = false;
            in_alert_ = true;
            mood_ = Mood::Alert;
            blink_active_ = look_active_ = false;
            base_ = frame_ = pose(Mood::Alert, 0);
            pending_play_ = false;
            last_signal_ = signal;
            return frame_;
        }

        if (signal == Signal::Unavailable) reaction_active_ = false;

        bool changed_signal = false;
        if (in_alert_) {
            in_alert_ = false;
            begin(signal == Signal::Ready ? Mood::Relief : Mood::Idle, now_ms);
            schedule_mood(now_ms);
            reset_small_actions(now_ms);
            changed_signal = true;
        } else if (signal == Signal::Unavailable &&
                   (last_signal_ != Signal::Unavailable || mood_ != Mood::Idle)) {
            begin(Mood::Idle, now_ms);
            schedule_mood(now_ms);
            changed_signal = true;
        } else if (signal == Signal::Ready && last_signal_ == Signal::Unavailable) {
            schedule_mood(now_ms);
        }
        last_signal_ = signal;
        if (delta == 0 && !changed_signal && !pending_play_) return frame_;
        pending_play_ = false;

        if (reaction_active_) {
            const auto reaction_age = now_ms - reaction_start_;
            if (reaction_age >= 2400) {
                reaction_active_ = false;
                begin(Mood::Idle, now_ms);
            } else if (reaction_age >= 700 && mood_ != Mood::Happy) {
                begin(Mood::Happy, now_ms);
            }
        }

        // A caller-owned performance keeps its emotion without replaying the
        // blend each frame. Sensor verdicts above always retain their priority.
        const bool performing = hold_mood && signal == Signal::Ready;
        if (!performing && mood_ != Mood::Idle && now_ms - mood_start_ >= duration(mood_)) {
            begin(Mood::Idle, now_ms);
        }
        if (!performing && signal == Signal::Ready && mood_ == Mood::Idle && now_ms - auto_start_ >= auto_delay_) {
            constexpr Mood actions[] = {Mood::Curious, Mood::Happy, Mood::Affection, Mood::Shy,
                                        Mood::Sad, Mood::Sleepy, Mood::Surprise, Mood::Wink};
            auto index = random(0, 7);
            if (static_cast<int>(index) == previous_action_) index = (index + 1) % 8;
            previous_action_ = static_cast<int>(index);
            begin(actions[index], now_ms);
            schedule_mood(now_ms);
        }

        const auto age = now_ms - mood_start_;
        const auto target = pose(mood_, age);
        base_ = blend(from_, target, smooth(static_cast<float>(age) / 280.0f));
        base_.mood = mood_;
        frame_ = base_;
        // An intentional interaction supplies its own gaze/blink. Keep the
        // ordinary random look from pulling the eyes away from that target.
        if (!directed_gaze) {
            if (delta != 0) update_small_actions(now_ms);
            apply_small_actions(now_ms);
        }
        bound(frame_);
        return frame_;
    }

private:
    static float smooth(float value) noexcept
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    static float wave(std::uint32_t age, std::uint32_t period) noexcept
    {
        return std::sin(static_cast<float>(age % period) * (6.283185307f / static_cast<float>(period)));
    }

    static float mix(float a, float b, float amount) noexcept { return a + (b - a) * amount; }

    static EyePose blend(const EyePose& a, const EyePose& b, float amount) noexcept
    {
        return {mix(a.x, b.x, amount), mix(a.y, b.y, amount), mix(a.width, b.width, amount),
                mix(a.height, b.height, amount), mix(a.tilt_deg, b.tilt_deg, amount),
                mix(a.smile, b.smile, amount)};
    }

    static Frame blend(const Frame& a, const Frame& b, float amount) noexcept
    {
        Frame result;
        result.left_eye = blend(a.left_eye, b.left_eye, amount);
        result.right_eye = blend(a.right_eye, b.right_eye, amount);
        result.mouth_width = mix(a.mouth_width, b.mouth_width, amount);
        result.mouth_height = mix(a.mouth_height, b.mouth_height, amount);
        result.mouth_curve = mix(a.mouth_curve, b.mouth_curve, amount);
        result.mouth_open = mix(a.mouth_open, b.mouth_open, amount);
        result.cheek = mix(a.cheek, b.cheek, amount);
        result.tear = mix(a.tear, b.tear, amount);
        return result;
    }

    static std::uint32_t duration(Mood mood) noexcept
    {
        switch (mood) {
            case Mood::Curious: return 1800;
            case Mood::Happy: return 2000;
            case Mood::Affection: return 2400;
            case Mood::Shy: return 1800;
            case Mood::Sad: return 2100;
            case Mood::Sleepy: return 2200;
            case Mood::Surprise: return 1300;
            case Mood::Wink: return 1000;
            case Mood::Relief: return 1800;
            default: return 0;
        }
    }

    static Frame pose(Mood mood, std::uint32_t age) noexcept
    {
        Frame result;
        result.mood = mood;
        auto& left = result.left_eye;
        auto& right = result.right_eye;
        switch (mood) {
            case Mood::Curious:
                left = {98, 90, 48, 64, -10, 0};
                right = {234, 102, 44, 48, 9, 0};
                result.mouth_width = 16; result.mouth_height = 9;
                result.mouth_curve = 0.15f; result.mouth_open = 0.15f;
                break;
            case Mood::Happy: {
                const auto bounce = 8.0f * std::abs(wave(age, 650));
                left = {92, 94 - bounce, 50, 27, -4, 1};
                right = {228, 94 - bounce, 50, 27, 4, 1};
                result.mouth_width = 43; result.mouth_height = 19;
                result.mouth_curve = 1; result.mouth_open = 0.65f; result.cheek = 0.75f;
                break;
            }
            case Mood::Affection: {
                const auto sway = 12.0f * wave(age, 1100);
                left = {92 + sway, 98, 49, 49, -10, 0.4f};
                right = {228 + sway, 98, 49, 49, 10, 0.4f};
                result.mouth_width = 24; result.mouth_height = 11;
                result.mouth_curve = 0.95f; result.mouth_open = 0.22f; result.cheek = 1;
                break;
            }
            case Mood::Shy: {
                const auto sway = 2.0f * wave(age, 1500);
                left = {98 + sway, 108, 43, 35, -12, 0.35f};
                right = {222 + sway, 108, 43, 35, 12, 0.35f};
                result.mouth_width = 18; result.mouth_height = 5;
                result.mouth_curve = 0.8f; result.mouth_open = 0.05f; result.cheek = 0.95f;
                break;
            }
            case Mood::Sad:
                left = {92, 105, 43, 45, -17, 0};
                right = {228, 105, 43, 45, 17, 0};
                result.mouth_width = 30; result.mouth_height = 7;
                result.mouth_curve = -1; result.mouth_open = 0.04f; result.tear = 1;
                break;
            case Mood::Sleepy:
                left.height = right.height = 17;
                left.y = right.y = 104;
                result.mouth_width = 16; result.mouth_height = 17;
                result.mouth_curve = 0; result.mouth_open = 0.6f;
                break;
            case Mood::Surprise:
                left = {92, 91, 45, 70, 0, 0};
                right = {228, 91, 45, 70, 0, 0};
                result.mouth_width = 24; result.mouth_height = 28;
                result.mouth_curve = 0; result.mouth_open = 1; result.cheek = 0.2f;
                break;
            case Mood::Wink:
                left = {92, 98, 52, 4, -8, 0.25f};
                right = {228, 95, 48, 60, 4, 0.1f};
                result.mouth_width = 32; result.mouth_height = 12;
                result.mouth_curve = 1; result.mouth_open = 0.4f; result.cheek = 0.7f;
                break;
            case Mood::Alert:
                // This geometry leaves x=120..200 for the 80px warning mark,
                // the top title and all bottom controls unobstructed.
                left = {92, 101, 46, 34, 18, 0};
                right = {228, 101, 46, 34, -18, 0};
                result.mouth_width = 34; result.mouth_height = 7;
                result.mouth_curve = -1; result.mouth_open = 0; result.cheek = 0;
                break;
            case Mood::Relief:
                left = {92, 96, 46, 40, -2, 0.8f};
                right = {228, 96, 46, 40, 2, 0.8f};
                result.mouth_width = 32; result.mouth_height = 10;
                result.mouth_curve = 1; result.mouth_open = 0.25f; result.cheek = 0.45f;
                break;
            default:
                left.y += wave(age, 3600);
                right.y = left.y;
                break;
        }
        return result;
    }

    void begin(Mood mood, std::uint32_t now) noexcept
    {
        from_ = base_;
        mood_ = mood;
        mood_start_ = now;
    }

    std::uint32_t random(std::uint32_t minimum, std::uint32_t maximum) noexcept
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return minimum + rng_ % (maximum - minimum + 1);
    }

    void schedule_mood(std::uint32_t now) noexcept
    {
        auto_start_ = now;
        auto_delay_ = random(2500, 5500);
    }

    void reset_small_actions(std::uint32_t now) noexcept
    {
        blink_active_ = look_active_ = false;
        blink_start_ = look_start_ = now;
        blink_delay_ = random(2200, 5100);
        look_delay_ = random(1100, 3400);
    }

    void initialize(std::uint32_t now) noexcept
    {
        if (initialized_) return;
        initialized_ = true;
        last_ms_ = mood_start_ = now;
        reset_small_actions(now);
        schedule_mood(now);
    }

    void update_small_actions(std::uint32_t now) noexcept
    {
        if (!blink_active_ && now - blink_start_ >= blink_delay_) {
            blink_active_ = true;
            blink_start_ = now;
            double_blink_ = random(0, 4) == 0;
        } else if (blink_active_ && now - blink_start_ >= (double_blink_ ? 590u : 250u)) {
            blink_active_ = false;
            blink_start_ = now;
            blink_delay_ = random(2200, 5100);
        }
        if (!look_active_ && now - look_start_ >= look_delay_) {
            look_active_ = true;
            look_start_ = now;
            look_x_ = static_cast<float>(static_cast<int>(random(0, 18)) - 9);
            look_y_ = static_cast<float>(static_cast<int>(random(0, 10)) - 5);
            look_hold_ = random(350, 850);
        } else if (look_active_ && now - look_start_ >= 500 + look_hold_) {
            look_active_ = false;
            look_start_ = now;
            look_delay_ = random(1100, 3400);
        }
    }

    static float blink(std::uint32_t age) noexcept
    {
        if (age < 80) return mix(1, 0.035f, smooth(static_cast<float>(age) / 80));
        if (age < 110) return 0.035f;
        return mix(0.035f, 1, smooth(static_cast<float>(age - 110) / 140));
    }

    void apply_small_actions(std::uint32_t now) noexcept
    {
        if (look_active_) {
            const auto age = now - look_start_;
            const float amount = age < 180 ? smooth(static_cast<float>(age) / 180) :
                age < 180 + look_hold_ ? 1 : 1 - smooth(static_cast<float>(age - 180 - look_hold_) / 320);
            frame_.left_eye.x += look_x_ * amount;
            frame_.right_eye.x += look_x_ * amount;
            frame_.left_eye.y += look_y_ * amount;
            frame_.right_eye.y += look_y_ * amount;
        }
        if (blink_active_) {
            const auto age = now - blink_start_;
            const float amount = age < 250 ? blink(age) :
                double_blink_ && age >= 340 ? blink(age - 340) : 1;
            frame_.left_eye.height *= amount;
            frame_.right_eye.height *= amount;
        }
    }

    static void bound(Frame& frame) noexcept
    {
        for (auto* eye : {&frame.left_eye, &frame.right_eye}) {
            eye->x = std::clamp(eye->x, 45.0f, 275.0f);
            eye->y = std::clamp(eye->y, 50.0f, 135.0f);
            eye->width = std::clamp(eye->width, 1.0f, 60.0f);
            eye->height = std::clamp(eye->height, 1.0f, 70.0f);
            eye->tilt_deg = std::clamp(eye->tilt_deg, -35.0f, 35.0f);
            eye->smile = std::clamp(eye->smile, 0.0f, 1.0f);
        }
        frame.mouth_width = std::clamp(frame.mouth_width, 1.0f, 70.0f);
        frame.mouth_height = std::clamp(frame.mouth_height, 1.0f, 28.0f);
        frame.mouth_curve = std::clamp(frame.mouth_curve, -1.0f, 1.0f);
        frame.mouth_open = std::clamp(frame.mouth_open, 0.0f, 1.0f);
        frame.cheek = std::clamp(frame.cheek, 0.0f, 1.0f);
        frame.tear = std::clamp(frame.tear, 0.0f, 1.0f);
    }

    std::uint32_t rng_;
    bool initialized_ = false;
    bool in_alert_ = false;
    bool pending_play_ = false;
    bool reaction_active_ = false;
    std::uint32_t reaction_start_ = 0;
    Signal last_signal_ = Signal::Unavailable;
    Mood mood_ = Mood::Idle;
    Frame frame_, base_, from_;
    std::uint32_t last_ms_ = 0, mood_start_ = 0, auto_start_ = 0, auto_delay_ = 0;
    int previous_action_ = -1;
    bool blink_active_ = false, double_blink_ = false;
    std::uint32_t blink_start_ = 0, blink_delay_ = 0;
    bool look_active_ = false;
    std::uint32_t look_start_ = 0, look_delay_ = 0, look_hold_ = 0;
    float look_x_ = 0, look_y_ = 0;
};

} // namespace distance_pet
