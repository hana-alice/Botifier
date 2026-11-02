#include <apps/common/idle_program/policy.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace idle_program;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
constexpr Context eligible{true, false, false};

Policy make_policy(Config config = {}, std::uint32_t now = 0)
{
    Policy policy;
    check(policy.set_config(config, now), "valid configuration rejected");
    policy.start(now);
    policy.set_context(eligible, now);
    policy.tick(now);
    return policy;
}

void run(Policy& policy, std::uint32_t& now, std::uint32_t duration, Context context = eligible)
{
    while (duration) {
        const auto step = std::min(duration, 100u);
        now += step;
        duration -= step;
        policy.set_context(context, now);
        policy.tick(now);
    }
}

void enter_rest(Policy& policy, std::uint32_t& now, Context context = eligible)
{
    const auto intro = policy.snapshot(now);
    check(intro.program == Program::EyeRest && intro.eye_phase == EyePhase::Intro,
          "rest helper did not begin at the real intro");
    check(policy.finish_eye_intro(intro.generation, now), "current real intro acknowledgement rejected");
    const auto motion = policy.snapshot(now);
    check(motion.eye_phase == EyePhase::Motion && motion.remaining_ms == kMotionDurationMs &&
              motion.phase_elapsed_ms == 0 && motion.generation == intro.generation + 1u,
          "intro acknowledgement did not begin a full motion phase");
    run(policy, now, kMotionDurationMs, context);
    const auto rest = policy.snapshot(now);
    check(rest.eye_phase == EyePhase::Rest && rest.remaining_ms == rest.config.rest_duration_ms &&
              rest.phase_elapsed_ms == 0 && rest.generation == motion.generation + 1u,
          "motion did not begin an independent full rest phase");
}

WeeklyUpdate quota(std::uint32_t sequence = 1, std::uint32_t observed = 1700000000u)
{
    return {true, sequence, 37, observed + 3600u, observed, 900000};
}

void defaults_and_configuration_bounds()
{
    Config config;
    check(config.eye_interval_ms == 1200000 && config.rest_duration_ms == 20000 &&
              config.idle_grace_ms == 10000 && config.pet_gap_ms == 3000 &&
              config.weekly_duration_ms == 12000 && config.weekly_interval_ms == 30000,
          "default schedule changed");
    check(valid_config(config), "defaults invalid");
    config.eye_interval_ms = 999;
    check(!valid_config(config), "sub-second eye interval accepted");
    config.eye_interval_ms = 86400001;
    check(!valid_config(config), "over-one-day interval accepted");
    config.eye_interval_ms = 20000;
    config.rest_duration_ms = 1000;
    config.idle_grace_ms = config.pet_gap_ms = 0;
    check(valid_config(config), "debug configuration rejected");
}

void twenty_minute_due_waits_for_idle_eligibility()
{
    Policy policy;
    policy.start(0);
    policy.tick(1200000);
    auto state = policy.snapshot(1200000);
    check(state.blocked && state.program == Program::Pet && state.eye_remaining_ms == 0,
          "eye due ran without a fresh eligible context");
    std::uint32_t now = 1200000;
    policy.set_context(eligible, now);
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "eye rest began before 10 seconds idle");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeRest, "idle overdue rest did not start at 10 seconds");
}

void full_twenty_second_rest_and_two_second_completion()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    auto state = policy.snapshot(now);
    check(state.program == Program::EyeRest && state.eye_phase == EyePhase::Intro && state.remaining_ms == 0 &&
              state.phase_elapsed_ms == 0 && state.completed_rests == 0,
          "due reminder did not begin with its unpredicted intro");
    run(policy, now, 60000);
    state = policy.snapshot(now);
    check(state.eye_phase == EyePhase::Intro && state.phase_elapsed_ms == 60000 &&
              state.remaining_ms == 0 && state.completed_rests == 0 && state.eye_remaining_ms == 0,
          "intro auto-finished or reset the work clock without real playback acknowledgement");
    enter_rest(policy, now);
    state = policy.snapshot(now);
    check(state.remaining_ms == 20000 && state.completed_rests == 0, "rest did not start at full 20 seconds");
    run(policy, now, 19999);
    state = policy.snapshot(now);
    check(state.program == Program::EyeRest && state.remaining_ms == 1 && state.completed_rests == 0,
          "rest completed before its full duration");
    run(policy, now, 1);
    state = policy.snapshot(now);
    check(state.program == Program::RestDone && state.remaining_ms == 2000 && state.completed_rests == 1 &&
              state.eye_remaining_ms == 20000, "completed rest did not reset one future interval");
    run(policy, now, 1999);
    check(policy.snapshot(now).program == Program::RestDone, "completion celebration ended early");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Pet, "completion celebration did not return to pet");
    check(policy.snapshot(now).completed_rests == 1, "reading snapshots added completions");
}

