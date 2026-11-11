#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lyrics {

enum class LinkState { Off, Pairing, Connecting, Discovering, Ready, Error };
enum class LyricState { Waiting, Loading, Synced, NotFound, Instrumental, NetworkError, Invalid, Unsynced };

struct Line {
    std::uint32_t time_ms = 0;
    std::string text;
};

struct Track {
    std::string player;
    std::string title;
    std::string artist;
    std::string album;
    std::uint32_t duration_ms = 0;
};

struct Snapshot {
    bool enabled = false;
    bool connected = false;
    bool playing = false;
    bool track_ready = false;
    LinkState link = LinkState::Off;
    LyricState state = LyricState::Waiting;
    Track track;
    std::uint32_t position_ms = 0;
    std::uint32_t generation = 0;
    std::uint64_t last_event_ms = 0;
    std::uint64_t paused_since_ms = 0;
    int last_error = 0;
    int http_status = 0;
    int phase = 0;
    std::uint32_t elapsed_ms = 0;
    int socket_errno = 0;
    int tls_error = 0;
    int tls_flags = 0;
    std::shared_ptr<const std::vector<Line>> lines;
    bool phone_lyrics = false;
};

} // namespace lyrics
