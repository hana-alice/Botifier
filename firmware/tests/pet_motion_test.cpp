#include <apps/app_distance/pet_motion.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using distance_pet::Controller;
using distance_pet::Frame;
using distance_pet::Mood;
using distance_pet::Signal;

namespace {

constexpr std::array<Mood, 8> actions{Mood::Curious, Mood::Happy, Mood::Affection, Mood::Shy,
                                     Mood::Sad, Mood::Sleepy, Mood::Surprise, Mood::Wink};

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::array<float, 20> values(const Frame& frame)
{
    const auto& l = frame.left_eye;
    const auto& r = frame.right_eye;
    return {l.x, l.y, l.width, l.height, l.tilt_deg, l.smile,
            r.x, r.y, r.width, r.height, r.tilt_deg, r.smile,
            frame.mouth_x, frame.mouth_y, frame.mouth_width, frame.mouth_height,
            frame.mouth_curve, frame.mouth_open, frame.cheek, frame.tear};
}

float difference(const Frame& a, const Frame& b)
{
    const auto left = values(a);
    const auto right = values(b);
    float maximum = 0;
    for (std::size_t i = 0; i < left.size(); ++i) maximum = std::max(maximum, std::abs(left[i] - right[i]));
    return maximum;
}

void check_same(const Frame& a, const Frame& b)
{
    check(a.mood == b.mood && difference(a, b) < 0.00001f, "identical input histories must yield identical frames");
}

void check_bounds(const Frame& frame)
{
    for (const auto number : values(frame)) check(std::isfinite(number), "animation generated NaN or infinity");
    for (const auto* eye : {&frame.left_eye, &frame.right_eye}) {
        check(eye->x >= 45 && eye->x <= 275 && eye->y >= 50 && eye->y <= 135, "eye center left its geometry bounds");
        check(eye->width > 0 && eye->width <= 60 && eye->height > 0 && eye->height <= 70, "eye size left its geometry bounds");
        check(eye->smile >= 0 && eye->smile <= 1 && std::abs(eye->tilt_deg) <= 35, "eye expression left its bounds");
    }
    check(frame.mouth_x == 160 && frame.mouth_y == 153, "mouth moved into the warning or control regions");
    check(frame.mouth_width > 0 && frame.mouth_width <= 70 && frame.mouth_height > 0 && frame.mouth_height <= 28,
          "mouth size left its geometry bounds");
    check(frame.mouth_curve >= -1 && frame.mouth_curve <= 1 && frame.mouth_open >= 0 && frame.mouth_open <= 1,
          "mouth expression left its bounds");
    check(frame.cheek >= 0 && frame.cheek <= 1 && frame.tear >= 0 && frame.tear <= 1, "face accents left their bounds");
}

Frame settled(Mood mood)
{
    Controller controller(17);
    controller.update(0, Signal::Ready);
    controller.play(mood, 0);
    Frame result;
    for (std::uint32_t time = 0; time <= 500; time += 20) result = controller.update(time, Signal::Ready);
    return result;
}

void seeded_histories_are_reproducible()
{
    Controller first(77), second(77);
    for (std::uint32_t time = 0; time < 120000; time += 20) {
        const auto signal = time >= 10000 && time < 12000 ? Signal::Alert :
                            time >= 50000 && time < 53000 ? Signal::Unavailable : Signal::Ready;
        const auto frame = first.update(time, signal);
        check_same(frame, second.update(time, signal));
        check_bounds(frame);
    }
}

void eight_actions_have_distinct_expressive_geometry()
{
    std::array<Frame, 8> frames;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        frames[i] = settled(actions[i]);
        check(frames[i].mood == actions[i], "manual action was not played");
        check_bounds(frames[i]);
        for (std::size_t j = 0; j < i; ++j) check(difference(frames[i], frames[j]) > 3, "two pet actions are visually indistinguishable");
    }
    check(frames[1].left_eye.smile > 0.9f && frames[1].mouth_curve > 0.9f, "happy must have strong smiling eyes and mouth");
    check(frames[2].cheek > 0.9f && frames[3].cheek > 0.9f, "affection and shyness must visibly blush");
    check(frames[4].tear > 0.9f && frames[4].mouth_curve < -0.9f, "sad must have a tear and exaggerated frown");
    check(frames[5].left_eye.height < 20 && frames[6].mouth_open > 0.9f, "sleepiness and surprise must be distinct");
    check(frames[7].left_eye.height < 8 && frames[7].right_eye.height > 50, "wink must close only one eye");
}

