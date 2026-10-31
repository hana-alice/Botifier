#include <apps/app_distance/nfc_gate.h>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using nfc_sense::CardGate;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void remove(CardGate& gate, std::uint32_t start)
{
    check(!gate.observe(start, false), "empty observation emitted an event");
    check(!gate.observe(start + 300u, false), "completed removal emitted an event");
    check(!gate.waiting_for_removal(), "two valid empty reads spanning 300 ms did not rearm");
}

void initial_card_is_accepted_once()
{
    CardGate gate;
    check(!gate.waiting_for_removal(), "initial idle should accept a first presentation");
    check(gate.observe(0, true), "first valid card at timestamp zero was not accepted");
    check(gate.waiting_for_removal(), "accepted presentation did not consume the gate");
    check(!gate.observe(150, true), "held card emitted a second event");
    gate.reset();
    check(!gate.observe(0, false) && !gate.observe(150, false), "initial idle empty reads emitted an event");
    check(gate.observe(300, true), "initial empty reads prevented a first presentation");
}

void sustained_presence_and_uid_changes_do_not_rearm()
{
    CardGate gate;
    unsigned accepted = gate.observe(0, true) ? 1u : 0u;
    // A different UID is deliberately still `present`: replacement without an
    // observed empty field cannot manufacture another presentation.
    for (std::uint32_t now = 150; now <= 15000; now += 150) {
        if (gate.observe(now, true)) ++accepted;
        check(gate.waiting_for_removal(), "held presence stopped waiting for removal");
    }
    check(accepted == 1, "continuous presence produced multiple accepted presentations");
}

void removal_requires_two_reads_and_full_interval()
{
    CardGate gate;
    gate.observe(0, true);
    gate.observe(150, false);
    check(gate.waiting_for_removal(), "one empty read rearmed the gate");
    gate.observe(449, false);
    check(gate.waiting_for_removal(), "299 ms absence rearmed early");
    gate.observe(450, false);
    check(!gate.waiting_for_removal(), "300 ms absence did not rearm at the boundary");
    check(gate.observe(600, true), "new presentation after removal was rejected");

    gate.reset();
    gate.observe(0, true);
    remove(gate, 150);
    check(gate.observe(451, true), "exactly two empty reads spanning 300 ms did not suffice");
}

void presence_interrupts_removal()
{
    CardGate gate;
    gate.observe(0, true);
    gate.observe(150, false);
    check(!gate.observe(400, true), "brief removal and renewed presence triggered another event");
    gate.observe(450, false);
    gate.observe(749, false);
    check(gate.waiting_for_removal(), "removal carried time across intervening presence");
    gate.observe(750, false);
    check(!gate.waiting_for_removal(), "fresh removal interval did not complete");
}

void read_errors_never_count_as_removal()
{
    CardGate gate;
    check(!gate.observe(0, false, false), "initial error emitted an event");
    check(gate.waiting_for_removal() && !gate.observe(150, true), "error left an immediately usable permission");
    gate.observe(300, false);
    gate.observe(450, false, false);
    gate.observe(600, false);
    gate.observe(899, false);
    check(gate.waiting_for_removal(), "read error contributed to the clear interval");
    gate.observe(900, false);
    check(!gate.waiting_for_removal() && gate.observe(1050, true), "valid removal did not recover from read error");
    for (std::uint32_t now = 1200; now <= 3000; now += 150) {
        check(!gate.observe(now, false, false), "repeated error emitted an event");
        check(gate.waiting_for_removal(), "repeated errors rearmed a consumed presentation");
    }
}

void playback_absence_cannot_prearm_resume()
{
    CardGate gate;
    check(gate.observe(0, true), "first card was not accepted");
    gate.block();
    for (std::uint32_t now = 150; now <= 1500; now += 150) {
        check(!gate.observe(now, false, true, true), "blocked empty field emitted an event");
        check(gate.waiting_for_removal(), "absence during playback prearmed resume");
    }
    check(!gate.observe(1650, true), "card present on resume replayed a blocked presentation");
    remove(gate, 1800);
    check(gate.observe(2250, true), "new post-playback presentation was rejected");
}

