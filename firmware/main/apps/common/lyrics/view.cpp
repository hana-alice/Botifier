#include "view.h"
#include "presentation.h"
#include "protocol.h"
#include "font_cjk_20.h"

#include <lvgl.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <src/misc/lv_text_private.h>
#include <algorithm>
#include <atomic>
#include <memory>

namespace lyrics {
namespace {

constexpr std::uint32_t kBackground = 0x000000;
constexpr std::uint32_t kText = 0xF4F4F4;
constexpr std::uint32_t kContext = 0x808080;
constexpr int kWidth = 288;
constexpr int kCueTop = 64;
constexpr int kCueHeight = 112;
constexpr int kLineSpacing = 2;

struct Counters {
    std::atomic<bool> active{false};
    std::atomic<std::uint32_t> started{0}, cycles{0}, render_ms{0}, max_render_ms{0};
    std::atomic<std::uint32_t> pixels{0}, glyphs{0}, cues{0}, failures{0};
} counters;

// Scale retained grayscale coverage, then cache the whole current cue once.
struct FontScale { int pixels; bool bold; };
constexpr FontScale kLargeScale{36,false}, kMediumScale{32,false}, kCompactScale{26,false}, kPreviewScale{18,false};

int scaled(int value, int pixels) { return (value * pixels + (value >= 0 ? 10 : -10)) / 20; }

bool scaled_descriptor(const lv_font_t* font, lv_font_glyph_dsc_t* glyph, std::uint32_t letter, std::uint32_t next)
{
    if (!lv_font_get_glyph_dsc_fmt_txt(&botifier_cjk_20, glyph, letter, next)) return false;
    const auto& scale = *static_cast<const FontScale*>(font->user_data);
    glyph->adv_w = scaled(glyph->adv_w,scale.pixels) + scale.bold;
    glyph->box_w = scaled(glyph->box_w,scale.pixels) + (glyph->box_w && scale.bold);
    glyph->box_h = scaled(glyph->box_h,scale.pixels);
    glyph->ofs_x = scaled(glyph->ofs_x,scale.pixels);
    glyph->ofs_y = scaled(glyph->ofs_y,scale.pixels);
    glyph->format = LV_FONT_GLYPH_FORMAT_A8;
    glyph->stride = 0;
    return true;
}

const void* scaled_bitmap(lv_font_glyph_dsc_t* glyph, lv_draw_buf_t* buffer)
{
    if (!buffer || !buffer->data || !glyph->box_w || !glyph->box_h) return nullptr;
    const auto& scale = *static_cast<const FontScale*>(glyph->resolved_font->user_data);
    const auto* source = static_cast<const lv_font_fmt_txt_dsc_t*>(botifier_cjk_20.dsc);
    const auto stride = buffer->header.stride;
    if (source->bpp!=2 || buffer->header.cf!=LV_COLOR_FORMAT_A8 ||
        buffer->header.w<glyph->box_w || buffer->header.h<glyph->box_h || stride<glyph->box_w ||
        static_cast<std::uint64_t>(glyph->box_h-1)*stride+glyph->box_w>buffer->data_size) return nullptr;
    const auto& original = source->glyph_dsc[glyph->gid.index];
    const auto* bitmap = botifier_cjk_20_bitmap(glyph->gid.index);
    if (!bitmap) return nullptr;
    counters.glyphs.fetch_add(1, std::memory_order_relaxed);
    const auto bit = [&](int x,int y) -> unsigned {
        if (x<0 || y<0 || x>=original.box_w || y>=original.box_h) return 0;
        const auto index = static_cast<unsigned>(y)*original.box_w+x;
        return ((bitmap[index/4] >> (6u-2u*(index%4))) & 3u) * 85u;
    };
    const auto sample = [&](int x,int y) -> unsigned {
        const int fx = ((2*x+1)*20*128)/scale.pixels-128;
        const int fy = ((2*y+1)*20*128)/scale.pixels-128;
        const int sx = fx>=0 ? fx/256 : -1, sy = fy>=0 ? fy/256 : -1;
        const auto ax = static_cast<unsigned>(fx-sx*256), ay = static_cast<unsigned>(fy-sy*256);
        return (bit(sx,sy)*(256-ax)*(256-ay)+bit(sx+1,sy)*ax*(256-ay)+
                bit(sx,sy+1)*(256-ax)*ay+bit(sx+1,sy+1)*ax*ay)/65536;
    };
    for (unsigned y=0;y<glyph->box_h;++y) for(unsigned x=0;x<glyph->box_w;++x) {
        auto alpha = sample(x,y);
        if (scale.bold && x) alpha = std::max(alpha,sample(x-1,y));
        buffer->data[y*stride+x] = static_cast<std::uint8_t>(alpha);
    }
    lv_draw_buf_flush_cache(buffer,nullptr);
    return buffer;
}

lv_font_t scaled_font(const FontScale& scale)
{
    auto font = botifier_cjk_20;
    font.get_glyph_dsc = scaled_descriptor;
    font.get_glyph_bitmap = scaled_bitmap;
    font.release_glyph = nullptr;
    font.line_height = scaled(font.line_height,scale.pixels);
    font.base_line = scaled(font.base_line,scale.pixels);
    font.static_bitmap = 0;
    font.user_data = const_cast<FontScale*>(&scale);
    return font;
}

const lv_font_t kLargeFont=scaled_font(kLargeScale), kMediumFont=scaled_font(kMediumScale),
    kCompactFont=scaled_font(kCompactScale), kPreviewFont=scaled_font(kPreviewScale);

lv_obj_t* plain(lv_obj_t* parent, int width, int height)
{
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return object;
}

lv_obj_t* label(lv_obj_t* parent, int x, int y, int width, int height,
                const lv_font_t* font, std::uint32_t color)
{
    auto* object = lv_label_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(object, kLineSpacing, 0);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
    return object;
}

int text_width(std::string_view text,const lv_font_t* font)
{
    // fit_text hands over a temporary null-terminated std::string.
    lv_point_t size{};
    lv_text_get_size(&size, text.data(), font, 0, kLineSpacing,
                     LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    return size.x;
}

std::string supported_text(std::string_view text, const lv_font_t* font, std::size_t limit)
{
    const auto clean=presentation::safe_text(text,limit);
    std::string supported;supported.reserve(clean.size());
    for(std::size_t i=0;i<clean.size();) {
        const auto character=presentation::character_at(clean,i);
        const auto simplified=protocol::simplify_display_character(character.codepoint);
        lv_font_glyph_dsc_t glyph{};
        if(simplified!=character.codepoint&&lv_font_get_glyph_dsc(font,&glyph,simplified,0)) {
            // The shared table maps BMP Han to BMP Han (three UTF-8 bytes).
            supported.push_back(static_cast<char>(0xe0|(simplified>>12)));
            supported.push_back(static_cast<char>(0x80|((simplified>>6)&63)));
            supported.push_back(static_cast<char>(0x80|(simplified&63)));
        } else if(character.codepoint=='\n'||lv_font_get_glyph_dsc(font,&glyph,character.codepoint,0))
            supported.append(clean,i,character.bytes);
        else supported.push_back('?');
        i+=character.bytes;
    }
    return supported;
}

std::string fit(std::string_view text, std::size_t rows = 1, const lv_font_t* font=&kPreviewFont,int width=kWidth)
{
    return presentation::fit_text(supported_text(text,font,presentation::kTextLimit),width,rows,
        [font](std::string_view value){return text_width(value,font);});
}

presentation::Group text_group(const std::vector<Line>& lines, std::size_t index, bool forward)
{
    while(index<lines.size()) {
        const auto group=presentation::group_at(lines,index);
        for(auto i=group.begin;i<group.end;++i) if(!lines[i].text.empty()) return group;
        if(!forward&&group.begin==0) break;
        index=forward?group.end:group.begin-1;
    }
    return {};
}

const char* status_title(LyricState state)
{
    switch (state) {
    case LyricState::Loading: return "FINDING LYRICS";
    case LyricState::NotFound: return "NO SYNCED LYRICS";
    case LyricState::Instrumental: return "INSTRUMENTAL";
    case LyricState::NetworkError: return "LYRICS OFFLINE";
    case LyricState::Invalid: return "NO SYNCED LYRICS";
    default: return "WAITING FOR LYRICS";
    }
}

// A foreground interruption destroys the LVGL view. Keep only its browsing
// bookmark; a weak identity cannot keep old song data alive or alias a new song.
struct Bookmark {
    std::weak_ptr<const std::vector<Line>> lines;
    std::uint32_t generation=0, remaining_ms=0;
    std::size_t group=0, offset=0;
} bookmark;

struct View {
    lv_obj_t* root = nullptr;
    lv_obj_t* previous = nullptr;
    lv_obj_t* current = nullptr;
    lv_obj_t* current_image = nullptr;
    lv_draw_buf_t* cue_raster = nullptr;
    bool cached_cue = false;
    lv_obj_t* next = nullptr;
    lv_obj_t* timing = nullptr;
    std::shared_ptr<const std::vector<Line>> manual_lines;
    presentation::Group manual_group;
    std::string manual_text;
    std::size_t manual_offset = 0, manual_end = 0, previous_manual_offset = 0;
    std::uint32_t manual_remaining_ms = 0;
    lv_timer_t* timer = nullptr;
    bool rendered = false;
    bool previous_playing = false;
    LyricState previous_state = LyricState::Waiting;
    std::uint32_t previous_generation = 0;
    std::size_t previous_cue = 0;
    const void* previous_lines = nullptr;
    std::string previous_title;
    std::string previous_artist;
    std::uint32_t last_position = 0, transition_at = 0;
    bool transitioning = false;
    bool frame_drawn = false, timer_running = false;
    std::uint32_t last_frame_at = 0;
    presentation::CueEntrance previous_entrance{};
    lv_display_t* display = nullptr;
    std::uint32_t render_started = 0;

    std::size_t page_end(std::size_t offset, unsigned rows=2) const
    {
        lv_text_attributes_t attributes{0,kLineSpacing,kWidth,LV_TEXT_FLAG_NONE};
        for(unsigned row=0;row<rows&&offset<manual_text.size();++row)
            offset+=lv_text_get_next_line(manual_text.c_str()+offset,manual_text.size()-offset,
                &kLargeFont,nullptr,&attributes);
        return offset;
    }

    void select_group(presentation::Group group)
    {
        manual_group=group;manual_offset=0;manual_text.clear();
        for(auto i=group.begin;i<group.end;++i) {
            const auto& text=(*manual_lines)[i].text;
            if(text.empty()) continue;
            if(!manual_text.empty()) manual_text+='\n';
            manual_text+=supported_text(text,&kLargeFont,text.size());
        }
    }

    void turn_page(bool forward)
    {
        if(!manual_lines) return;
        manual_remaining_ms=6000;
        if(manual_group.empty()) {select_group(text_group(*manual_lines,0,true));return;}
        if(forward&&manual_end<manual_text.size()) {manual_offset=manual_end;return;}
        if(forward||manual_offset==0) {
            const auto group=text_group(*manual_lines,forward?manual_group.end:
                manual_group.begin?manual_group.begin-1:manual_lines->size(),forward);
            if(group.empty()) return;
            select_group(group);
            if(forward) return;
            manual_offset=manual_text.size();
        }
        std::size_t offset=0,next=0;
        while((next=page_end(offset))<manual_offset) offset=next;
        manual_offset=offset;
    }

    static void display_event(lv_event_t* event)
    {
        auto* self = static_cast<View*>(lv_event_get_user_data(event));
        switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START: self->render_started = lv_tick_get(); break;
        case LV_EVENT_RENDER_READY: {
            const auto elapsed = lv_tick_get() - self->render_started;
            counters.cycles.fetch_add(1, std::memory_order_relaxed);
            counters.render_ms.fetch_add(elapsed, std::memory_order_relaxed);
            if (elapsed > counters.max_render_ms.load(std::memory_order_relaxed))
                counters.max_render_ms.store(elapsed, std::memory_order_relaxed);
            break;
        }
        case LV_EVENT_FLUSH_START: {
            const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
            if (area) counters.pixels.fetch_add(lv_area_get_width(area) * lv_area_get_height(area), std::memory_order_relaxed);
            break;
        }
        default: break;
        }
    }

    void cache_current()
    {
        lv_obj_remove_flag(current, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_opa(current, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(current, lv_color_white(), 0);
        lv_obj_set_style_translate_y(current, 0, 0);
        lv_obj_update_layout(current);
        const bool has_text = *lv_label_get_text(current) != '\0';
        const int max_padding = kLargeFont.line_height / 4;
        if (has_text && !cue_raster) cue_raster = lv_draw_buf_create(kWidth + 2 * max_padding,
            kCueHeight + 2 * max_padding, LV_COLOR_FORMAT_L8, LV_STRIDE_AUTO);
        lv_image_set_src(current_image, nullptr);
        lv_obj_set_style_translate_y(current_image, 0, 0);
        lv_obj_set_style_image_opa(current_image, LV_OPA_COVER, 0);
        if (cue_raster) lv_image_cache_drop(cue_raster);
        // This LVGL SW backend accepts A8 in snapshot(), but cannot draw to it.
        // White-on-zero L8 produces the same coverage bytes; display them as A8.
        if (cue_raster) cue_raster->header.cf = LV_COLOR_FORMAT_L8;
        cached_cue = has_text && cue_raster && lv_snapshot_take_to_draw_buf(current, LV_COLOR_FORMAT_L8, cue_raster) == LV_RESULT_OK;
        bool coverage = false;
        if (cached_cue) {
            for (unsigned y = 0; y < cue_raster->header.h && !coverage; ++y)
                for (unsigned x = 0; x < cue_raster->header.w; ++x)
                    if (cue_raster->data[y * cue_raster->header.stride + x]) { coverage = true; break; }
            cached_cue = coverage;
        }
        lv_obj_set_style_text_color(current, lv_color_hex(kText), 0);
        if (cached_cue) {
            cue_raster->header.cf = LV_COLOR_FORMAT_A8;
            lv_image_set_src(current_image, cue_raster);
            const int padding_x = (static_cast<int>(cue_raster->header.w) - lv_obj_get_width(current)) / 2;
            const int padding_y = (static_cast<int>(cue_raster->header.h) - lv_obj_get_height(current)) / 2;
            lv_obj_set_pos(current_image, lv_obj_get_x(current) - padding_x, lv_obj_get_y(current) - padding_y);
            lv_obj_remove_flag(current_image, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
            counters.cues.fetch_add(1, std::memory_order_relaxed);
        } else {
            lv_obj_add_flag(current_image, LV_OBJ_FLAG_HIDDEN);
            if (has_text) counters.failures.fetch_add(1, std::memory_order_relaxed);
        }
        previous_entrance = {};
    }

    View()
    {
        root = plain(lv_layer_top(), 320, 240);
        lv_obj_add_event_cb(root,[](lv_event_t* event) {
            auto* input=lv_event_get_indev(event);
            if(!input) return;
            lv_point_t point{};lv_indev_get_point(input,&point);
            static_cast<View*>(lv_event_get_user_data(event))->turn_page(point.x>=160);
        },LV_EVENT_SHORT_CLICKED,this);
        lv_obj_set_style_bg_color(root, lv_color_hex(kBackground), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        previous = label(root, 16, 28, kWidth, 28, &kPreviewFont, kContext);
        current = label(root, 16, kCueTop, kWidth, kCueHeight, &kLargeFont, kText);
        current_image = lv_image_create(root);
        lv_obj_remove_style_all(current_image);
        lv_obj_remove_flag(current_image, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_style_image_recolor(current_image, lv_color_hex(kText), 0);
        lv_obj_set_style_image_recolor_opa(current_image, LV_OPA_COVER, 0);
        lv_obj_add_flag(current_image, LV_OBJ_FLAG_HIDDEN);
        next = label(root, 16, 184, kWidth, 28, &kPreviewFont, kContext);
        timer=lv_timer_create([](lv_timer_t* source) {
            static_cast<View*>(lv_timer_get_user_data(source))->animate(lv_tick_get());
        },33,this);
        if(timer)lv_timer_pause(timer);
        counters.started.store(lv_tick_get(), std::memory_order_relaxed);
        counters.cycles = 0; counters.render_ms = 0; counters.max_render_ms = 0;
        counters.pixels = 0; counters.glyphs = 0; counters.cues = 0; counters.failures = 0;
        display = lv_display_get_default();
        if (display) lv_display_add_event_cb(display, display_event, LV_EVENT_ALL, this);
        counters.active.store(true, std::memory_order_relaxed);
    }

    ~View()
    {
        bookmark={manual_lines,previous_generation,manual_remaining_ms,manual_group.begin,manual_offset};
        counters.active.store(false, std::memory_order_relaxed);
        if (display) lv_display_remove_event_cb_with_user_data(display, display_event, this);
        if (timer) lv_timer_delete(timer);
        lv_image_set_src(current_image, nullptr);
        lv_obj_delete(root);
        if (cue_raster) { lv_image_cache_drop(cue_raster); lv_draw_buf_destroy(cue_raster); }
    }

    void animate(std::uint32_t now,bool force=false)
    {
        if(!force&&frame_drawn&&now-last_frame_at<33)return;
        last_frame_at=now;frame_drawn=true;
        const auto entrance=transitioning?presentation::cue_entrance(now-transition_at):presentation::CueEntrance{};
        auto* visual = cached_cue ? current_image : current;
        if (entrance.offset_y != previous_entrance.offset_y)
            lv_obj_set_style_translate_y(visual, entrance.offset_y, 0);
        if (entrance.opacity != previous_entrance.opacity) {
            if (cached_cue) lv_obj_set_style_image_opa(visual, entrance.opacity, 0);
            else lv_obj_set_style_text_opa(visual, entrance.opacity, 0);
        }
        previous_entrance = entrance;
        if(now-transition_at>=presentation::kCueEntranceMs)transitioning=false;
        if(timer && transitioning != timer_running) {
            if(transitioning) lv_timer_resume(timer); else lv_timer_pause(timer);
            timer_running = transitioning;
        }
    }

    void render(const Snapshot& value)
    {
        const bool playback_changed=!rendered||value.playing!=previous_playing;
        const bool synced = value.state == LyricState::Synced && value.lines && !value.lines->empty();
        const bool manual = value.state == LyricState::Unsynced && value.lines && !value.lines->empty();
        const bool track_changed=!rendered || previous_generation!=value.generation || previous_lines!=value.lines.get() ||
            previous_state!=value.state || (!value.phone_lyrics&&previous_title!=value.track.title) || previous_artist!=value.track.artist;
        if(track_changed) {
            manual_lines=manual?value.lines:nullptr;
            manual_remaining_ms=0;
            if(manual) {
                if(!rendered&&bookmark.remaining_ms&&bookmark.generation==value.generation&&
                    bookmark.lines.lock()==manual_lines&&bookmark.group<manual_lines->size()) {
                    select_group(presentation::group_at(*manual_lines,bookmark.group));
                    manual_offset=std::min(bookmark.offset,manual_text.size());
                    manual_remaining_ms=bookmark.remaining_ms;
                }
                if(!timing) {
                    timing=label(root,16,4,kWidth,20,&lv_font_montserrat_14,kContext);
                    lv_label_set_text(timing,"TIMING?");
                }
                lv_obj_add_flag(root,LV_OBJ_FLAG_CLICKABLE);
            } else {
                if(timing) {lv_obj_delete(timing);timing=nullptr;}
                manual_text.clear();
                lv_obj_remove_flag(root,LV_OBJ_FLAG_CLICKABLE);
            }
        }
        const bool seek=track_changed||value.position_ms<last_position||value.position_ms-last_position>750;
        if(rendered&&manual_remaining_ms) {
            if(seek) manual_remaining_ms=0;
            else if(value.playing&&previous_playing)
                manual_remaining_ms-=std::min(manual_remaining_ms,value.position_ms-last_position);
        }
        auto cue = synced||manual ? presentation::select_cue(*value.lines, value.position_ms) : presentation::Cue{};
        if(manual) {
            if(!manual_remaining_ms&&(track_changed||manual_offset||manual_group.begin!=cue.current.begin||manual_group.end!=cue.current.end))
                select_group(cue.current);
            cue.current=manual_group;
            cue.previous=text_group(*manual_lines,manual_group.begin?manual_group.begin-1:manual_lines->size(),false);
            cue.next=text_group(*manual_lines,manual_group.end,true);
        }
        // Zero means intro/no cue; real group indices use one-based cache keys.
        const auto cue_key = cue.current.empty() ? 0 : cue.current.begin + 1;
        const bool text_changed = !rendered || previous_generation != value.generation ||
            previous_state != value.state || previous_lines != value.lines.get() || previous_cue != cue_key ||
            (!value.phone_lyrics && previous_title != value.track.title) || previous_artist != value.track.artist ||
            (manual&&previous_manual_offset!=manual_offset);
        if (text_changed) {
            std::string main;
            if (synced||manual) {
                main = presentation::group_text(*value.lines, cue.current);
                lv_label_set_text(previous, fit(presentation::group_text(*value.lines, cue.previous)).c_str());
                lv_label_set_text(next, fit(presentation::group_text(*value.lines, cue.next)).c_str());
            } else {
                lv_label_set_text(previous, "");
                main=status_title(value.state);
                lv_label_set_text(next, "");
            }
            const lv_font_t* main_font=&kLargeFont;
            std::string fitted;
            if(manual) {
                manual_end=page_end(manual_offset);
                const auto split=page_end(manual_offset,1);
                fitted=manual_text.substr(manual_offset,split-manual_offset);
                while(!fitted.empty()&&(fitted.back()=='\n'||fitted.back()==' ')) fitted.pop_back();
                if(split<manual_end) fitted+='\n'+manual_text.substr(split,manual_end-split);
                while(!fitted.empty()&&(fitted.back()=='\n'||fitted.back()==' ')) fitted.pop_back();
            } else {
                fitted=fit(main,2,main_font);
                for(const auto* fallback : {&kMediumFont, &kCompactFont}) {
                    if(fitted.size()<3 || fitted.compare(fitted.size()-3,3,"...")!=0) break;
                    main_font=fallback;fitted=fit(main,2,main_font);
                }
            }
            lv_obj_set_style_text_font(current,main_font,0);
            lv_label_set_text(current,fitted.c_str());
            lv_point_t size{};
            lv_text_get_size(&size, lv_label_get_text(current), main_font, 0, kLineSpacing,
                             kWidth, LV_TEXT_FLAG_NONE);
            lv_obj_set_height(current,size.y);
            lv_obj_set_y(current,kCueTop+std::max<int>(0,(kCueHeight-size.y)/2));
            cache_current();
            transitioning=(synced||(manual&&!manual_remaining_ms))&&!track_changed&&value.playing&&cue_key>previous_cue&&value.position_ms>=last_position&&
                value.position_ms-last_position<=750;
            transition_at=lv_tick_get();
            previous_generation = value.generation;
            previous_state = value.state;
            previous_lines = value.lines.get();
            previous_cue = cue_key;
            previous_title = value.track.title;
            previous_artist = value.track.artist;
            previous_manual_offset = manual_offset;
        }
        previous_playing = value.playing;
        const auto now=lv_tick_get();
        if(!value.playing||seek)transitioning=false;
        last_position=value.position_ms;
        animate(now,text_changed||playback_changed||seek);
        rendered = true;
    }
};

std::unique_ptr<View> view;

} // namespace

RenderStats render_stats()
{
    RenderStats value;
    value.active = counters.active.load(std::memory_order_relaxed);
    value.elapsed_ms = lv_tick_get() - counters.started.load(std::memory_order_relaxed);
    value.cycles = counters.cycles.load(std::memory_order_relaxed);
    value.render_ms = counters.render_ms.load(std::memory_order_relaxed);
    value.max_render_ms = counters.max_render_ms.load(std::memory_order_relaxed);
    value.flushed_pixels = counters.pixels.load(std::memory_order_relaxed);
    value.scaled_glyph_rasters = counters.glyphs.load(std::memory_order_relaxed);
    value.cue_rasters = counters.cues.load(std::memory_order_relaxed);
    value.cache_failures = counters.failures.load(std::memory_order_relaxed);
    return value;
}

bool wants_surface(const Snapshot& value, std::uint64_t now_ms)
{
    return presentation::wants_surface(value, now_ms);
}

void clear_locked() { view.reset(); }

void render_locked(const Snapshot& value)
{
    if (!value.enabled || !value.connected || value.link != LinkState::Ready || !value.track_ready) {
        clear_locked();
        return;
    }
    if (!view) view = std::make_unique<View>();
    if (lv_obj_get_child(lv_layer_top(), -1) != view->root) lv_obj_move_foreground(view->root);
    view->render(value);
}

} // namespace lyrics
