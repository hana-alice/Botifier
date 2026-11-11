#pragma once

#include "types.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace lyrics::protocol {

constexpr std::size_t kMaxAttributeBytes = 512;
constexpr std::size_t kMaxResponseBytes = 65536;
constexpr std::size_t kMaxLines = 512;
constexpr std::uint64_t kMetadataSettleMs = 800;

bool valid_text(std::string_view value, std::size_t maximum, bool multiline = false);
// Display preference only: preserve raw lyrics, metadata and timestamps.
std::uint32_t simplify_display_character(std::uint32_t point);
bool parse_playback(std::string_view value, bool& playing, double& rate, std::uint32_t& elapsed_ms);
std::string percent_encode(std::string_view value);
std::string request_url(const Track& track, bool include_album = true);
inline bool phone_metadata_candidate(const Track& track)
{
    return track.player == reinterpret_cast<const char*>(u8"网易云音乐") && track.artist.find('/') != std::string::npos;
}
bool same_request_identity(const Track& a, const Track& b, bool phone_metadata);
bool phone_title_matches(std::string_view title, const std::vector<Line>& lines, std::uint32_t position_ms);

// valid distinguishes a structurally valid search with no unique match (id 0)
// from an invalid or unexpected response. Access keys come only from public
// KuGou search candidates; they are never used as arbitrary URL components.
struct Candidate {
    bool valid = false;
    std::uint64_t id = 0;
    std::string access_key;
    std::uint32_t duration_ms = 0;
    // LRCLIB-only fallback: one exact title/artist/album ID in the catalog
    // response. Cues beyond phone duration can only be displayed as unsynced text.
    bool unique_identity = false;
    bool phone_identity = false;
    std::uint64_t fallback_id = 0;
};

std::string netease_search_url(const Track& track);
std::string netease_lyric_url(std::uint64_t id);
std::string kugou_search_url(const Track& track);
std::string kugou_lyric_url(const Candidate& candidate);
Candidate parse_netease_search(std::string_view body, const Track& expected, bool allow_phone_identity = true);
Candidate parse_kugou_search(std::string_view body, const Track& expected);
std::string lrclib_catalog_search_url(const Track& track);
std::string lrclib_catalog_lyric_url(const Candidate& candidate);
Candidate parse_lrclib_catalog_search(std::string_view body, const Track& expected);

struct Response {
    LyricState state = LyricState::Invalid;
    std::shared_ptr<const std::vector<Line>> lines;
    int http_status = 0;
    int error = 0;
    std::uint32_t retry_after_ms = 0;
    int phase = 0;
    std::uint32_t elapsed_ms = 0;
    int socket_errno = 0;
    int tls_error = 0;
    int tls_flags = 0;
    bool phone_identity = false;
};

Response parse_response(int http_status, std::string_view body, const Track& expected,
                        std::string_view retry_after = {});
Response parse_netease_lyrics(int http_status, std::string_view body, const Track& expected);
Response parse_kugou_lyrics(int http_status, std::string_view body, const Track& expected);
Response parse_lrclib_catalog_lyrics(int http_status, std::string_view body, const Track& expected,
                                    const Candidate& candidate, std::string_view retry_after = {});
bool parse_lrc(std::string_view body, std::uint32_t duration_ms, std::vector<Line>& lines);

struct Request { Track track; std::uint32_t generation = 0; bool phone_metadata = false; };

// No platform calls: the BLE publisher and snapshots share this state behind
// their short service lock. HTTP owns a copy of Request outside that lock.
class Engine {
public:
    void set_enabled(bool enabled, std::uint64_t now);
    void link(LinkState state, int error, std::uint64_t now);
    void invalidate_track(std::uint64_t now);
    bool track_snapshot(const Track& track, std::uint64_t now);
    bool attribute(std::uint8_t entity, std::uint8_t attribute, std::string_view value, std::uint64_t now);
    Snapshot snapshot(std::uint64_t now) const;
    bool take_request(std::uint64_t now, Request& request);
    bool current(const Request& request) const;
    bool complete(const Request& request, Response response, std::uint64_t now);
    void advance(std::uint64_t now) { verify_phone_title(now); }
    void error(int value) { latest_.last_error = value; }

private:
    enum class InputMode { Normal, Probe, AwaitingTitle, Verified, Rejected };
    struct CachedLyrics {
        Track track;
        LyricState state = LyricState::Waiting;
        std::shared_ptr<const std::vector<Line>> lines;
        std::size_t bytes = 0;
    };
    void invalidate(std::uint64_t now, bool clear_fields);
    void remember_success();
    void restore_cached(std::uint64_t now);
    void verify_phone_title(std::uint64_t now);
    bool metadata_ready(std::uint64_t now) const;
    std::uint32_t position(std::uint64_t now) const;
    Snapshot latest_;
    InputMode input_mode_ = InputMode::Normal;
    std::array<CachedLyrics, 2> cache_;
    unsigned seen_ = 0;
    unsigned attempts_ = 0;
    bool requested_ = false;
    bool has_queue_ = false;
    std::uint32_t queue_index_ = 0;
    std::uint64_t metadata_at_ = 0;
    std::uint64_t position_at_ = 0;
    std::uint64_t next_request_at_ = 0;
    std::uint32_t anchor_ms_ = 0;
    double rate_ = 0;
};

} // namespace lyrics::protocol