void only_current_eligible_intro_acknowledgement_advances_the_sequence()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    const auto intro_generation = policy.snapshot(now).generation;
    check(!policy.finish_eye_intro(intro_generation - 1u, now) &&
              !policy.finish_eye_intro(intro_generation + 1u, now),
          "wrong generation finished an intro");
    run(policy, now, 700);
    const auto intro = policy.snapshot(now);
    check(intro.eye_phase == EyePhase::Intro && intro.generation == intro_generation && intro.phase_elapsed_ms == 700,
          "rejected acknowledgement changed the real intro phase");
    auto fresh = policy;
    check(fresh.finish_eye_intro(intro_generation, now + 499u), "499 ms context rejected a current intro acknowledgement");
    auto stale = policy;
    check(!stale.finish_eye_intro(intro_generation, now + 500u), "stale context accepted an intro acknowledgement");
    auto backwards = policy;
    check(!backwards.finish_eye_intro(intro_generation, now - 1u), "backdated intro acknowledgement rewound time");
    check(policy.finish_eye_intro(intro_generation, now), "eligible current intro acknowledgement failed");
    auto state = policy.snapshot(now);
    check(state.eye_phase == EyePhase::Motion && state.generation == intro_generation + 1u &&
              state.phase_elapsed_ms == 0 && state.remaining_ms == kMotionDurationMs,
          "real intro acknowledgement did not start motion exactly once");
    check(!policy.finish_eye_intro(intro_generation, now) && !policy.finish_eye_intro(state.generation, now),
          "duplicate or motion-generation acknowledgement restarted motion");
    run(policy, now, kMotionDurationMs - 1u);
    state = policy.snapshot(now);
    check(state.eye_phase == EyePhase::Motion && state.remaining_ms == 1 && state.phase_elapsed_ms == kMotionDurationMs - 1u,
          "motion finished before all twelve rhythm beats");
    const auto readonly = policy.snapshot(now + 1u);
    check(readonly.eye_phase == EyePhase::Motion && readonly.remaining_ms == 0 &&
              readonly.generation == intro_generation + 1u && readonly.completed_rests == 0,
          "read-only snapshot advanced motion into rest");
    run(policy, now, 1);
    state = policy.snapshot(now);
    check(state.eye_phase == EyePhase::Rest && state.remaining_ms == 20000 && state.phase_elapsed_ms == 0 &&
              state.generation == intro_generation + 2u && state.completed_rests == 0 && state.eye_remaining_ms == 0,
          "rest did not begin as a new complete phase after motion");
    check(!policy.finish_eye_intro(intro_generation, now) && !policy.finish_eye_intro(state.generation, now),
          "old acknowledgement changed a running rest");

    policy.set_context({false, false, false}, now);
    policy.set_context(eligible, now);
    run(policy, now, 10000);
    const auto next = policy.snapshot(now);
    check(next.eye_phase == EyePhase::Intro && next.generation != intro_generation &&
              !policy.finish_eye_intro(intro_generation, now),
          "cancelled prior intro acknowledgement finished a replacement intro");
    check(policy.finish_eye_intro(next.generation, now), "replacement intro could not acknowledge real playback");
}

void delayed_worker_tick_never_borrows_time_from_rest()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    run(policy, now, 60000);
    check(policy.snapshot(now).eye_phase == EyePhase::Intro && policy.snapshot(now).completed_rests == 0,
          "long real intro implicitly counted as a rest");
    check(policy.finish_eye_intro(policy.snapshot(now).generation, now), "delayed-worker intro acknowledgement failed");
    const auto motion_generation = policy.snapshot(now).generation;
    // UI contexts remain healthy while the lower-priority worker misses ticks.
    for (unsigned frame = 0; frame < 600; ++frame) {
        now += 100;
        policy.set_context(eligible, now);
    }
    auto state = policy.snapshot(now);
    check(state.eye_phase == EyePhase::Motion && state.phase_elapsed_ms == 60000 && state.remaining_ms == 0 &&
              state.completed_rests == 0, "snapshot consumed delayed motion time as rest");
    policy.tick(now);
    state = policy.snapshot(now);
    check(state.eye_phase == EyePhase::Rest && state.phase_elapsed_ms == 0 && state.remaining_ms == 20000 &&
              state.generation == motion_generation + 1u && state.completed_rests == 0 && state.eye_remaining_ms == 0,
          "large worker tick shortened or skipped the independent rest");
    run(policy, now, 19999);
    check(policy.snapshot(now).remaining_ms == 1 && policy.snapshot(now).completed_rests == 0,
          "large motion overrun shortened the rest deadline");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::RestDone && policy.snapshot(now).completed_rests == 1 &&
              policy.snapshot(now).eye_remaining_ms == 20000,
          "full delayed rest did not reset the work clock at its own completion");
}