void ordinary_transitions_are_continuous()
{
    for (const auto from : actions) {
        for (const auto to : actions) {
            Controller controller(3);
            controller.update(0, Signal::Ready);
            controller.play(from, 0);
            Frame previous;
            for (std::uint32_t time = 0; time <= 400; time += 20) previous = controller.update(time, Signal::Ready);
            controller.play(to, 400);
            auto next = controller.update(400, Signal::Ready);
            check(difference(previous, next) < 0.00001f, "starting an ordinary action jumped its geometry");
            for (std::uint32_t time = 420; time <= 700; time += 20) {
                previous = next;
                next = controller.update(time, Signal::Ready);
                check(difference(previous, next) < 8.5f, "20ms action blend made a discontinuous pose jump");
                check_bounds(next);
            }
        }
    }
}

void affection_sways_both_eyes_and_happy_bounces()
{
    Controller affection(5);
    affection.update(0, Signal::Ready);
    affection.play(Mood::Affection, 0);
    const auto left = affection.update(300, Signal::Ready);
    const auto right = affection.update(800, Signal::Ready);
    const auto left_travel = right.left_eye.x - left.left_eye.x;
    const auto right_travel = right.right_eye.x - left.right_eye.x;
    check(std::abs(left_travel) > 8 && std::abs(left_travel - right_travel) < 0.001f,
          "affection must sway both eyes together instead of changing only a static face");
    check(left.cheek > 0.9f && right.cheek > 0.9f, "affection sway must retain strong cheek accents");
    Controller happy(5);
    happy.update(0, Signal::Ready);
    happy.play(Mood::Happy, 0);
    const auto high = happy.update(500, Signal::Ready);
    const auto low = happy.update(650, Signal::Ready);
    check(std::abs(high.left_eye.y - low.left_eye.y) > 4 && high.left_eye.smile > 0.9f,
          "happy must bounce visibly while keeping its smiling eye arcs");
}

void automatic_expressions_are_spaced_and_look_returns_to_center()
{
    Controller gestures(29);
    Mood previous = Mood::Idle;
    std::vector<std::uint32_t> starts;
    for (std::uint32_t time = 0; time < 90000; time += 20) {
        const auto mood = gestures.update(time, Signal::Ready).mood;
        if (mood != Mood::Idle && previous == Mood::Idle) starts.push_back(time);
        previous = mood;
    }
    check(starts.size() > 10, "ready pet must occasionally play automatic expressions");
    check(starts.front() >= 2500 && starts.front() <= 5520, "first expression was not delayed naturally");
    for (std::size_t i = 1; i < starts.size(); ++i) {
        check(starts[i] - starts[i - 1] >= 2500 && starts[i] - starts[i - 1] <= 5520,
              "automatic expression starts must remain 2.5 to 5.5 seconds apart");
    }
    Controller quiet(29);
    bool looked = false;
    bool returned = false;
    for (std::uint32_t time = 0; time < 12000; time += 20) {
        const auto frame = quiet.update(time, Signal::Unavailable);
        looked = looked || std::abs(frame.left_eye.x - 92) > 2;
        if (looked && std::abs(frame.left_eye.x - 92) < 0.01f && std::abs(frame.right_eye.x - 228) < 0.01f) {
            returned = true;
            break;
        }
    }
    check(looked && returned, "a quiet eye glance must hold briefly and return to its original center");
}

void repeated_timestamps_do_not_advance_pose_or_random_state()
{
    Controller first(91), second(91);
    for (std::uint32_t time = 0; time <= 20000; time += 20) {
        const auto reference = first.update(time, Signal::Ready);
        check_same(reference, second.update(time, Signal::Ready));
        for (int repeat = 0; repeat < 4; ++repeat) check_same(reference, first.update(time, Signal::Ready));
    }
}