void blocked_card_is_consumed_without_a_queue()
{
    CardGate gate;
    // `blocked` also represents invalid/stale/alerting distance observations.
    check(!gate.observe(0, true, true, true), "distance-blocked card emitted an event");
    check(!gate.observe(150, true), "unblocking replayed the still-present card");
    check(!gate.observe(300, false, true, true), "blocked empty field emitted an event");
    gate.observe(450, false);
    gate.observe(749, false);
    check(gate.waiting_for_removal(), "absence before unblock was included after unblock");
    gate.observe(750, false);
    check(gate.observe(900, true), "fresh absence after unblock failed to permit another presentation");
}

void explicit_block_cancels_partial_removal_and_idle_permission()
{
    CardGate gate;
    gate.block();
    check(!gate.observe(0, true), "stop or disable did not consume an idle permission");
    gate.observe(150, false);
    gate.block();
    gate.observe(400, false);
    gate.observe(699, false);
    check(gate.waiting_for_removal(), "block retained partial removal evidence");
    gate.observe(700, false);
    check(!gate.waiting_for_removal(), "new removal interval after block did not complete");
}

void late_and_duplicate_observations_fail_closed()
{
    CardGate gate;
    gate.observe(1000, true);
    gate.observe(1150, false);
    check(!gate.observe(1150, false), "duplicate empty read emitted an event");
    gate.observe(1450, false);
    check(gate.waiting_for_removal(), "duplicate frame retained the old removal start");
    gate.observe(1749, false);
    check(gate.waiting_for_removal(), "recovery removal ended before 300 ms");
    gate.observe(1750, false);
    check(!gate.waiting_for_removal(), "valid interval after duplicate did not recover");
    check(!gate.observe(1749, true) && gate.waiting_for_removal(), "backdated card used an idle permission");
    check(!gate.observe(1748, false), "backdated empty read emitted an event");
    gate.observe(1900, false);
    gate.observe(2199, false);
    check(gate.waiting_for_removal(), "backdated input rewound the observation high-water mark");
    gate.observe(2200, false);
    check(gate.observe(2350, true), "chronological clear interval after late input did not recover");
}

void uint32_wrap_preserves_clear_interval()
{
    CardGate gate;
    constexpr auto start = std::numeric_limits<std::uint32_t>::max() - 149u;
    check(gate.observe(start, true), "first card before wrap was rejected");
    gate.observe(start + 150u, false);
    gate.observe(start + 449u, false);
    check(gate.waiting_for_removal(), "wrapped 299 ms removal ended early");
    gate.observe(start + 450u, false);
    check(!gate.waiting_for_removal() && gate.observe(start + 600u, true),
          "timestamp zero or wrapped removal prevented a new presentation");
    check(!gate.observe(start + 599u, false) && gate.waiting_for_removal(), "late observation after wrap bypassed chronology");
}

void repeated_lifetime_presentations_stay_one_shot()
{
    CardGate gate;
    unsigned accepted = 0;
    std::uint32_t now = 0;
    for (unsigned presentation = 0; presentation < 1000; ++presentation) {
        if (gate.observe(now, true)) ++accepted;
        check(!gate.observe(now + 150u, true), "held read emitted another lifetime event");
        remove(gate, now + 300u);
        now += 750u;
    }
    check(accepted == 1000, "presentation lifetime lost or duplicated an event");
    std::cout << "synthetic NFC lifetime: presentations=1000, accepted=" << accepted << '\n';
}

}  // namespace

int main()
{
    const std::array tests{
        initial_card_is_accepted_once,
        sustained_presence_and_uid_changes_do_not_rearm,
        removal_requires_two_reads_and_full_interval,
        presence_interrupts_removal,
        read_errors_never_count_as_removal,
        playback_absence_cannot_prearm_resume,
        blocked_card_is_consumed_without_a_queue,
        explicit_block_cancels_partial_removal_and_idle_permission,
        late_and_duplicate_observations_fail_closed,
        uint32_wrap_preserves_clear_interval,
        repeated_lifetime_presentations_stay_one_shot,
    };
    try {
        for (const auto test : tests) test();
    } catch (const std::exception& error) {
        std::cerr << "nfc_gate_test: " << error.what() << '\n';
        return 1;
    }
    std::cout << "nfc_gate_test: " << tests.size() << " groups passed (synthetic card presence only)\n";
    return 0;
}