void motion_timeline_is_bounded_and_returns_to_centre()
{
    static_assert(kMotionBeatMs == 1000 && kMotionDurationMs == 12000 && kMotionBeats.size() == 12);
    constexpr std::array<std::int16_t, 12> yaw{{-40, -80, 0, 40, 80, 0, 0, 0, 0, 0, 0, 0}};
    constexpr std::array<std::int16_t, 12> pitch{{0, 0, 0, 0, 0, 0, -20, -40, 0, 20, 40, 0}};
    for (std::uint32_t beat = 0; beat < kMotionBeats.size(); ++beat) {
        for (const auto edge : {0u, kMotionBeatMs - 1u}) {
            const auto elapsed = beat * kMotionBeatMs + edge;
            const auto pose = motion_beat(elapsed);
            check(pose.yaw_delta == yaw[beat] && pose.pitch_delta == pitch[beat],
                  "motion timeline did not preserve the complete one-second beat");
            check(motion_group(elapsed) == beat / 3u + 1u &&
                      motion_step(elapsed) == (beat + 1u) % 3u &&
                      motion_is_center(elapsed) == (beat % 3u == 2u),
                  "motion display ordinals disagreed with the one-two-centre groups");
            check((pose.yaw_delta == 0 && pose.pitch_delta == 0) == motion_is_center(elapsed),
                  "a group's third beat did not hold the head at centre");
        }
    }
    for (const auto elapsed : {kMotionDurationMs, kMotionDurationMs + 1u, std::numeric_limits<std::uint32_t>::max()}) {
        const auto pose = motion_beat(elapsed);
        check(pose.yaw_delta == 0 && pose.pitch_delta == 0, "completed motion did not remain at centre");
        check(motion_group(elapsed) == 0 && motion_step(elapsed) == 0 && motion_is_center(elapsed),
              "completed motion exposed a current group or movement step");
    }
}

void intro_and_motion_boundaries_survive_uint32_wrap()
{
    for (const auto lead : {22500u, 24500u}) {
        Config config;
        config.eye_interval_ms = 20000;
        std::uint32_t now = std::numeric_limits<std::uint32_t>::max() - lead;
        auto policy = make_policy(config, now);
        run(policy, now, 20000);
        const auto intro_generation = policy.snapshot(now).generation;
        const auto intro_delay = lead == 22500u ? 3000u : 1000u;
        run(policy, now, intro_delay);
        check(policy.snapshot(now).eye_phase == EyePhase::Intro &&
                  policy.snapshot(now).phase_elapsed_ms == intro_delay &&
                  policy.snapshot(now).generation == intro_generation,
              "intro elapsed time or generation failed across uint32 wrap");
        check(policy.finish_eye_intro(intro_generation, now), "wrapped intro acknowledgement rejected");
        run(policy, now, kMotionDurationMs - 1u);
        auto state = policy.snapshot(now);
        check(state.eye_phase == EyePhase::Motion && state.phase_elapsed_ms == kMotionDurationMs - 1u && state.remaining_ms == 1,
              "motion beat timing failed across uint32 wrap");
        run(policy, now, 1);
        state = policy.snapshot(now);
        check(state.eye_phase == EyePhase::Rest && state.phase_elapsed_ms == 0 && state.remaining_ms == 20000 &&
                  state.completed_rests == 0 && state.generation == intro_generation + 2u,
              "wrapped motion boundary did not preserve the complete rest");
        run(policy, now, 20000);
        check(policy.snapshot(now).program == Program::RestDone && policy.snapshot(now).completed_rests == 1,
              "wrapped intro/motion sequence failed to complete once");
    }
}

void ordinary_activity_preserves_started_animation_and_holds_idle_cards()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    // Continuous ordinary touch must not cancel, restart, or prolong any phase.
    auto generation = policy.snapshot(now).generation;
    for (unsigned touch = 0; touch < 50; ++touch) {
        now += 20;
        policy.note_activity(now);
        policy.set_context(eligible, now);
        policy.tick(now);
        const auto state = policy.snapshot(now);
        check(state.program == Program::EyeRest && state.eye_phase == EyePhase::Intro &&
                  state.generation == generation && state.phase_elapsed_ms == (touch + 1u) * 20u,
              "ordinary touch restarted or interrupted the intro");
    }
    check(policy.finish_eye_intro(generation, now), "touch test intro acknowledgement rejected");
    generation = policy.snapshot(now).generation;
    constexpr auto motion_touches = kMotionDurationMs / 20u;
    for (unsigned touch = 0; touch < motion_touches; ++touch) {
        now += 20;
        policy.note_activity(now);
        policy.set_context(eligible, now);
        policy.tick(now);
        const auto state = policy.snapshot(now);
        if (touch + 1u < motion_touches) {
            check(state.eye_phase == EyePhase::Motion && state.generation == generation &&
                      state.remaining_ms == kMotionDurationMs - (touch + 1u) * 20u,
                  "ordinary touch interrupted or prolonged the motion");
        }
    }
    const auto rest_started = now;
    generation = policy.snapshot(now).generation;
    for (unsigned touch = 0; touch < 1000; ++touch) {
        now += 20;
        policy.note_activity(now);
        policy.set_context(eligible, now);
        policy.tick(now);
        const auto state = policy.snapshot(now);
        if (touch < 999) {
            check(state.program == Program::EyeRest && state.eye_phase == EyePhase::Rest &&
                      state.remaining_ms == 20000u - (now - rest_started) && state.completed_rests == 0,
                  "ordinary touch interrupted the highest-priority animation");
            check(state.generation == generation, "ordinary touch restarted animation generation");
        }
    }
    check(policy.snapshot(now).completed_rests == 1 && policy.snapshot(now).program == Program::RestDone,
          "touching during animation prevented its complete twenty-second play");

    policy = make_policy();
    now = 0;
    const auto pet_generation = policy.snapshot(now).generation;
    for (unsigned touch = 0; touch < 1000; ++touch) {
        now += 20;
        policy.note_activity(now);
        policy.set_context(eligible, now);
        policy.tick(now);
        check(policy.snapshot(now).program == Program::Pet && policy.snapshot(now).generation == pet_generation,
              "held touch started an ordinary card or changed pet generation");
    }
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "ordinary card ignored fresh idle grace");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).remaining_ms == 12000,
          "ordinary card failed to resume after held touch ended");
}

