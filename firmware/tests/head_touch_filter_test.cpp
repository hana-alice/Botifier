#ifdef NDEBUG
#undef NDEBUG
#endif
#include <hal/head_touch_filter.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using head_touch::Filter;
unsigned assertions = 0;

void check(bool condition, const char* message)
{
    ++assertions;
    if (!condition) throw std::runtime_error(message);
    assert(condition);
}

void expect(Filter& filter, std::uint8_t raw, std::uint32_t now,
            std::uint8_t expected, const char* message)
{
    std::uint8_t output = 0xff;
    check(filter.sample(raw, true, now, output), "valid sample was discarded");
    check(output == expected, message);
}

void short_positive_pulses_do_not_accumulate()
{
    Filter filter;
    expect(filter, 0, 0, 0, "idle input produced contact");
    expect(filter, 1, 10, 0, "first positive sample produced contact");
    expect(filter, 1, 50, 0, "short pulse produced contact");
    expect(filter, 1, 109, 0, "99 ms pulse crossed the press boundary");
    expect(filter, 0, 110, 0, "short pulse survived its release");
    expect(filter, 1, 200, 0, "separate pulses accumulated press time");
    expect(filter, 1, 299, 0, "second 99 ms pulse produced contact");
    expect(filter, 0, 300, 0, "second short pulse survived its release");
}

void stable_press_and_hold_preserve_one_contact()
{
    Filter filter;
    expect(filter, 1, 0, 0, "first sample bypassed press confirmation");
    expect(filter, 1, 99, 0, "press accepted before 100 ms");
    expect(filter, 1, 100, 1, "100 ms press was not accepted");
    expect(filter, 1, 150, 1, "held contact disappeared");
    expect(filter, 1, 349, 1, "valid held contact was interrupted");
    expect(filter, 1, 549, 1, "exactly 200 ms sample gap interrupted contact");
}

void release_noise_retains_contact_until_200_ms()
{
    Filter filter;
    expect(filter, 1, 0, 0, "press was immediate");
    expect(filter, 1, 100, 1, "confirmed press was missing");
    expect(filter, 0, 150, 1, "one zero sample released contact");
    expect(filter, 0, 349, 1, "199 ms zero pulse released contact");
    expect(filter, 1, 350, 1, "release bounce generated a new press delay");
    expect(filter, 0, 400, 1, "new release inherited old release time");
    expect(filter, 0, 599, 1, "new release accepted before 200 ms");
    expect(filter, 0, 600, 0, "200 ms release was not accepted");
}

void a_real_release_allows_a_second_touch()
{
    Filter filter;
    expect(filter, 1, 0, 0, "first touch was immediate");
    expect(filter, 1, 100, 1, "first touch was not confirmed");
    expect(filter, 0, 150, 1, "release was immediate");
    expect(filter, 0, 350, 0, "stable release did not rearm contact");
    expect(filter, 4, 400, 0, "second touch skipped confirmation");
    expect(filter, 4, 499, 0, "second touch accepted too early");
    expect(filter, 4, 500, 4, "legitimate second touch was discarded");
}

void channel_changes_pass_through_without_false_release()
{
    Filter filter;
    expect(filter, 1, 0, 0, "initial channel was immediate");
    expect(filter, 4, 50, 0, "moving contact bypassed press confirmation");
    expect(filter, 16, 100, 16, "continuous contact lost time when its channel changed");
    expect(filter, 32, 150, 32, "confirmed contact did not pass its actual position");
    expect(filter, 0x15, 200, 0x15, "multi-channel contact bits were changed");
    expect(filter, 0, 250, 0x15, "pending release introduced a false swipe position");
    expect(filter, 0, 449, 0x15, "pending release lost its last nonzero position");
    expect(filter, 0, 450, 0, "stable channel release was not accepted");
}