void alert_immediately_preempts_even_at_zero_delta()
{
    Controller controller(2);
    controller.update(0, Signal::Ready);
    controller.play(Mood::Happy, 0);
    controller.update(500, Signal::Ready);
    const auto alert = controller.update(500, Signal::Alert);
    check(alert.mood == Mood::Alert && alert.mouth_curve < -0.9f && alert.left_eye.smile == 0,
          "alert must replace idle expression on the very same frame, without a hold");
    check(alert.left_eye.tilt_deg > 10 && alert.right_eye.tilt_deg < -10, "alert must have serious eyes");
    const auto angle = alert.left_eye.tilt_deg * (3.141592654f / 180);
    const auto half_width = alert.left_eye.width * std::cos(angle) / 2 +
                            alert.left_eye.height * std::abs(std::sin(angle)) / 2;
    check(alert.left_eye.x + half_width < 120 && alert.right_eye.x - half_width > 200,
          "rotated alert eyes overlap the central 80px warning symbol");
    for (std::uint32_t time = 500; time <= 9000; time += 20) {
        controller.play(Mood::Affection, time);
        check_same(alert, controller.update(time, Signal::Alert));
    }
    Controller fresh;
    check(fresh.update(0, Signal::Alert).mood == Mood::Alert, "first-ever frame must also honor an immediate alert");
}

void ready_recovery_plays_one_relief_then_resumes_idle()
{
    Controller controller(11);
    controller.update(0, Signal::Alert);
    check(controller.update(100, Signal::Ready).mood == Mood::Relief, "valid alert recovery must start relief");
    int relief_entries = 1;
    auto last_mood = Mood::Relief;
    for (std::uint32_t time = 120; time <= 12000; time += 20) {
        const auto frame = controller.update(time, Signal::Ready);
        if (frame.mood == Mood::Relief && last_mood != Mood::Relief) ++relief_entries;
        if (time == 1680) check(frame.mood == Mood::Relief, "relief ended before 1.6 seconds");
        if (time == 2300) check(frame.mood != Mood::Relief, "relief lasted beyond 2.2 seconds");
        last_mood = frame.mood;
    }
    check(relief_entries == 1, "continuous Ready must not repeat the recovery celebration");
}

void unavailable_never_claims_a_successful_recovery()
{
    Controller controller(19);
    controller.update(0, Signal::Alert);
    for (std::uint32_t time = 20; time <= 5000; time += 20) {
        const auto frame = controller.update(time, Signal::Unavailable);
        check(frame.mood == Mood::Idle, "lost sensor data must quietly idle, not celebrate or retain alert");
    }
    check(controller.update(5020, Signal::Ready).mood == Mood::Idle,
          "Ready after unavailable must not retroactively celebrate an earlier alert");
    Controller missing;
    for (std::uint32_t time = 0; time <= 30000; time += 20) {
        check(missing.update(time, Signal::Unavailable).mood == Mood::Idle,
              "ordinary unavailable data must not invent proximity or recovery events");
    }
}

void blinks_close_hold_open_and_include_nonperiodic_double_blinks()
{
    Controller controller(7);
    controller.update(0, Signal::Unavailable);
    std::uint32_t first_close = 0;
    for (std::uint32_t time = 10; time <= 6000; time += 10) {
        const auto current = controller.update(time, Signal::Unavailable);
        if (current.left_eye.height < 57.9f) { first_close = time; break; }
    }
    check(first_close != 0, "seeded idle never blinked");
    check(controller.update(first_close + 70, Signal::Unavailable).left_eye.height < 3,
          "blink must close in about 80ms");
    check(controller.update(first_close + 100, Signal::Unavailable).left_eye.height < 3,
          "blink must hold closed for about 30ms");
    check(controller.update(first_close + 240, Signal::Unavailable).left_eye.height > 57,
          "blink must reopen in about 140ms");

    Controller trace(7);
    std::vector<std::uint32_t> closures;
    bool was_closed = false;
    for (std::uint32_t time = 0; time < 120000; time += 10) {
        const bool closed = trace.update(time, Signal::Unavailable).left_eye.height < 5;
        if (closed && !was_closed) closures.push_back(time);
        was_closed = closed;
    }
    check(closures.size() > 10, "long quiet trace must include blinking");
    bool double_blink = false;
    std::vector<std::uint32_t> pauses;
    for (std::size_t i = 1; i < closures.size(); ++i) {
        const auto interval = closures[i] - closures[i - 1];
        if (interval < 600) double_blink = true;
        else pauses.push_back(interval);
    }
    check(double_blink, "long seeded trace must occasionally double blink");
    check(pauses.size() > 3 && *std::max_element(pauses.begin(), pauses.end()) -
          *std::min_element(pauses.begin(), pauses.end()) > 500, "blink spacing must vary instead of ticking periodically");
}