void blocked_context_and_asynchronous_staleness_cancel()
{
    for (const auto blocked : {Context{false, false, false}, Context{true, true, false}}) {
      for (const auto phase : {EyePhase::Intro, EyePhase::Motion, EyePhase::Rest}) {
        Config config;
        config.eye_interval_ms = 20000;
        auto policy = make_policy(config);
        std::uint32_t now = 0;
        run(policy, now, 20000);
        if (phase == EyePhase::Rest) enter_rest(policy, now);
        else if (phase == EyePhase::Motion)
            check(policy.finish_eye_intro(policy.snapshot(now).generation, now), "exit test intro acknowledgement rejected");
        const auto cancelled_generation = policy.snapshot(now).generation;
        policy.set_context(blocked, now);
        check(policy.snapshot(now).blocked && policy.snapshot(now).program == Program::Pet &&
                  policy.snapshot(now).completed_rests == 0, "explicit exit/foreground did not cancel rest");
        check(!policy.finish_eye_intro(cancelled_generation, now), "cancelled sequence accepted an obsolete intro acknowledgement");
        run(policy, now, 20000, blocked);
        run(policy, now, 100);
        check(policy.snapshot(now).program == Program::Pet, "unblock reused time spent in a foreground task");
        run(policy, now, 10000);
        check(policy.snapshot(now).program == Program::EyeRest && policy.snapshot(now).eye_phase == EyePhase::Intro &&
                  policy.snapshot(now).phase_elapsed_ms == 0,
              "due was lost or resumed a partial phase after foreground cancellation");
      }
    }
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    check(policy.snapshot(now + 499u).program == Program::EyeRest, "499 ms context was rejected");
    check(policy.snapshot(now + 500u).blocked && policy.snapshot(now + 500u).program == Program::Pet,
          "500 ms stale asynchronous context remained eligible");
    // A late UI refresh must not hide the interruption and award completion.
    now += 20000;
    policy.set_context(eligible, now);
    policy.tick(now);
    check(policy.snapshot(now).completed_rests == 0 && policy.snapshot(now).program == Program::Pet,
          "late context refresh retroactively completed a rest");
}

void persistent_distance_alert_allows_and_cannot_interrupt_due_rest()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    const Context alert{true, false, true};
    policy.set_context(alert, now);
    run(policy, now, 19999, alert);
    check(policy.snapshot(now).blocked && policy.snapshot(now).program == Program::Pet,
          "distance warning did not suppress ordinary cards");
    run(policy, now, 1, alert);
    auto state = policy.snapshot(now);
    check(!state.blocked && state.program == Program::EyeRest && state.eye_phase == EyePhase::Intro && state.remaining_ms == 0,
          "distance alert reset idle activity forever or prevented a due animation");
    run(policy, now, 5000, alert);
    check(!policy.snapshot(now).blocked && policy.snapshot(now).eye_phase == EyePhase::Intro,
          "distance alert cancelled the ongoing intro");
    enter_rest(policy, now, alert);
    run(policy, now, 19999, alert);
    state = policy.snapshot(now);
    check(!state.blocked && state.program == Program::EyeRest && state.remaining_ms == 1 && state.completed_rests == 0,
          "persistent alert interrupted or shortened the animation");
    run(policy, now, 1, alert);
    state = policy.snapshot(now);
    check(state.completed_rests == 1 && state.blocked && state.program == Program::Pet,
          "complete animation did not hand the surface back to the ongoing alert");
}

void eye_rest_rejects_foreground_claim_without_changing_playback()
{
  for (const auto phase : {EyePhase::Intro, EyePhase::Motion, EyePhase::Rest}) {
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    if (phase == EyePhase::Rest) enter_rest(policy, now);
    else if (phase == EyePhase::Motion)
        check(policy.finish_eye_intro(policy.snapshot(now).generation, now), "claim test intro acknowledgement rejected");
    run(policy, now, phase == EyePhase::Rest ? 5000u : 1000u);
    const auto before = policy.snapshot(now);
    check(before.program == Program::EyeRest && before.eye_phase == phase, "claim test did not enter intended eye phase");
    check(!policy.try_begin_foreground_animation(now), "foreground claim interrupted an already started eye rest");
    const auto after = policy.snapshot(now);
    check(after.program == before.program && after.remaining_ms == before.remaining_ms &&
              after.generation == before.generation && after.eye_phase == before.eye_phase &&
              after.phase_elapsed_ms == before.phase_elapsed_ms && !after.blocked,
          "rejected foreground claim modified remaining animation time or ownership");
    if (phase == EyePhase::Intro) enter_rest(policy, now);
    else if (phase == EyePhase::Motion) run(policy, now, kMotionDurationMs - 1000u);
    run(policy, now, phase == EyePhase::Rest ? 15000u : 20000u);
    check(policy.snapshot(now).completed_rests == 1, "rejected claim prevented completion at the original deadline");
  }
}