void invalid_reads_break_confirmation_without_releasing_contact()
{
    Filter filter;
    std::uint8_t output = 0xff;
    expect(filter, 1, 0, 0, "initial touch was immediate");
    check(!filter.sample(1, false, 90, output), "failed read emitted a sample");
    expect(filter, 1, 100, 0, "failed read counted toward press confirmation");
    expect(filter, 1, 199, 0, "press inherited time from before the failed read");
    expect(filter, 1, 200, 1, "valid press after failed read did not recover");
    expect(filter, 0, 250, 1, "first release sample dropped confirmed contact");
    check(!filter.sample(0, false, 400, output), "failed release read emitted a sample");
    expect(filter, 0, 450, 1, "failed read counted toward release confirmation");
    expect(filter, 0, 649, 1, "release inherited time from before the failed read");
    expect(filter, 0, 650, 0, "valid release after failed read did not recover");
}

void gaps_above_200_ms_restart_confirmation()
{
    Filter filter;
    expect(filter, 1, 0, 0, "first sample was immediate");
    expect(filter, 1, 201, 0, "long sampling gap created a press");
    expect(filter, 1, 300, 0, "press retained time across a long sampling gap");
    expect(filter, 1, 301, 1, "press after resumed sampling was not confirmed");
    expect(filter, 0, 350, 1, "release was immediate");
    expect(filter, 0, 551, 1, "long sampling gap released confirmed contact");
    expect(filter, 0, 750, 1, "release retained time across a long sampling gap");
    expect(filter, 0, 751, 0, "release after resumed sampling was not confirmed");
}

void reserved_bits_never_create_contact()
{
    Filter filter;
    expect(filter, 0xc0, 0, 0, "reserved bits created contact");
    expect(filter, 0x80, 100, 0, "held reserved bit created contact");
    expect(filter, 0x40, 200, 0, "different reserved bit created contact");
    expect(filter, 0xc1, 250, 0, "real touch skipped confirmation with reserved bits");
    expect(filter, 0x81, 350, 1, "reserved bits leaked into confirmed contact");
    expect(filter, 0xc0, 400, 1, "reserved bits bypassed release confirmation");
    expect(filter, 0x80, 600, 0, "reserved bits prevented stable release");
}

void uint32_wrap_preserves_press_and_release_boundaries()
{
    Filter filter;
    constexpr auto start = std::numeric_limits<std::uint32_t>::max() - 50u;
    expect(filter, 1, start, 0, "touch at wrap boundary was immediate");
    expect(filter, 1, start + 99u, 0, "wrapped 99 ms press accepted early");
    expect(filter, 1, start + 100u, 1, "wrapped 100 ms press failed");
    expect(filter, 0, start + 150u, 1, "wrapped release was immediate");
    expect(filter, 0, start + 349u, 1, "wrapped 199 ms release accepted early");
    expect(filter, 0, start + 350u, 0, "wrapped 200 ms release failed");

    Filter releasing;
    expect(releasing, 4, 0, 0, "release-wrap setup skipped press confirmation");
    expect(releasing, 4, 100, 4, "release-wrap setup did not confirm contact");
    expect(releasing, 4, start - 50u, 4, "long gap lost confirmed contact");
    expect(releasing, 0, start, 4, "release before wrap was immediate");
    expect(releasing, 0, start + 199u, 4, "release crossing wrap accepted early");
    expect(releasing, 0, start + 200u, 0, "200 ms release crossing wrap failed");
}

void first_two_contacts(Filter& filter, std::uint32_t start = 0)
{
    expect(filter, 1, start, 0, "first contact skipped confirmation");
    expect(filter, 1, start + 100u, 1, "first legitimate contact was suppressed");
    expect(filter, 0, start + 150u, 1, "first release was immediate");
    expect(filter, 0, start + 350u, 0, "first release did not rearm contact");
    expect(filter, 4, start + 400u, 0, "second contact skipped confirmation");
    expect(filter, 4, start + 500u, 4, "second legitimate contact was suppressed");
    expect(filter, 0, start + 550u, 4, "second release was immediate");
    expect(filter, 0, start + 750u, 0, "second release did not rearm contact");
    check(!filter.isolated() && filter.suppressed_presses() == 0,
          "legitimate double touch entered isolation");
}

void enter_isolation(Filter& filter, std::uint32_t start = 0)
{
    first_two_contacts(filter, start);
    expect(filter, 16, start + 800u, 0, "third contact skipped confirmation");
    expect(filter, 16, start + 899u, 0, "third contact accepted before confirmation");
    expect(filter, 16, start + 900u, 0, "third confirmed contact escaped isolation");
    check(filter.isolated() && filter.suppressed_presses() == 1,
          "third confirmed contact did not isolate or count exactly once");
}

