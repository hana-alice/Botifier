#include <apps/app_distance/edge_swipe.h>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using distance_ui::EdgeSwipe;
using distance_ui::SwipeEdge;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

SwipeEdge gesture(int x0, int y0, int x1, int y1, std::uint32_t duration = 200u)
{
    EdgeSwipe swipe;
    swipe.press(x0, y0, 0);
    return swipe.release(x1, y1, duration);
}

void exact_display_edges_are_valid()
{
    for (const auto x : {0, 160, 319}) {
        for (const auto y : {0, 27})
            check(gesture(x, y, x, y + 42) == SwipeEdge::Top,
                  "valid upper edge or exact 42-pixel downward swipe was rejected");
        for (const auto y : {212, 239})
            check(gesture(x, y, x, y - 42) == SwipeEdge::Bottom,
                  "valid lower edge or exact 42-pixel upward swipe was rejected");
    }
    check(gesture(0, 0, 0, 239) == SwipeEdge::Top, "full-height downward swipe was rejected");
    check(gesture(319, 239, 319, 0) == SwipeEdge::Bottom, "full-height upward swipe was rejected");
}

void central_origins_and_short_or_wrong_direction_swipes_are_rejected()
{
    for (const auto y : {28, 100, 120, 211}) {
        check(gesture(160, y, 160, 239) == SwipeEdge::None, "central origin enabled a downward gesture");
        check(gesture(160, y, 160, 0) == SwipeEdge::None, "central origin enabled an upward gesture");
    }
    check(gesture(160, 27, 160, 68) == SwipeEdge::None, "41-pixel top swipe passed the 42-pixel boundary");
    check(gesture(160, 212, 160, 171) == SwipeEdge::None, "41-pixel bottom swipe passed the 42-pixel boundary");
    check(gesture(160, 27, 160, 0) == SwipeEdge::None, "outward top movement triggered the menu");
    check(gesture(160, 212, 160, 239) == SwipeEdge::None, "outward bottom movement triggered the menu");
    check(gesture(160, 0, 160, 0) == SwipeEdge::None, "stationary tap triggered the menu");
}

void horizontal_and_diagonal_motion_have_a_strict_margin()
{
    for (const auto sign : {-1, 1}) {
        check(gesture(160, 0, 160 + sign * 100, 0) == SwipeEdge::None, "horizontal upper-edge motion triggered");
        check(gesture(160, 239, 160 + sign * 100, 239) == SwipeEdge::None, "horizontal lower-edge motion triggered");
        check(gesture(160, 0, 160 + sign * 42, 42) == SwipeEdge::None, "45-degree diagonal triggered");
        check(gesture(160, 239, 160 + sign * 42, 197) == SwipeEdge::None, "reverse diagonal triggered");
        check(gesture(160, 0, 160 + sign * 34, 42) == SwipeEdge::None, "equal vertical/horizontal-plus-eight margin triggered");
        check(gesture(160, 239, 160 + sign * 34, 197) == SwipeEdge::None, "equal bottom margin triggered");
        check(gesture(160, 0, 160 + sign * 33, 42) == SwipeEdge::Top, "strictly dominant downward swipe was rejected");
        check(gesture(160, 239, 160 + sign * 33, 197) == SwipeEdge::Bottom, "strictly dominant upward swipe was rejected");
    }
}

void coordinates_outside_the_display_fail_closed()
{
    constexpr std::array<std::array<int, 2>, 10> outside{{
        {{-1, 0}}, {{320, 0}}, {{0, -1}}, {{0, 240}}, {{-1, 239}}, {{320, 239}},
        {{std::numeric_limits<int>::min(), 0}}, {{std::numeric_limits<int>::max(), 0}},
        {{0, std::numeric_limits<int>::min()}}, {{0, std::numeric_limits<int>::max()}},
    }};
    for (const auto point : outside) {
        check(gesture(point[0], point[1], 160, 120) == SwipeEdge::None,
              "outside origin triggered a gesture or overflowed geometry");
        check(gesture(160, 0, point[0], point[1]) == SwipeEdge::None,
              "outside endpoint triggered a top gesture or overflowed geometry");
        check(gesture(160, 239, point[0], point[1]) == SwipeEdge::None,
              "outside endpoint triggered a bottom gesture or overflowed geometry");
    }
}

void slow_swipes_work_but_long_presses_do_not()
{
    for (const auto duration : {0u, 200u, 1000u, 3999u, 4000u}) {
        check(gesture(160, 0, 160, 80, duration) == SwipeEdge::Top, "valid slow top swipe was rejected");
        check(gesture(160, 239, 160, 159, duration) == SwipeEdge::Bottom, "valid slow bottom swipe was rejected");
    }
    for (const auto duration : {4001u, 10000u, std::numeric_limits<std::uint32_t>::max()}) {
        check(gesture(160, 0, 160, 80, duration) == SwipeEdge::None, "long or backdated top press triggered");
        check(gesture(160, 239, 160, 159, duration) == SwipeEdge::None, "long or backdated bottom press triggered");
    }
}