void foreground_claim_wins_tick_race_and_preserves_one_due()
{
    for (const bool start_failed : {false, true}) {
        Config config;
        config.eye_interval_ms = 20000;
        auto policy = make_policy(config);
        std::uint32_t now = 0;
        run(policy, now, 19999);
        check(policy.try_begin_foreground_animation(now), "foreground claim was denied before eye rest began");
        // There is deliberately no new context before this worker tick.
        policy.tick(now + 1u);
        check(policy.snapshot(now + 1u).blocked && policy.snapshot(now + 1u).program == Program::Pet &&
                  policy.snapshot(now + 1u).eye_remaining_ms == 0,
              "worker started eye rest in the check/start gap after a foreground claim");
        ++now;
        if (!start_failed) run(policy, now, 60000, {true, true, true});
        policy.set_context(eligible, now);
        run(policy, now, 9999);
        check(policy.snapshot(now).program == Program::Pet && policy.snapshot(now).completed_rests == 0,
              "foreground playback or failed start queued multiple rests or reused busy time as idle");
        run(policy, now, 1);
        check(policy.snapshot(now).program == Program::EyeRest && policy.snapshot(now).eye_phase == EyePhase::Intro &&
                  policy.snapshot(now).remaining_ms == 0 && policy.snapshot(now).phase_elapsed_ms == 0 &&
                  policy.snapshot(now).completed_rests == 0,
              "foreground completion/failure lost the single pending due");
    }
}

void explicit_program_disable_can_stop_animation()
{
  for (const auto phase : {EyePhase::Intro, EyePhase::Motion, EyePhase::Rest}) {
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 25000);
    if (phase == EyePhase::Rest) enter_rest(policy, now);
    else if (phase == EyePhase::Motion)
        check(policy.finish_eye_intro(policy.snapshot(now).generation, now), "disable test intro acknowledgement rejected");
    config.enabled = false;
    check(policy.set_config(config, now), "explicit program disable rejected");
    check(policy.snapshot(now).blocked && policy.snapshot(now).program == Program::Pet &&
              policy.snapshot(now).completed_rests == 0 && policy.snapshot(now).eye_remaining_ms == 0,
          "explicit program disable failed to stop animation without awarding completion");
    run(policy, now, 1000);
    config.enabled = true;
    check(policy.set_config(config, now), "program reenable rejected");
    policy.set_context(eligible, now);
    run(policy, now, 10000);
    check(policy.snapshot(now).program == Program::EyeRest && policy.snapshot(now).eye_phase == EyePhase::Intro &&
              policy.snapshot(now).remaining_ms == 0 && policy.snapshot(now).phase_elapsed_ms == 0,
          "explicitly cancelled eye due did not restart completely after reenable and fresh idle");
  }
}

void overdue_intervals_never_form_a_backlog()
{
    Config config;
    config.eye_interval_ms = 20000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 180000, {true, true, false});
    run(policy, now, 10100);
    check(policy.snapshot(now).program == Program::EyeRest, "overdue single rest did not start");
    enter_rest(policy, now);
    run(policy, now, 20000);
    check(policy.snapshot(now).completed_rests == 1, "one due generated multiple completions");
    run(policy, now, 19999);
    check(policy.snapshot(now).program != Program::EyeRest && policy.snapshot(now).completed_rests == 1,
          "missed intervals were replayed as a backlog");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeRest, "next interval was not measured from completion");
}

void twelve_three_twelve_three_information_cadence()
{
    auto policy = make_policy();
    check(policy.update_weekly(quota(), 0), "quota rejected");
    std::uint32_t now = 0;
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "information appeared before idle grace");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).remaining_ms == 12000 &&
              policy.snapshot(now).eye_remaining_ms == 1190000,
          "first 12-second countdown did not display the independent eye deadline");
    run(policy, now, 11999);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).remaining_ms == 1,
          "countdown card ended early");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Pet, "countdown did not return to pet after 12 seconds");
    run(policy, now, 2999);
    check(policy.snapshot(now).program == Program::Pet, "information cards skipped the three-second pet gap");
    run(policy, now, 1);
    check(now == 25000 && policy.snapshot(now).program == Program::Weekly && policy.snapshot(now).remaining_ms == 12000,
          "first weekly card incorrectly waited for its normal interval");
    run(policy, now, 12000);
    check(policy.snapshot(now).program == Program::Pet, "weekly card did not end after 12 seconds");
    run(policy, now, 3000);
    check(policy.snapshot(now).program == Program::EyeCountdown, "second cycle did not alternate back to countdown");
    run(policy, now, 15000);
    check(now == 55000 && policy.snapshot(now).program == Program::Weekly,
          "weekly starts did not preserve the 30-second lower bound");
    check(policy.snapshot(now).completed_rests == 0 && policy.snapshot(now).eye_remaining_ms == 1145000,
          "information cards advanced or completed the independent eye-rest timer");
}