void third_contact_isolated_without_repeated_suppression_counts()
{
    Filter filter;
    enter_isolation(filter);
    for (std::uint32_t now = 1000; now <= 9000; now += 200)
        expect(filter, 16, now, 0, "held isolated input reached the gesture recognizer");
    check(filter.isolated() && filter.suppressed_presses() == 1,
          "held samples ended isolation or inflated the suppressed-contact counter");
}

void one_held_contact_never_counts_as_a_burst()
{
    Filter filter;
    expect(filter, 1, 0, 0, "held contact was immediate");
    expect(filter, 1, 100, 1, "held contact was not confirmed");
    for (std::uint32_t now = 300; now <= 10300; now += 200)
        expect(filter, 1, now, 1, "one held contact was mistaken for repeated contacts");
    expect(filter, 4, 10400, 4, "held contact channel change was suppressed");
    expect(filter, 16, 10500, 16, "held swipe channel change was suppressed");
    check(!filter.isolated() && filter.suppressed_presses() == 0,
          "one long touch or its swipe entered burst isolation");
}

void quiet_rearms_fresh_contacts_and_resets_the_burst()
{
    Filter filter;
    enter_isolation(filter);
    for (std::uint32_t now = 1000; now <= 2800; now += 200)
        expect(filter, 0, now, 0, "quiet isolated input was not released");
    expect(filter, 0, 2999, 0, "quiet confirmation produced contact");
    check(filter.isolated(), "1999 ms quiet period ended isolation early");
    expect(filter, 0, 3000, 0, "quiet recovery produced contact");
    check(!filter.isolated(), "2000 ms valid quiet did not restore input");
    expect(filter, 1, 3100, 0, "first recovered touch skipped confirmation");
    expect(filter, 1, 3200, 1, "first recovered legitimate touch was lost");
    expect(filter, 0, 3250, 1, "recovered release was immediate");
    expect(filter, 0, 3450, 0, "recovered release did not rearm");
    expect(filter, 4, 3500, 0, "second recovered contact skipped confirmation");
    expect(filter, 4, 3600, 4, "old burst count suppressed the second recovered touch");
    expect(filter, 0, 3650, 4, "second recovered release was immediate");
    expect(filter, 0, 3850, 0, "second recovered release did not rearm");
    expect(filter, 16, 3900, 0, "third recovered contact skipped confirmation");
    expect(filter, 16, 4000, 0, "third recovered contact escaped isolation");
    check(filter.isolated() && filter.suppressed_presses() == 2,
          "fresh burst did not enter a separately counted isolation");
}

void bouncing_input_restarts_quiet_confirmation()
{
    Filter filter;
    enter_isolation(filter);
    for (std::uint32_t now = 1000; now <= 2600; now += 200)
        expect(filter, 0, now, 0, "quiet period produced contact");
    expect(filter, 4, 2700, 0, "bounce escaped isolation");
    for (std::uint32_t now = 2800; now <= 4600; now += 200)
        expect(filter, 0, now, 0, "restarted quiet period produced contact");
    expect(filter, 0, 4799, 0, "quiet bounce recovery produced contact");
    check(filter.isolated(), "quiet time before the bounce was retained");
    expect(filter, 0, 4800, 0, "quiet recovery produced contact");
    check(!filter.isolated() && filter.suppressed_presses() == 1,
          "bounce recovery failed or counted each raw sample as a suppressed press");
}

void invalid_reads_cannot_count_as_quiet()
{
    Filter filter;
    enter_isolation(filter);
    for (std::uint32_t now = 1000; now <= 2600; now += 200)
        expect(filter, 0, now, 0, "quiet period produced contact");
    std::uint8_t output = 0xff;
    check(!filter.sample(0, false, 2700, output), "invalid isolated read emitted a sample");
    check(filter.isolated(), "read failure ended isolation");
    for (std::uint32_t now = 2800; now <= 4600; now += 200)
        expect(filter, 0, now, 0, "post-error quiet period produced contact");
    expect(filter, 0, 4799, 0, "post-error quiet period produced contact");
    check(filter.isolated(), "invalid read counted toward uninterrupted quiet");
    expect(filter, 0, 4800, 0, "post-error recovery produced contact");
    check(!filter.isolated(), "valid quiet did not recover after read failure");
}