void each_release_consumes_exactly_one_real_press()
{
    EdgeSwipe swipe;
    check(swipe.release(160, 80, 100) == SwipeEdge::None, "release without press triggered");
    swipe.press(160, 0, 100);
    check(swipe.release(160, 80, 200) == SwipeEdge::Top, "first real press/release failed");
    check(swipe.release(160, 100, 201) == SwipeEdge::None, "duplicate release replayed a valid gesture");
    swipe.press(160, 239, 300);
    check(swipe.release(160, 220, 400) == SwipeEdge::None, "short bottom gesture unexpectedly succeeded");
    check(swipe.release(160, 100, 401) == SwipeEdge::None, "second release revived a rejected short gesture");
    swipe.press(160, 239, 500);
    check(swipe.release(-1, 100, 600) == SwipeEdge::None, "invalid release triggered");
    check(swipe.release(160, 100, 601) == SwipeEdge::None, "valid later release revived an outside endpoint");
    swipe.press(160, 239, 700);
    check(swipe.release(160, 159, 800) == SwipeEdge::Bottom, "fresh press did not rearm after rejected gestures");
}

void duplicate_press_cancels_until_release()
{
    EdgeSwipe swipe;
    swipe.press(160, 0, 0);
    swipe.press(160, 0, 100);
    check(swipe.release(160, 80, 200) == SwipeEdge::None, "duplicate top press restarted a live gesture");
    swipe.press(160, 239, 300);
    swipe.press(160, 0, 400);
    swipe.press(160, 239, 500);
    check(swipe.release(160, 159, 600) == SwipeEdge::None, "repeated presses replaced the cancelled origin");
    swipe.press(-1, 0, 700);
    swipe.press(160, 0, 800);
    check(swipe.release(160, 80, 900) == SwipeEdge::None, "duplicate press repaired an invalid initial origin");
    swipe.press(160, 0, 1000);
    check(swipe.release(160, 80, 1100) == SwipeEdge::Top, "new gesture did not rearm after duplicate-press cancellation");
}

void reset_discards_view_lifecycle_state()
{
    EdgeSwipe swipe;
    swipe.reset();
    check(swipe.release(160, 100, 0) == SwipeEdge::None, "reset constructed a gesture");
    swipe.press(160, 0, 100);
    swipe.reset();
    swipe.reset();
    check(swipe.release(160, 80, 200) == SwipeEdge::None, "reset preserved a previous view's held press");
    swipe.press(160, 239, 300);
    check(swipe.release(160, 159, 400) == SwipeEdge::Bottom, "reset prevented a later valid gesture");
}

void elapsed_time_survives_uint32_wrap_and_rejects_backdating()
{
    const auto start = std::numeric_limits<std::uint32_t>::max() - 100u;
    EdgeSwipe swipe;
    swipe.press(160, 0, start);
    check(swipe.release(160, 80, start + 200u) == SwipeEdge::Top, "uint32-wrap swipe was rejected");
    swipe.press(160, 239, start);
    check(swipe.release(160, 159, start + 4000u) == SwipeEdge::Bottom, "wrapped exact four-second swipe was rejected");
    swipe.press(160, 0, start);
    check(swipe.release(160, 80, start + 4001u) == SwipeEdge::None, "wrapped long press triggered");
    swipe.press(160, 239, 1000);
    check(swipe.release(160, 159, 999) == SwipeEdge::None, "backdated release triggered");
    check(swipe.release(160, 159, 1100) == SwipeEdge::None, "later release revived a backdated gesture");
}

} // namespace

int main()
{
    const std::array tests{
        exact_display_edges_are_valid,
        central_origins_and_short_or_wrong_direction_swipes_are_rejected,
        horizontal_and_diagonal_motion_have_a_strict_margin,
        coordinates_outside_the_display_fail_closed,
        slow_swipes_work_but_long_presses_do_not,
        each_release_consumes_exactly_one_real_press,
        duplicate_press_cancels_until_release,
        reset_discards_view_lifecycle_state,
        elapsed_time_survives_uint32_wrap_and_rejects_backdating,
    };
    try { for (const auto test : tests) test(); }
    catch (const std::exception& error) { std::cerr << "edge_swipe_test: " << error.what() << '\n'; return 1; }
    std::cout << "edge_swipe_test: " << tests.size() << " geometry and lifecycle groups passed\n";
    return 0;
}