void alternating_cards_are_fair_and_weekly_interval_is_a_lower_bound()
{
    auto policy = make_policy();
    policy.update_weekly(quota(), 0);
    std::uint32_t now = 0;
    run(policy, now, 10000);
    std::uint32_t countdown_ms = 0, weekly_ms = 0, pet_ms = 0;
    for (unsigned sample = 0; sample < 1500; ++sample) {
        const auto state = policy.snapshot(now);
        if (state.program == Program::EyeCountdown) countdown_ms += 100;
        else if (state.program == Program::Weekly) weekly_ms += 100;
        else if (state.program == Program::Pet) pet_ms += 100;
        else check(false, "ordinary rotation created an eye-rest completion");
        run(policy, now, 100);
    }
    check(countdown_ms == 60000 && weekly_ms == 60000 && pet_ms == 30000,
          "five normal cycles failed 12/3/12/3 fairness and 80 percent information occupancy");

    Config config;
    config.weekly_interval_ms = 60000;
    policy = make_policy(config);
    policy.update_weekly(quota(), 0);
    now = 0;
    run(policy, now, 25000);
    check(policy.snapshot(now).program == Program::Weekly, "first weekly card was not immediately eligible");
    run(policy, now, 30000);
    check(policy.snapshot(now).program == Program::EyeCountdown,
          "weekly interval delay extended plain pet instead of showing another countdown");
    run(policy, now, 29999);
    check(policy.snapshot(now).program == Program::Pet, "weekly card ignored its longer configured lower bound");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Weekly, "weekly lower bound prevented a due card forever");
}

void eye_due_preempts_each_information_card_and_pet_gap()
{
    for (const auto deadline : {15000u, 27000u}) {
        Config config;
        config.eye_interval_ms = deadline;
        auto policy = make_policy(config);
        policy.update_weekly(quota(), 0);
        std::uint32_t now = 0;
        run(policy, now, deadline - 1000u);
        check(policy.snapshot(now).program == (deadline == 15000 ? Program::EyeCountdown : Program::Weekly),
              "preemption test did not enter the intended information card");
        run(policy, now, 1000);
        check(policy.snapshot(now).program == Program::EyeRest && policy.snapshot(now).eye_phase == EyePhase::Intro &&
                  policy.snapshot(now).remaining_ms == 0 && policy.snapshot(now).phase_elapsed_ms == 0 &&
                  policy.snapshot(now).completed_rests == 0,
              "due eye sequence did not replace the information card with a complete intro");
    }
    Config config;
    config.eye_interval_ms = 20000;
    config.pet_gap_ms = 30000;
    auto policy = make_policy(config);
    std::uint32_t now = 0;
    run(policy, now, 20000);
    check(policy.snapshot(now).program == Program::EyeRest, "eye due waited for an ordinary pet gap");
}

void interrupted_information_is_discarded_and_next_card_waits_for_idle()
{
    auto policy = make_policy();
    policy.update_weekly(quota(), 0);
    std::uint32_t now = 0;
    run(policy, now, 12000);
    check(policy.snapshot(now).program == Program::EyeCountdown, "interruption test did not start a countdown card");
    policy.set_context({true, true, false}, now);
    run(policy, now, 5000, {true, true, false});
    policy.set_context(eligible, now);
    run(policy, now, 9999);
    check(policy.snapshot(now).program == Program::Pet, "foreground interruption did not reset idle grace");
    run(policy, now, 1);
    check(policy.snapshot(now).program == Program::Weekly && policy.snapshot(now).remaining_ms == 12000 &&
              policy.snapshot(now).completed_rests == 0,
          "interrupted countdown was queued or counted as a completed eye rest");
    run(policy, now, 1000);
    policy.note_activity(now);
    run(policy, now, 10000);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).completed_rests == 0,
          "touch interruption replayed the weekly card instead of moving to the next card");
}

void interrupted_weekly_does_not_extend_the_pure_pet_gap()
{
    for (const bool foreground : {false, true}) {
        auto policy = make_policy();
        policy.update_weekly(quota(), 0);
        std::uint32_t now = 0;
        run(policy, now, 25000);
        check(policy.snapshot(now).program == Program::Weekly, "interruption test did not start weekly");
        if (foreground) {
            policy.set_context({true, true, false}, now);
            run(policy, now, 1000, {true, true, false});
            policy.set_context(eligible, now);
            run(policy, now, 10000);
        } else {
            WeeklyUpdate unavailable;
            unavailable.sequence = 2;
            policy.update_weekly(unavailable, now);
            run(policy, now, 3000);
        }
        check(policy.snapshot(now).program == Program::EyeCountdown,
              "interrupted weekly did not advance to a countdown");
        run(policy, now, 12000);
        check(policy.snapshot(now).program == Program::Pet, "countdown did not finish before fallback");
        run(policy, now, 2999);
        check(policy.snapshot(now).program == Program::Pet, "fallback skipped the configured pet gap");
        run(policy, now, 1);
        check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).remaining_ms == 12000 &&
                  policy.snapshot(now).completed_rests == 0,
              "pending weekly lower bound extended pure pet beyond three seconds");
        run(policy, now, 15000);
        check(policy.snapshot(now).program == Program::Weekly,
              "fallback countdown starved weekly after its lower bound became due");
    }
}

