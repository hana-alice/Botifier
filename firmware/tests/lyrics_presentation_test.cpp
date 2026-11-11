#include <apps/common/lyrics/presentation.h>

#include <iostream>
#include <stdexcept>

namespace {
using namespace lyrics;
namespace display = lyrics::presentation;

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int columns(std::string_view text)
{
    int width = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto character = display::character_at(text, offset);
        check(character.valid, "fitter emitted invalid UTF-8");
        width += character.codepoint >= 0x2E80 ? 2 : 1;
        offset += character.bytes;
    }
    return width;
}

void fits(std::string_view text, int width, std::size_t max_lines)
{
    std::size_t count = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        check(columns(text.substr(start, end == std::string_view::npos ? end : end - start)) <= width,
              "fitted row overflowed its measured width");
        ++count;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    check(count <= max_lines, "fitter emitted too many rows");
}

void exact_timestamps_intro_seek_and_translation()
{
    const std::vector<Line> lines{{1000, "First original line"}, {1000, "第一句"},
        {2500, "Second original line"}, {4000, ""}, {6000, "Last original line"}};
    auto cue = display::select_cue(lines, 999);
    check(cue.current.empty() && cue.previous.empty() && cue.next.begin == 0 && cue.next.end == 2,
          "intro displayed the future first lyric");
    cue = display::select_cue(lines, 1000);
    check(cue.current.begin == 0 && cue.current.end == 2 && cue.next.begin == 2,
          "equal timestamp translation was not grouped");
    check(display::group_text(lines, cue.current) == "First original line\n第一句", "translation content was lost");
    cue = display::select_cue(lines, 3999);
    check(cue.current.begin == 2 && cue.previous.begin == 0 && cue.previous.end == 2 && cue.next.begin == 3,
          "selection crossed the next line timestamp");
    cue = display::select_cue(lines, 4000);
    check(display::group_text(lines, cue.current).empty(), "blank instrumental cue inherited old lyric");
    cue = display::select_cue(lines, 9000);
    check(cue.current.begin == 4 && cue.next.empty(), "last cue did not remain selected");
    cue = display::select_cue(lines, 1500);
    check(cue.current.begin == 0 && cue.current.end == 2, "backwards seek kept stale lyric");
    check(display::select_cue({}, 1000).current.empty(), "empty lyrics selected a cue");
}

void surface_requires_complete_track_and_yields_on_disconnect()
{
    Snapshot value;
    value.enabled = value.connected = value.playing = value.track_ready = true;
    value.link = LinkState::Ready;
    value.track.title = "An original song";
    value.state = LyricState::Loading;
    check(display::wants_surface(value, 100000), "playing/loading track failed to own the lyrics surface");
    value.playing = false;
    value.paused_since_ms = 100000;
    check(display::wants_surface(value, 114999), "paused lyrics disappeared before the hold deadline");
    check(display::wants_surface(value, 115000) && display::wants_surface(value, 900000),
          "paused current lyrics unexpectedly yielded to the idle carousel");
    check(display::wants_surface(value, 99999), "wall-clock changes hid complete paused track information");
    value.connected = false;
    check(!display::wants_surface(value, 100001), "disconnect retained the lyrics surface");
    value.connected = true;
    value.track_ready = false;
    check(!display::wants_surface(value, 100001), "incomplete metadata took over the screen");
    value.track_ready = true;
    value.link = LinkState::Discovering;
    check(!display::wants_surface(value, 100001), "discovery displayed stale track data");
    value.link = LinkState::Ready;
    value.enabled = false;
    check(!display::wants_surface(value, 100001), "disabled music retained the screen");
}

void bounded_motion_curves()
{
    auto previous=display::cue_entrance(0);
    check(previous.offset_y==8&&previous.opacity==96,"cue entrance must begin visibly below its resting position");
    for(unsigned elapsed=1;elapsed<=1000;++elapsed) {
        const auto frame=display::cue_entrance(elapsed);
        check(frame.offset_y>=0&&frame.offset_y<=previous.offset_y&&frame.opacity>=previous.opacity,
              "cue entrance must move upward and fade in without overshoot");
        previous=frame;
    }
    check(previous.offset_y==0&&previous.opacity==255,"cue entrance must settle fully visible");
    check(display::smooth_position(1000,1120,0)==1000&&display::smooth_position(1000,1120,60)==1060&&
          display::smooth_position(1000,1120,120)==1120&&display::smooth_position(1000,1120,UINT32_MAX)==1120,
          "progress interpolation must stay between received positions and clamp to its endpoint");
    check(display::smooth_position(2000,1000,60)==1500,"backward position interpolation must not overflow unsigned arithmetic");
}

void bounded_wrapping_preserves_utf8_words_and_translation()
{
    check(display::fit_text("hello world again", 11, 3, columns) == "hello world\nagain", "English word wrapping split a word");
    check(display::fit_text("一二三四五六七八九十", 8, 2, columns) == "一二三四\n五六...",
          "Chinese clipping broke a character or hid truncation");
    check(display::fit_text("first\n第二句", 12, 3, columns) == "first\n第二句", "translation newline was lost");
    check(display::fit_text("     hello world     ", 8, 2, columns) == "hello\nworld...",
          "space wrapping or final truncation was ambiguous");
    check(display::fit_text("abcdefghijklmnopqrstuv", 7, 2, columns) == "abcdefg\nhijk...",
          "long unbroken text exceeded the row limit");
    std::string large;
    for (int i = 0; i < 500; ++i) large += "原文";
    const auto bounded = display::safe_text(large);
    check(bounded.size() <= display::kTextLimit && bounded.substr(bounded.size() - 3) == "...", "input byte cap lost truncation marker");
    for (const auto& input : {large, std::string("an original English phrase with some spaces"), std::string("第一句\nAn original translation")}) {
        for (int width = 4; width <= 40; ++width) {
            for (std::size_t rows = 1; rows <= 3; ++rows) fits(display::fit_text(input, width, rows, columns), width, rows);
        }
    }
}

void malformed_bytes_and_controls_never_reach_labels()
{
    constexpr char malformed[] = "a\0b\x1b" "c\r\td\xF0\x9F";
    const std::string bytes{malformed, sizeof(malformed) - 1};
    const auto clean = display::safe_text(bytes);
    check(clean.find('\0') == std::string::npos && clean.find('\x1b') == std::string::npos &&
          clean.find('\r') == std::string::npos && clean.find('\t') == std::string::npos,
          "control bytes reached the display");
    columns(clean);
    check(!display::character_at("\xC0\xAF", 0).valid && !display::character_at("\xED\xA0\x80", 0).valid &&
          !display::character_at("\xF4\x90\x80\x80", 0).valid, "invalid Unicode accepted");
    check(display::safe_text("一二三", 7) == "一...", "byte clipping split UTF-8");
    std::vector<Line> translations;
    for (int i = 0; i < 100; ++i) translations.push_back({0, std::string(100, 'x')});
    const auto group = display::group_text(translations, {0, translations.size()});
    check(group.size() <= display::kTextLimit, "duplicate timestamp group bypassed text limit");
}

} // namespace

int main()
{
    try {
        exact_timestamps_intro_seek_and_translation();
        surface_requires_complete_track_and_yields_on_disconnect();
        bounded_wrapping_preserves_utf8_words_and_translation();
        malformed_bytes_and_controls_never_reach_labels();
        bounded_motion_curves();
        std::cout << "lyrics presentation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