void long_sample_gaps_cannot_count_as_quiet()
{
    Filter filter;
    enter_isolation(filter);
    for (std::uint32_t now = 1000; now <= 2600; now += 200)
        expect(filter, 0, now, 0, "quiet period produced contact");
    expect(filter, 0, 2801, 0, "sample gap produced contact");
    for (std::uint32_t now = 3001; now <= 4601; now += 200)
        expect(filter, 0, now, 0, "post-gap quiet period produced contact");
    expect(filter, 0, 4800, 0, "post-gap quiet period produced contact");
    check(filter.isolated(), "201 ms sampling gap counted toward quiet");
    expect(filter, 0, 4801, 0, "post-gap recovery produced contact");
    check(!filter.isolated(), "continuous quiet did not recover after a sampling gap");
}

void burst_window_has_an_inclusive_4000_ms_boundary()
{
    Filter boundary;
    first_two_contacts(boundary);
    expect(boundary, 16, 4000, 0, "boundary touch skipped confirmation");
    expect(boundary, 16, 4100, 0, "third touch exactly 4000 ms after the first escaped isolation");
    check(boundary.isolated(), "4000 ms burst boundary was excluded");

    Filter expired;
    first_two_contacts(expired);
    expect(expired, 16, 4001, 0, "new burst touch skipped confirmation");
    expect(expired, 16, 4101, 16, "expired burst suppressed a new legitimate first touch");
    check(!expired.isolated() && expired.suppressed_presses() == 0,
          "4001 ms contact incorrectly retained the old burst");
}

void burst_isolation_and_quiet_rearm_survive_uint32_wrap()
{
    Filter burst;
    constexpr auto start = std::numeric_limits<std::uint32_t>::max() - 500u;
    enter_isolation(burst, start);
    check(burst.isolated(), "burst crossing uptime wrap escaped isolation");

    Filter quiet;
    constexpr auto before_wrap = std::numeric_limits<std::uint32_t>::max() - 950u;
    enter_isolation(quiet, before_wrap);
    for (std::uint32_t age = 950; age <= 2750; age += 200)
        expect(quiet, 0, before_wrap + age, 0, "quiet period crossing wrap produced contact");
    expect(quiet, 0, before_wrap + 2949u, 0, "wrapped quiet period produced contact");
    check(quiet.isolated(), "wrapped 1999 ms quiet ended isolation early");
    expect(quiet, 0, before_wrap + 2950u, 0, "wrapped recovery produced contact");
    check(!quiet.isolated(), "wrapped 2000 ms quiet did not rearm");
    expect(quiet, 4, before_wrap + 3000u, 0, "wrapped recovered touch skipped confirmation");
    expect(quiet, 4, before_wrap + 3100u, 4, "first legitimate touch after wrapped recovery was lost");
}

} // namespace

int main()
{
    try {
        short_positive_pulses_do_not_accumulate();
        stable_press_and_hold_preserve_one_contact();
        release_noise_retains_contact_until_200_ms();
        a_real_release_allows_a_second_touch();
        channel_changes_pass_through_without_false_release();
        invalid_reads_break_confirmation_without_releasing_contact();
        gaps_above_200_ms_restart_confirmation();
        reserved_bits_never_create_contact();
        uint32_wrap_preserves_press_and_release_boundaries();
        third_contact_isolated_without_repeated_suppression_counts();
        one_held_contact_never_counts_as_a_burst();
        quiet_rearms_fresh_contacts_and_resets_the_burst();
        bouncing_input_restarts_quiet_confirmation();
        invalid_reads_cannot_count_as_quiet();
        long_sample_gaps_cannot_count_as_quiet();
        burst_window_has_an_inclusive_4000_ms_boundary();
        burst_isolation_and_quiet_rearm_survive_uint32_wrap();
        std::cout << "head_touch_filter_test: 17 scenarios, " << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "head_touch_filter_test: " << error.what() << '\n';
        return 1;
    }
}