void weekly_duplicates_old_observations_and_ttl_do_not_refresh()
{
    auto policy = make_policy();
    auto value = quota();
    value.ttl_ms = 60000;
    check(policy.update_weekly(value, 0), "first quota rejected");
    check(policy.snapshot(59999).weekly_status == WeeklyStatus::Fresh, "TTL ended early");
    check(!policy.update_weekly(value, 59999), "duplicate sequence was accepted");
    check(policy.snapshot(60000).weekly_status == WeeklyStatus::Stale && policy.snapshot(60000).weekly_left == -1,
          "duplicate refreshed expired quota or stale quota exposed fresh percent");
    value.sequence = 2;
    value.ttl_ms = 900000;
    check(policy.update_weekly(value, 60000) && policy.snapshot(60000).weekly_status == WeeklyStatus::Stale,
          "same observed timestamp with a new sequence extended old TTL");
    value.sequence = 3;
    --value.observed_at;
    check(!policy.update_weekly(value, 60001), "backward observed_at was accepted");
    value = quota(3, 1700000061u);
    check(policy.update_weekly(value, 61000) && policy.snapshot(61000).weekly_status == WeeklyStatus::Fresh,
          "new non-backward query could not refresh quota");
}

void reset_deadline_absolute_clock_and_shortened_reconnect_ttl()
{
    auto policy = make_policy();
    auto value = quota();
    value.reset_at = value.observed_at + 30u;
    value.ttl_ms = 60000;
    policy.update_weekly(value, 0);
    check(policy.snapshot(29999).weekly_status == WeeklyStatus::Fresh &&
              policy.snapshot(30000).weekly_status == WeeklyStatus::Stale, "reset did not expire earlier than TTL");
    policy = make_policy();
    value = quota();
    value.ttl_ms = 60000;
    policy.update_weekly(value, 0, value.observed_at + 60u);
    check(policy.snapshot(0).weekly_status == WeeklyStatus::Stale, "already-expired absolute-clock data became fresh");
    policy = make_policy();
    value.ttl_ms = 120000; // Reconnected host supplied only two minutes of its cache budget.
    policy.update_weekly(value, 0);
    check(policy.snapshot(119999).weekly_status == WeeklyStatus::Fresh &&
              policy.snapshot(120000).weekly_status == WeeklyStatus::Stale, "reconnection expanded a shortened TTL");
}

void unavailable_is_explicit_and_missing_data_is_not_a_card()
{
    auto policy = make_policy();
    std::uint32_t now = 0;
    run(policy, now, 60000);
    check(policy.snapshot(now).program == Program::EyeCountdown && policy.snapshot(now).weekly_status == WeeklyStatus::Waiting &&
              policy.snapshot(now).completed_rests == 0,
          "missing quota fabricated a weekly card");
    WeeklyUpdate unavailable;
    unavailable.sequence = 1;
    check(policy.update_weekly(unavailable, now), "explicit unavailable rejected");
    run(policy, now, 10000);
    check(policy.snapshot(now).program == Program::Weekly && policy.snapshot(now).weekly_status == WeeklyStatus::Unavailable &&
              policy.snapshot(now).weekly_left == -1, "explicit unavailable was presented as a percentage");
}

void stale_quota_rotates_without_a_current_percentage()
{
    auto policy = make_policy();
    auto value = quota();
    value.ttl_ms = 60000;
    policy.update_weekly(value, 0);
    std::uint32_t now = 0;
    run(policy, now, 60000);
    auto state = policy.snapshot(now);
    check(state.program == Program::Weekly && state.weekly_status == WeeklyStatus::Stale && state.weekly_left == -1,
          "expired quota failed to rotate explicitly as stale without an old percentage");
    run(policy, now, 7000);
    check(policy.snapshot(now).program == Program::Pet, "stale weekly card failed to return to pet");
    run(policy, now, 18000);
    state = policy.snapshot(now);
    check(state.program == Program::Weekly && state.weekly_status == WeeklyStatus::Stale && state.weekly_left == -1,
          "stale status did not preserve the normal weekly rotation cadence");
}

void stored_config_v1_migrates_cadence_and_v2_preserves_settings()
{
    Config loaded;
    const StoredConfig legacy{1, 0, 600000, 45000, 1234, 30000, 6000, 60000};
    check(decode_config(legacy, loaded) && !loaded.enabled && loaded.eye_interval_ms == 600000 &&
              loaded.rest_duration_ms == 45000 && loaded.idle_grace_ms == 10000 && loaded.pet_gap_ms == 3000 &&
              loaded.weekly_duration_ms == 12000 && loaded.weekly_interval_ms == 30000,
          "version-one migration retained hidden old cadence or lost visible user settings");
    const StoredConfig current{2, 1, 900000, 30000, 5000, 4000, 15000, 45000};
    check(decode_config(current, loaded) && loaded.enabled && loaded.eye_interval_ms == 900000 &&
              loaded.rest_duration_ms == 30000 && loaded.idle_grace_ms == 5000 && loaded.pet_gap_ms == 4000 &&
              loaded.weekly_duration_ms == 15000 && loaded.weekly_interval_ms == 45000,
          "version-two settings did not survive decoding");
    auto invalid = current;
    invalid.version = 3;
    check(!decode_config(invalid, loaded) && loaded.weekly_interval_ms == 45000, "unknown storage version mutated current settings");
    invalid = current;
    invalid.enabled = 2;
    check(!decode_config(invalid, loaded), "invalid stored boolean accepted");
}