void different_seeds_change_the_idle_history()
{
    Controller first(1), second(2);
    bool changed = false;
    for (std::uint32_t time = 0; time <= 30000; time += 20) {
        const auto a = first.update(time, Signal::Ready);
        const auto b = second.update(time, Signal::Ready);
        changed = changed || a.mood != b.mood || difference(a, b) > 0.1f;
    }
    check(changed, "different seeds must not produce the same gesture history");
}

void uptime_rollover_preserves_the_same_relative_animation()
{
    Controller first(15), wrapped(15);
    const auto start = std::numeric_limits<std::uint32_t>::max() - 2000u;
    for (std::uint32_t offset = 0; offset <= 20000; offset += 20) {
        const auto signal = offset >= 1000 && offset < 1500 ? Signal::Alert :
                            offset >= 7000 && offset < 7500 ? Signal::Unavailable : Signal::Ready;
        check_same(first.update(offset, signal), wrapped.update(start + offset, signal));
    }
}

void one_hundred_thousand_frames_remain_finite_and_bounded()
{
    Controller controller(0);
    std::array<bool, 11> visited{};
    const auto start = std::numeric_limits<std::uint32_t>::max() - 1000u;
    for (std::uint32_t frame = 0; frame < 100000; ++frame) {
        const auto phase = frame % 10000;
        const auto signal = (phase >= 5000 && phase < 5020) || (phase >= 9000 && phase < 9020) ? Signal::Alert :
                            phase >= 9020 && phase < 9080 ? Signal::Unavailable : Signal::Ready;
        const auto result = controller.update(start + frame * 20u, signal);
        check_bounds(result);
        visited[static_cast<std::size_t>(result.mood)] = true;
    }
    for (const bool seen : visited) check(seen, "long simulated history did not exercise every mood");
}

} // namespace

int main()
{
    const struct Test { const char* name; void (*run)(); } tests[] = {
        {"seeded_histories_are_reproducible", seeded_histories_are_reproducible},
        {"eight_actions_have_distinct_expressive_geometry", eight_actions_have_distinct_expressive_geometry},
        {"ordinary_transitions_are_continuous", ordinary_transitions_are_continuous},
        {"affection_sways_both_eyes_and_happy_bounces", affection_sways_both_eyes_and_happy_bounces},
        {"automatic_expressions_are_spaced_and_look_returns_to_center", automatic_expressions_are_spaced_and_look_returns_to_center},
        {"repeated_timestamps_do_not_advance_pose_or_random_state", repeated_timestamps_do_not_advance_pose_or_random_state},
        {"alert_immediately_preempts_even_at_zero_delta", alert_immediately_preempts_even_at_zero_delta},
        {"ready_recovery_plays_one_relief_then_resumes_idle", ready_recovery_plays_one_relief_then_resumes_idle},
        {"unavailable_never_claims_a_successful_recovery", unavailable_never_claims_a_successful_recovery},
        {"blinks_close_hold_open_and_include_nonperiodic_double_blinks", blinks_close_hold_open_and_include_nonperiodic_double_blinks},
        {"different_seeds_change_the_idle_history", different_seeds_change_the_idle_history},
        {"uptime_rollover_preserves_the_same_relative_animation", uptime_rollover_preserves_the_same_relative_animation},
        {"one_hundred_thousand_frames_remain_finite_and_bounded", one_hundred_thousand_frames_remain_finite_and_bounded},
    };
    unsigned passed = 0;
    for (const auto& test : tests) {
        try { test.run(); ++passed; std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << test.name << ": " << error.what() << '\n'; }
    }
    const auto total = sizeof(tests) / sizeof(tests[0]);
    std::cout << passed << '/' << total << " pet motion tests passed\n";
    return passed == total ? 0 : 1;
}