void visual_motion_is_continuous_and_holds_each_completed_target()
{
    check(kMotionMoveMs < kMotionBeatMs, "motion must retain the between-beat hold");
    auto previous = motion_visual_offset(0);
    check(previous.yaw_delta == 0 && previous.pitch_delta == 0, "visual motion must begin at neutral");
    for (std::uint32_t time = 1; time <= kMotionDurationMs; ++time) {
        const auto current = motion_visual_offset(time);
        check(std::abs(current.yaw_delta - previous.yaw_delta) < 0.35f &&
                  std::abs(current.pitch_delta - previous.pitch_delta) < 0.35f,
              "visual motion jumps between adjacent milliseconds or beat boundaries");
        check(std::abs(current.yaw_delta) <= 80 && std::abs(current.pitch_delta) <= 40,
              "visual easing overshot the original motion envelope");
        previous = current;
    }
    for (std::uint32_t beat = 0; beat < kMotionBeats.size(); ++beat) {
        const auto target = kMotionBeats[beat];
        for (const auto offset : {static_cast<std::uint32_t>(kMotionMoveMs), kMotionBeatMs - 1}) {
            const auto settled = motion_visual_offset(beat * kMotionBeatMs + offset);
            check(settled.yaw_delta == target.yaw_delta && settled.pitch_delta == target.pitch_delta,
                  "visual movement failed to settle and hold the same hardware target");
        }
    }
    check(motion_visual_offset(UINT32_MAX).yaw_delta == 0 &&
              motion_visual_offset(UINT32_MAX).pitch_delta == 0,
          "completed or oversized phase must remain at neutral");
}

void clock_wrap_and_read_only_snapshots()
{
    Config config;
    config.eye_interval_ms = 20000;
    std::uint32_t now = std::numeric_limits<std::uint32_t>::max() - 25000u;
    auto policy = make_policy(config, now);
    const auto started = now;
    auto value = quota();
    value.ttl_ms = 60000;
    policy.update_weekly(value, now);
    run(policy, now, 20000);
    enter_rest(policy, now);
    run(policy, now, 20000);
    check(now < started && policy.snapshot(now).program == Program::RestDone && policy.snapshot(now).completed_rests == 1,
          "countdown/completion failed across uint32 wrap");
    check(policy.snapshot(now).weekly_status == WeeklyStatus::Fresh, "quota aged incorrectly across uint32 wrap");
    check(policy.snapshot(now - 1u).blocked, "backdated snapshot was treated as current context");
    const auto generation = policy.snapshot(now).generation;
    policy.note_activity(now - 1u);
    check(policy.snapshot(now).generation == generation, "backdated activity rewound policy state");
    run(policy, now, 20000);
    check(policy.snapshot(now).weekly_status == WeeklyStatus::Stale, "wrapped monotonic TTL failed to expire");
}

} // namespace

int main()
{
    const std::array tests{defaults_and_configuration_bounds, twenty_minute_due_waits_for_idle_eligibility,
        full_twenty_second_rest_and_two_second_completion, ordinary_activity_preserves_started_animation_and_holds_idle_cards,
        only_current_eligible_intro_acknowledgement_advances_the_sequence,
        delayed_worker_tick_never_borrows_time_from_rest, motion_timeline_is_bounded_and_returns_to_centre,
        intro_and_motion_boundaries_survive_uint32_wrap,
        blocked_context_and_asynchronous_staleness_cancel, overdue_intervals_never_form_a_backlog,
        persistent_distance_alert_allows_and_cannot_interrupt_due_rest,
        eye_rest_rejects_foreground_claim_without_changing_playback,
        foreground_claim_wins_tick_race_and_preserves_one_due, explicit_program_disable_can_stop_animation,
        twelve_three_twelve_three_information_cadence, alternating_cards_are_fair_and_weekly_interval_is_a_lower_bound,
        eye_due_preempts_each_information_card_and_pet_gap, interrupted_information_is_discarded_and_next_card_waits_for_idle,
        interrupted_weekly_does_not_extend_the_pure_pet_gap,
        weekly_duplicates_old_observations_and_ttl_do_not_refresh, reset_deadline_absolute_clock_and_shortened_reconnect_ttl,
        unavailable_is_explicit_and_missing_data_is_not_a_card, stale_quota_rotates_without_a_current_percentage,
        stored_config_v1_migrates_cadence_and_v2_preserves_settings,
        visual_motion_is_continuous_and_holds_each_completed_target, clock_wrap_and_read_only_snapshots};
    try { for (const auto test : tests) test(); }
    catch (const std::exception& error) { std::cerr << "idle_program_test: " << error.what() << '\n'; return 1; }
    std::cout << "idle_program_test: " << tests.size() << " policy groups passed\n";
    return 0;
}
