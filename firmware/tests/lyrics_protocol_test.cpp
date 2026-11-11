#include <apps/common/lyrics/protocol.h>
#include <apps/common/lyrics/service.h>
#include <ArduinoJson.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

using namespace lyrics;
using namespace lyrics::protocol;

namespace {
unsigned checks = 0;
template <typename Char> std::string_view utf8(const Char* value)
{
    return std::string_view(reinterpret_cast<const char*>(value));
}
void check(bool value, const char* description)
{
    ++checks;
    if (!value) throw std::runtime_error(description);
}

Track track()
{
    return {"Spotify", "Window Light", "Botifier Test Artist", "Original Demo", 90000};
}

std::string response_body(const Track& song, const char* lrc = "[00:01.00]Window light is moving\n[00:04.500]Across the quiet room")
{
    JsonDocument document;
    document["id"] = 42;
    document["trackName"] = song.title;
    document["artistName"] = song.artist;
    document["albumName"] = song.album;
    document["duration"] = song.duration_ms / 1000.0;
    document["instrumental"] = false;
    document["syncedLyrics"] = lrc;
    std::string result;
    serializeJson(document, result);
    return result;
}

void connected(Engine& engine, std::uint64_t now = 1000)
{
    engine.set_enabled(true, now);
    engine.link(LinkState::Ready, 0, now);
    check(engine.attribute(0, 0, "Spotify", now), "player name");
}

void metadata(Engine& engine, const Track& song, std::uint64_t now)
{
    check(engine.track_snapshot(song, now), "atomic complete metadata");
}

void utf8_and_playback()
{
    check(valid_text(utf8(u8"守岸人 · Window 🎵"), 512), "valid multilingual UTF8");
    for (const std::string bad : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
                                  std::string("\xe4\xb8"), std::string("\x80"), std::string("\xc2\x85"),
                                  std::string("a\0b", 3), std::string("a\x01"), std::string("\x7f")})
        check(!valid_text(bad, 512), "invalid UTF8/control rejected");
    check(!valid_text(std::string(513, 'x'), 512), "attribute byte cap");
    check(!valid_text("two\nlines", 512), "metadata newline rejected");
    check(valid_text("two\r\nlines\t", 512, true), "LRC multiline separators accepted");
    bool playing = false; double rate = 0; std::uint32_t elapsed = 0;
    check(parse_playback("1,1.000000,20.125", playing, rate, elapsed) && playing && rate == 1 && elapsed == 20125, "AMS playback values");
    check(parse_playback("0,1,20.125", playing, rate, elapsed) && !playing && rate == 0 && elapsed == 20125, "paused state freezes rate");
    check(parse_playback("2,-1,10", playing, rate, elapsed) && playing && rate == -1, "rewind rate accepted");
    for (const auto* bad : {"1,1,nan", "1,inf,2", "1,1,1e3", "1,1,20,0", "4,1,20", "1,17,20", "1,1,-1",
                            "1,1,86401", "1,1,9999999999999999", "1, 1,2", "1,1,2 ", ",1,2", "1,,2", "1,1,.2", "1,1,2."})
        check(!parse_playback(bad, playing, rate, elapsed), "malformed AMS numeric rejected");
    check(percent_encode(utf8(u8"守岸人 &+/?#")) == "%E5%AE%88%E5%B2%B8%E4%BA%BA%20%26%2B%2F%3F%23", "UTF8 URL encoding");
    check(percent_encode("A-z_09.~") == "A-z_09.~", "URL unreserved bytes");
    const auto url = request_url(track());
    check(url.find("https://lrclib.net/api/get?track_name=Window%20Light&artist_name=Botifier%20Test%20Artist") == 0, "fixed HTTPS lookup URL");
    check(url.find("&duration=90.000") != std::string::npos, "duration formatting independent of locale");
}

void unsynced_timeline_fallback()
{
    auto phone = track(); phone.duration_ms = 192614;
    auto catalog = phone; catalog.duration_ms = 198000;
    const Candidate selected{true, 42, {}, 198000};
    const auto body = response_body(catalog,
        "[00:01.00]Original first phrase\n[03:09.580]Original middle phrase\n[03:25.760]Original final phrase\n[03:28.180]");
    auto result = parse_lrclib_catalog_lyrics(200, body, phone, selected);
    check(result.state == LyricState::Unsynced && result.lines && result.lines->size() == 4,
          "valid complete lyrics survive catalog and phone duration mismatch as unsynced text");
    check(result.lines->at(2).time_ms == 205760 && result.lines->at(2).text == "Original final phrase" &&
          result.lines->back().time_ms == 208180, "fallback neither trims words nor rescales original timestamps");
    const auto exact = response_body(phone, "[00:01.00]Original first phrase\n[03:25.760]Original final phrase");
    check(parse_response(200, exact, phone).state == LyricState::Unsynced,
          "exact response with a mismatched valid timeline is also text only");
    for (const auto* bad : {"[00:01.00]Original first phrase\n[03:25.760]Original final phrase\n[bad]broken",
                           "[03:25.760]single fragment", "[00:01.00]ab\n[03:25.760]cd"}) {
        const auto rejected = parse_response(200, response_body(phone, bad), phone);
        check(rejected.state == LyricState::Invalid && !rejected.lines,
              "fallback still validates all rows and minimum useful text");
    }
    for (const auto field : {0, 1, 2}) {
        auto wrong = phone;
        (field == 0 ? wrong.title : field == 1 ? wrong.artist : wrong.album) += " (different version)";
        const auto rejected = parse_response(200, exact, wrong);
        check(rejected.state == LyricState::NotFound && !rejected.lines,
              "text fallback cannot bypass identity and version checks");
    }
    auto missing_duration = phone; missing_duration.duration_ms = 0;
    auto tiny_catalog = phone; tiny_catalog.duration_ms = 1;
    check(parse_response(200, response_body(tiny_catalog), missing_duration).state == LyricState::Invalid,
          "text fallback does not repair invalid phone metadata");
    Engine engine; connected(engine);
    check(engine.track_snapshot(phone, 1100), "text fallback track accepted");
    Request request;
    check(engine.take_request(2000, request) && engine.complete(request, result, 2100),
          "text fallback published through the actual engine");
    check(engine.snapshot(2200).state == LyricState::Unsynced && engine.snapshot(2200).lines == result.lines,
          "engine retains complete text without claiming sync");
    engine.invalidate_track(2300);
    check(!engine.snapshot(2300).lines, "changing track clears unsynced text");
    check(!engine.complete(request, result, 2350) && !engine.snapshot(2350).lines,
          "a stale text fallback cannot publish after the track changes");
    check(engine.track_snapshot(phone, 2400) && engine.take_request(4000, request),
          "a text fallback is not cached as a permanent replacement for synced lyrics");
}

void lrc_boundaries()
{
    std::vector<Line> lines;
    check(parse_lrc("[ar:Botifier]\r\n[ti:Original]\r\n \t \r\n[offset:+250]\r\n[00:04.500][00:08.00]Second phrase\r\n[00:01.00]First phrase\r\n[00:04.500]Translation\r\n[00:06.00]\r\n", 90000, lines), "metadata CRLF whitespace blank offset repeated timestamps translation");
    check(lines.size() == 5 && lines[0].time_ms == 1250 && lines[1].text == "Second phrase" &&
          lines[2].text == "Translation" && lines[3].text.empty() && lines[4].time_ms == 8250, "stable ordered complete timeline");
    check(parse_lrc("[offset:-1000]\n[00:00.500]First phrase\n[00:02.000]Next phrase", 90000, lines) && lines.front().time_ms == 0, "negative offset clamps at zero");
    for (const std::string bad : {std::string("[00:01.00]only"), std::string("[00:01.00]ab\n[00:02.00]cd"),
        std::string("[00:01.00]First phrase\n[00:01.00]Translation"), std::string("[00:60.00]Bad seconds\n[00:02.00]Second phrase"),
        std::string("[00:01.1]Bad fraction\n[00:02.00]Second phrase"), std::string("[00:01.1234]Bad precision\n[00:02.00]Second phrase"),
        std::string("[00:01.00]First phrase\n[01:32.001]Past track edge"), std::string("[offset:60001]\n[00:01.00]First\n[00:03.00]Next"),
        std::string("[offset:0]\n[offset:1]\n[00:01.00]First\n[00:03.00]Next"), std::string("[00:01.00]First phrase\nUnmarked garbage"),
        std::string("[00:01.00]First phrase\n[bad]Bad marker"), std::string("[00:01.00]First phrase\n[00:03.00]") + std::string("a\0b", 3)})
        check(!parse_lrc(bad, 90000, lines) && lines.empty(), "invalid timeline rejected without partial output");
    check(parse_lrc("[00:01.00]First phrase\n[01:32.000]Boundary phrase", 90000, lines), "two second tail margin");
    check(!parse_lrc("[00:01.00]" + std::string(513, 'x') + "\n[00:03.00]Next phrase", 90000, lines), "single lyric byte limit");
    std::string many;
    for (unsigned i = 0; i < 513; ++i) many += "[00:01.00]Line\n";
    check(!parse_lrc(many, 90000, lines), "line count limit");
    std::string tags;
    for (unsigned i = 0; i < 17; ++i) tags += "[00:01.00]";
    tags += "First phrase\n[00:03.00]Next phrase";
    check(!parse_lrc(tags, 90000, lines), "per-row timestamp cap");
}

void response_validation()
{
    const auto song = track();
    const auto good = response_body(song);
    auto result = parse_response(200, good, song);
    check(result.state == LyricState::Synced && result.lines && result.lines->size() == 2, "actual JSON produces synced lyrics");
    auto unknown_album = song; unknown_album.album.clear();
    check(parse_response(200, good, unknown_album).state == LyricState::Synced,
          "absent AMS album accepts matching title artist and duration");
    check(parse_response(404, "", song).state == LyricState::NotFound, "404 not found");
    check(parse_response(302, good, song).state == LyricState::NetworkError, "redirect not treated as success");
    check(parse_response(500, good, song).state == LyricState::NetworkError, "HTTP error status");
    check(parse_response(429, "", song, "120").retry_after_ms == 120000, "Retry-After seconds respected");
    check(parse_response(429, "", song, "9999999999999").retry_after_ms == 60000, "unbounded retry input fallback");
    check(parse_response(429, "", song, "0").retry_after_ms == 30000, "retry minimum");
    auto wrong = song; wrong.title += " Live";
    check(parse_response(200, response_body(wrong), song).state == LyricState::NotFound, "wrong track version rejected");
    check(parse_response(200, response_body(wrong), unknown_album).state == LyricState::NotFound, "absent album still rejects wrong title");
    wrong = song; wrong.artist = "Another artist";
    check(parse_response(200, response_body(wrong), song).state == LyricState::NotFound, "wrong artist rejected");
    check(parse_response(200, response_body(wrong), unknown_album).state == LyricState::NotFound, "absent album still rejects wrong artist");
    wrong = song; wrong.album = "Another album";
    check(parse_response(200, response_body(wrong), song).state == LyricState::NotFound, "wrong album rejected");
    wrong = song; wrong.duration_ms += 2001;
    check(parse_response(200, response_body(wrong), song).state == LyricState::NotFound, "duration tolerance strict");
    check(parse_response(200, response_body(wrong), unknown_album).state == LyricState::NotFound, "absent album still rejects wrong duration");
    wrong = song; wrong.duration_ms += 2000;
    check(parse_response(200, response_body(wrong), song).state == LyricState::Synced, "duration tolerance inclusive");
    JsonDocument document;
    deserializeJson(document, good);
    auto encode = [&] { std::string body; serializeJson(document, body); return body; };
    document["instrumental"] = true;
    check(parse_response(200, encode(), song).state == LyricState::Instrumental, "instrumental explicit");
    document["instrumental"] = false;
    document["syncedLyrics"] = nullptr;
    check(parse_response(200, encode(), song).state == LyricState::NotFound, "plain lyrics only is not synced");
    document["syncedLyrics"] = "[00:01.00]a";
    check(parse_response(200, encode(), song).state == LyricState::Invalid, "dirty short fragment not accepted");
    document["syncedLyrics"] = "[00:01.00]First phrase\n[00:04.00]Next phrase";
    document["duration"] = "90";
    check(parse_response(200, encode(), song).state == LyricState::Invalid, "typed numeric duration required");
    document["duration"] = 90;
    document["trackName"] = serialized("\"Window\\u0000Light\"");
    check(parse_response(200, encode(), song).state == LyricState::Invalid, "escaped metadata NUL rejected");
    for (const auto& bad : {good + " garbage", "/* comment */" + good, good.substr(0, good.size() - 1) + ",\"duration\":90}",
                            std::string("{'trackName':'Window Light'}"), std::string("{\"duration\":NaN}"), std::string(65537, 'x'),
                            std::string("{\"duration\":01}"), std::string("{\"x\":\"\\ud800\"}"), std::string("{\"x\":\"\\udc00\"}"),
                            std::string("{\"x\":\"\xc0\xaf\"}")})
        check(parse_response(200, bad, song).state == LyricState::Invalid, "invalid strict JSON rejected");
}

std::string encode(const JsonDocument& document)
{
    std::string result; serializeJson(document, result); return result;
}

void netease_song(JsonArray songs, const Track& song, std::uint64_t id)
{
    auto value = songs.add<JsonObject>();
    value["id"] = id; value["name"] = song.title; value["duration"] = song.duration_ms;
    value["album"]["name"] = song.album;
    value["artists"].to<JsonArray>().add<JsonObject>()["name"] = song.artist;
}

void kugou_song(JsonArray songs, const Track& song, std::uint64_t id)
{
    auto value = songs.add<JsonObject>();
    value["id"] = std::to_string(id); value["song"] = song.title; value["duration"] = song.duration_ms;
    value["singer"] = song.artist; value["accesskey"] = "public_test_key-42";
    value["album"] = song.album;
}

// Test-only canonical encoder: the production path only decodes a provider
// response, and no untimed text is converted into artificial lyric cues.
std::string base64(std::string_view text)
{
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < text.size(); i += 3) {
        const auto a = static_cast<unsigned char>(text[i]);
        const auto b = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0;
        const auto c = i + 2 < text.size() ? static_cast<unsigned char>(text[i + 2]) : 0;
        result += alphabet[a >> 2]; result += alphabet[((a & 3) << 4) | (b >> 4)];
        result += i + 1 < text.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        result += i + 2 < text.size() ? alphabet[c & 63] : '=';
    }
    return result;
}

void fallback_urls_and_legacy_boundaries()
{
    const auto song = track();
    check(request_url(song).find("&album_name=Original%20Demo") != std::string::npos, "legacy exact album lookup retained");
    check(request_url(song, false).find("album_name") == std::string::npos &&
          request_url(song, false).find("&duration=90.000") != std::string::npos, "cross-album retry omits only album query");
    auto other_album = song; other_album.album = "A different release";
    check(parse_response(200, response_body(other_album), song).state == LyricState::NotFound, "baseline exact lookup rejects different album");
    auto no_album = song; no_album.album.clear();
    check(parse_response(200, response_body(other_album), no_album).state == LyricState::Synced, "explicit cross-album retry retains title artist duration validation");
    check(netease_search_url(song) == "https://music.163.com/api/search/get?s=Window%20Light%20Botifier%20Test%20Artist&type=1&limit=8&offset=0", "bounded NetEase search URL");
    check(netease_lyric_url(1842025914) == "https://music.163.com/api/song/lyric?id=1842025914&lv=-1&kv=-1&tv=-1", "NetEase lyric URL uses numeric selected id");
    check(kugou_search_url(song) == "https://lyrics.kugou.com/search?ver=1&man=yes&client=pc&keyword=Window%20Light%20Botifier%20Test%20Artist&duration=90000", "bounded KuGou search uses milliseconds");
    check(kugou_lyric_url({true,42,"public_test_key-42"}) == "https://lyrics.kugou.com/download?ver=1&client=pc&id=42&fmt=lrc&charset=utf8&accesskey=public_test_key-42", "KuGou URL accepts only a selected candidate");
    check(netease_lyric_url(0).empty() && netease_lyric_url(UINT64_MAX).empty(), "NetEase invalid id cannot construct URL");
    for (const Candidate candidate : {Candidate{}, Candidate{true,0,"key"}, Candidate{false,42,"key"},
            Candidate{true,UINT64_MAX,"key"}, Candidate{true,42,{}}, Candidate{true,42,"x&evil=1"},
            Candidate{true,42,"../path"}, Candidate{true,42,std::string(129,'a')}})
        check(kugou_lyric_url(candidate).empty(), "KuGou invalid id/key cannot construct URL");
    auto chinese = song; chinese.title = std::string(utf8(u8"这世界那么多人 &+?")); chinese.artist = std::string(utf8(u8"莫文蔚"));
    check(netease_search_url(chinese).find("%E8%BF%99%E4%B8%96%E7%95%8C") != std::string::npos &&
          kugou_search_url(chinese).find("%20%26%2B%3F") != std::string::npos, "provider query UTF8 and metacharacters are percent encoded");
    auto invalid = song; invalid.artist.clear();
    check(netease_search_url(invalid).empty() && kugou_search_url(invalid).empty(), "empty artist cannot query a broad unrelated search");
    invalid = song; invalid.duration_ms = 0;
    check(netease_search_url(invalid).empty() && kugou_search_url(invalid).empty(), "duration required for provider matching");
}

void fallback_candidate_matching()
{
    const auto song = track();
    for (const bool kugou : {false,true}) {
        JsonDocument document;
        document[kugou?"status":"code"] = 200;
        auto songs = kugou?document["candidates"].to<JsonArray>():document["result"]["songs"].to<JsonArray>();
        auto append = [&](const Track& value, std::uint64_t id) {if(kugou)kugou_song(songs,value,id);else netease_song(songs,value,id);};
        auto parse = [&] {return kugou?parse_kugou_search(encode(document),song):parse_netease_search(encode(document),song);};
        check(parse().valid&&!parse().id, "empty but valid provider search differs from invalid JSON");
        auto wrong = song; wrong.artist = "Some other artist"; append(wrong,1); append(song,42);
        auto selected = parse();
        check(selected.valid&&selected.id==42, "same-name other artist is not accepted as the first result");
        check(!kugou||selected.access_key=="public_test_key-42", "selected public access key retained");
        songs.clear(); wrong=song;wrong.title += " (Live)";append(wrong,1);
        check(parse().valid&&!parse().id, "Live title suffix is not stripped");
        songs.clear(); wrong=song;wrong.title += " - Remix";append(wrong,1);
        check(parse().valid&&!parse().id, "Remix title suffix is not stripped");
        songs.clear(); wrong=song;wrong.duration_ms+=2001;append(wrong,1);
        check(parse().valid&&!parse().id, "candidate duration beyond two seconds rejected");
        songs.clear(); wrong.duration_ms=song.duration_ms+2000;append(wrong,1);
        check(parse().id==1, "two-second candidate duration boundary inclusive");
        songs.clear(); wrong=song;wrong.album="Other release";append(wrong,1);
        check(parse().id==1, "same recording may match on another album");
        auto same_album=song;same_album.duration_ms+=2000;append(same_album,2);
        check(parse().id==2, "same album ranks ahead of closest cross-album duration");
        songs.clear();wrong.duration_ms+=1200;append(wrong,1);wrong.duration_ms=song.duration_ms+800;append(wrong,2);
        check(parse().id==2, "closest duration wins within equal album rank");
        wrong.duration_ms=song.duration_ms-800;append(wrong,3);
        check(parse().valid&&!parse().id, "equally ranked distinct ids rejected as ambiguous");
        songs.clear();append(song,42);append(song,42);
        check(parse().id==42, "identical duplicate candidate does not create false ambiguity");
        songs.clear();wrong=song;wrong.title="  WINDOW   Light ";wrong.artist=" botifier test artist  ";append(wrong,42);
        check(parse().id==42, "ASCII case and surrounding/repeated spaces normalized");
        songs.clear();append(song,42);
        if(kugou) {songs[0]["singer"]="Botifier Test Artist,Guest";}
        else {songs[0]["artists"].as<JsonArray>().add<JsonObject>()["name"]="Guest";}
        check(parse().valid&&!parse().id, "partial artist list cannot select a collaboration");
        auto collaboration=song;collaboration.artist="Guest & Botifier Test Artist";
        selected=kugou?parse_kugou_search(encode(document),collaboration):parse_netease_search(encode(document),collaboration);
        check(selected.id==42, "explicitly delimited full artist sets match regardless of order");
        collaboration.artist=" Guest ; Botifier Test Artist ";
        selected=kugou?parse_kugou_search(encode(document),collaboration):parse_netease_search(encode(document),collaboration);
        check(selected.id==42, "semicolon-separated collaboration matches the complete artist set");
        songs.clear();wrong=song;wrong.artist="AC/DC";append(wrong,42);
        selected=kugou?parse_kugou_search(encode(document),wrong):parse_netease_search(encode(document),wrong);
        check(selected.id==42, "slash in AC/DC remains part of its artist name");
        wrong.artist="AC";
        selected=kugou?parse_kugou_search(encode(document),wrong):parse_netease_search(encode(document),wrong);
        check(selected.valid&&!selected.id, "AC/DC is not treated as artist AC");
        songs.clear();auto chinese=song;chinese.title=std::string(utf8(u8"这世界那么多人"));chinese.artist=std::string(utf8(u8"莫文蔚"));append(chinese,1842025914);
        selected=kugou?parse_kugou_search(encode(document),chinese):parse_netease_search(encode(document),chinese);
        check(selected.id==1842025914, "Chinese metadata preserved exactly through JSON matching");
        auto unrelated=chinese;unrelated.artist="Karen Mok";
        selected=kugou?parse_kugou_search(encode(document),unrelated):parse_netease_search(encode(document),unrelated);
        check(selected.valid&&!selected.id, "artist translations are not invented or hardcoded");
        songs.clear();for(unsigned i=0;i<(kugou?11u:9u);++i)append(song,i+1);
        check(!parse().valid, "provider candidate array cap enforced");
    }
}

void fallback_candidate_validation()
{
    const auto song=track();
    for(const bool kugou:{false,true}) {
        JsonDocument document;document[kugou?"status":"code"]=200;
        auto songs=kugou?document["candidates"].to<JsonArray>():document["result"]["songs"].to<JsonArray>();
        if(kugou)kugou_song(songs,song,42);else netease_song(songs,song,42);
        auto parse=[&](std::string_view body){return kugou?parse_kugou_search(body,song):parse_netease_search(body,song);};
        const auto good=encode(document);
        for(const auto* bad:{"{}","[]","null","{\"code\":\"200\"}","{\"status\":\"200\"}",
            "{\"code\":200,\"result\":[]}","{\"status\":200,\"candidates\":{}}"})
            check(!parse(bad).valid,"provider envelope and typed fields required");
        for(const auto& bad:{good+" trailing","/* comment */"+good,good.substr(0,good.size()-1)+",\"x\":1,\"x\":2}",
            std::string(65537,'x'),std::string("{\"x\":\"\\ud800\"}"),std::string("{\"x\":\"\xc0\xaf\"}")})
            check(!parse(bad).valid,"provider search uses strict framing UTF8 duplicate and size checks");
        for(const auto* bad:{"0","-1","1.0","1e3","true","null","18446744073709551616"}) {
            songs[0]["id"]=serialized(bad);check(!parse(encode(document)).valid,"candidate integer id required without coercion");
        }
        deserializeJson(document,good);songs=kugou?document["candidates"].as<JsonArray>():document["result"]["songs"].as<JsonArray>();
        songs[0]["duration"]="90000";check(!parse(encode(document)).valid,"duration cannot be a numeric string");
        songs[0]["duration"]=90000.5;check(!parse(encode(document)).valid,"duration requires integral milliseconds");
        songs[0]["duration"]=0;check(!parse(encode(document)).valid,"zero candidate duration rejected");
        songs[0]["duration"]=90000;
        if(kugou) {
            for(const auto* bad:{"","0","42&evil=1","-42"," 42","9223372036854775808"}) {
                songs[0]["id"]=bad;check(!parse(encode(document)).valid,"KuGou id string is bounded positive digits only");
            }
            songs[0]["id"]="42";
            for(const auto& bad:{std::string{},std::string("public&x=1"),std::string("../path"),std::string(129,'x')}) {
                songs[0]["accesskey"]=bad;check(!parse(encode(document)).valid,"KuGou access key is bounded and URL-safe");
            }
            songs[0]["accesskey"]="good";songs[0]["album"]=42;
            check(!parse(encode(document)).valid,"optional KuGou album must be a text string");
        } else {
            songs[0]["id"]="42";check(!parse(encode(document)).valid,"NetEase id is a JSON integer, not string");
            songs[0]["id"]=42;songs[0]["artists"]=nullptr;
            check(!parse(encode(document)).valid,"NetEase artists requires array");
            songs[0]["artists"].to<JsonArray>().add<JsonObject>()["name"]="";
            check(!parse(encode(document)).valid,"empty NetEase artist rejected");
        }
    }
    const auto empty=parse_netease_search("{\"code\":200,\"result\":{\"songCount\":0}}",song);
    check(empty.valid&&!empty.id,"NetEase explicit zero-song envelope accepted");
    check(!parse_netease_search("{\"code\":200,\"result\":{\"songCount\":1}}",song).valid,"missing nonempty songs array invalid");
    JsonDocument nested;nested["code"]=200;
    auto candidates=nested["result"]["songs"].to<JsonArray>();netease_song(candidates,song,42);
    // Normal provider metadata includes these seventh-level containers even
    // when alias arrays are empty. This original fixture mirrors that shape
    // without copying a provider response or any copyrighted lyric text.
    candidates[0]["album"]["artist"]["alias"].to<JsonArray>();
    candidates[0]["artists"][0]["alias"].to<JsonArray>();
    check(parse_netease_search(encode(nested),song).id==42,"NetEase normal nested empty optional artist aliases must not reject the whole search");
    candidates[0]["album"]["artist"]["alias"].as<JsonArray>().add("Original album artist alias");
    candidates[0]["artists"][0]["alias"].as<JsonArray>().add("Original artist alias");
    check(parse_netease_search(encode(nested),song).id==42,"NetEase nonempty optional aliases fit the bounded provider-specific nesting budget");
    candidates[0]["artists"][0]["name"]="Unrelated Performer";
    candidates[0]["artists"][0]["alias"][0]=song.artist;
    const auto mismatch=parse_netease_search(encode(nested),song);
    check(mismatch.valid&&!mismatch.id,"Unrelated nested metadata does not weaken required artist identity");
    candidates[0]["album"]["artist"]["alias"][0].to<JsonObject>()["a"]["b"]["c"]=1;
    check(!parse_netease_search(encode(nested),song).valid,"NetEase optional metadata still has a finite nesting limit");
}

void fallback_lyric_validation()
{
    const auto song=track();
    const std::string lrc="[00:01.00]Window light is moving\n[00:04.500]Across the quiet room";
    JsonDocument netease;netease["code"]=200;netease["lrc"]["lyric"]=lrc;
    JsonDocument kugou;kugou["status"]=200;kugou["content"]=base64(lrc);
    // Baseline protocol cannot interpret either envelope; the new adapters
    // feed actual timed text into the same existing strict LRC parser.
    check(parse_response(200,encode(netease),song).state==LyricState::Invalid&&
          parse_response(200,encode(kugou),song).state==LyricState::Invalid,"baseline LRCLIB parser does not accept unrelated envelopes");
    for(const bool kg:{false,true}) {
        auto parse=[&](int status,std::string_view body){return kg?parse_kugou_lyrics(status,body,song):parse_netease_lyrics(status,body,song);};
        const auto good=kg?encode(kugou):encode(netease);
        auto response=parse(200,good);
        check(response.state==LyricState::Synced&&response.lines&&response.lines->size()==2&&
              response.lines->front().time_ms==1000&&response.lines->back().time_ms==4500,"provider preserves actual synced timestamps");
        check(parse(404,good).state==LyricState::NotFound&&parse(503,good).state==LyricState::NetworkError&&
              parse(429,good).retry_after_ms==60000,"provider HTTP errors preserve existing retry semantics");
        for(const auto& bad:{good+" trailing",std::string("[]"),std::string(65537,'x'),
            good.substr(0,good.size()-1)+",\"x\":1,\"x\":2}"})
            check(parse(200,bad).state==LyricState::Invalid,"provider lyric framing size and duplicate keys rejected");
        const std::string late = "[00:01.00]First phrase\n[01:32.001]Beyond track end";
        if(kg)kugou["content"]=base64(late);else netease["lrc"]["lyric"]=late;
        response=parse(200,kg?encode(kugou):encode(netease));
        check(response.state==LyricState::Unsynced&&response.lines&&response.lines->back().time_ms==92001,
              "both alternate providers retain valid late text without claiming sync");
        for(const auto& text:{std::string("Untimed plain lyrics\nSecond line"),std::string("[00:01.00]only"),
            std::string("[00:01.00]First phrase\n")+std::string("\xc0\xaf")}) {
            if(kg)kugou["content"]=base64(text);else netease["lrc"]["lyric"]=text;
            check(parse(200,kg?encode(kugou):encode(netease)).state==LyricState::Invalid,"provider cannot invent timings or accept dirty LRC");
        }
        const std::string chinese=std::string(utf8(u8"[00:01.00]这世界那么多人\n[00:04.500]人群里敞着一扇门"));
        if(kg)kugou["content"]=base64(chinese);else netease["lrc"]["lyric"]=chinese;
        response=parse(200,kg?encode(kugou):encode(netease));
        check(response.state==LyricState::Synced&&response.lines&&response.lines->front().text==utf8(u8"这世界那么多人"),"Chinese timed text preserved without normalization");
        if(kg)kugou["content"]="";else netease["lrc"]["lyric"]="";
        check(parse(200,kg?encode(kugou):encode(netease)).state==LyricState::NotFound,"empty provider lyric is not a network failure");
    }
    for(const auto* malformed:{"A","AAA","AAAA=","A===","=AAA","AA=A","AA==AAAA","!!!!","YQ= ","YR==","YWJ=","YQ\n==","77+9AA=="}) {
        kugou["content"]=malformed;
        check(parse_kugou_lyrics(200,encode(kugou),song).state==LyricState::Invalid,"strict base64 length alphabet padding tail bits UTF8 and controls");
    }
    for(unsigned remainder:{1u,2u}) {
        auto text=lrc;while(text.size()%3!=remainder)text.push_back(' ');
        auto content=base64(text);kugou["content"]=content;
        check(parse_kugou_lyrics(200,encode(kugou),song).state==LyricState::Synced,"canonical padded base64 remains a valid timed LRC");
        constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const auto at=content.size()-(remainder==1?3:2);
        content[at]=alphabet[alphabet.find(content[at])|1u];kugou["content"]=content;
        check(parse_kugou_lyrics(200,encode(kugou),song).state==LyricState::Invalid,"nonzero base64 padding bits rejected even if relaxed decoding would yield the same valid LRC");
    }
    check(parse_netease_lyrics(200,"{\"code\":200,\"nolyric\":true}",song).state==LyricState::NotFound,"explicit NetEase no-lyric state");
    check(parse_netease_lyrics(200,"{\"code\":200,\"lrc\":{\"lyric\":42}}",song).state==LyricState::Invalid,"NetEase lyric field type checked");
    check(parse_kugou_lyrics(200,"{\"status\":200,\"content\":42}",song).state==LyricState::Invalid,"KuGou content field type checked");
}

void catalog_record(JsonArray records, const Track& song, std::uint64_t id)
{
    auto item=records.add<JsonObject>();
    item["id"]=id;item["trackName"]=song.title;item["artistName"]=song.artist;item["albumName"]=song.album;
    item["duration"]=song.duration_ms/1000.0;item["instrumental"]=false;item["hasSyncedLyrics"]=true;
}

void han_metadata_compatibility()
{
    check(simplify_display_character(U'愛')==U'爱'&&simplify_display_character(U'說')==U'说'&&
          simplify_display_character(U'後')==U'后', "display prefers simplified Han including the unambiguous supplement");
    for(const auto point:{U'A',U'a',U' ',U'\n',U'！',U'你',U'乾',U'著',U'𠮷',U'🎵'})
        check(simplify_display_character(point)==point, "display preference preserves case spaces punctuation unmapped and contextual forms");
    auto traditional_title=track(), simplified_title=track();
    traditional_title.title=std::string(utf8(u8"後")); simplified_title.title=std::string(utf8(u8"后"));
    check(parse_response(200,response_body(traditional_title),simplified_title).state==LyricState::NotFound,
          "display-only supplement does not change metadata identity matching");
    const auto raw=parse_response(200,response_body(track(),reinterpret_cast<const char*>(u8"[00:01.00]愛在後來\n[00:04.50]聽你說話")),track());
    check(raw.state==LyricState::Synced&&raw.lines&&raw.lines->front().text==utf8(u8"愛在後來")&&
          raw.lines->back().time_ms==4500,"raw traditional source and cue times remain unchanged");
    const Track traditional{"Spotify", std::string(utf8(u8"風雲決")), std::string(utf8(u8"任賢齊")),
                            std::string(utf8(u8"齊待")), 287947};
    const Track simplified{"Spotify", std::string(utf8(u8"风云决")), std::string(utf8(u8"任贤齐")),
                           std::string(utf8(u8"齐待")), 287947};
    for (const bool reverse : {false, true}) {
        const auto& expected = reverse ? simplified : traditional;
        const auto& actual = reverse ? traditional : simplified;
        check(parse_response(200, response_body(actual), expected).state == LyricState::Synced,
              "traditional/simplified metadata compares equivalently in either direction");
        JsonDocument catalog;
        catalog_record(catalog.to<JsonArray>(), actual, 42);
        const auto selected = parse_lrclib_catalog_search(encode(catalog), expected);
        check(selected.id == 42 && parse_lrclib_catalog_lyrics(200, response_body(actual), expected, selected).state == LyricState::Synced,
              "catalog selection and download use the same Chinese compatibility");
        for (const bool kg : {false, true}) {
            JsonDocument document; document[kg ? "status" : "code"] = 200;
            auto items = kg ? document["candidates"].to<JsonArray>() : document["result"]["songs"].to<JsonArray>();
            if (kg) kugou_song(items, actual, 42); else netease_song(items, actual, 42);
            check((kg ? parse_kugou_search(encode(document), expected) : parse_netease_search(encode(document), expected)).id == 42,
                  "Chinese fallback candidate compatibility retains complete identities");
        }
    }
    auto wrong = simplified;
    for (unsigned field = 0; field < 3; ++field) {
        wrong = simplified;
        (field == 0 ? wrong.title : field == 1 ? wrong.artist : wrong.album) += " (Live)";
        check(parse_response(200, response_body(wrong), traditional).state == LyricState::NotFound,
              "Chinese folding never removes version or identity suffixes");
    }
    check(request_url(traditional).find(percent_encode(traditional.title)) != std::string::npos,
          "provider query keeps original phone metadata");
    Engine engine; connected(engine); metadata(engine, traditional, 1100);
    check(engine.snapshot(2000).track.title == traditional.title && engine.snapshot(2000).track.artist == traditional.artist,
          "metadata compatibility does not rewrite display or cache identity");
    auto unicode = track(); unicode.title = std::string(utf8(u8"未知字 㐀 𠮷 🎵 カタカナ"));
    check(parse_response(200, response_body(unicode), unicode).state == LyricState::Synced,
          "unmapped Han supplementary characters emoji and kana survive unchanged");
    unicode.title += "x";
    check(parse_response(200, response_body(unicode), track()).state == LyricState::NotFound,
          "unmapped text is never dropped from identity");

    auto phone = track(); phone.title = std::string(utf8(u8"发")); phone.duration_ms = 288763;
    auto other = phone; other.title = std::string(utf8(u8"發")); other.duration_ms = 311000;
    JsonDocument collisions; auto records = collisions.to<JsonArray>(); catalog_record(records, other, 42);
    other.title = std::string(utf8(u8"髮")); catalog_record(records, other, 43);
    check(parse_lrclib_catalog_search(encode(collisions), phone).id == 0,
          "script-folded different IDs still block unique-identity duration recovery");
}

void kugou_lyric_quality()
{
    const auto expected = track();
    JsonDocument document; document["status"] = 200;
    auto items = document["candidates"].to<JsonArray>();
    auto actual = expected; actual.duration_ms += 26;
    for (unsigned i = 0; i < 3; ++i) {
        kugou_song(items, actual, 42 + i);
        items[i]["score"] = 60 - 10 * i;
        items[i]["language"] = std::string(utf8(i == 2 ? u8"伴奏" : u8"国语"));
    }
    auto select = [&] { return parse_kugou_search(encode(document), expected); };
    check(select().id == 42, "KuGou score resolves duplicate lyric editions of one matched recording");
    items[2]["score"] = 100;
    check(select().id == 42, "explicit accompaniment cannot win by score");
    items[2]["language"] = std::string(utf8(u8"純音樂"));
    check(select().id == 42, "traditional instrumental marker also excluded");
    items[2]["language"] = "Instrumental";
    check(select().id == 42, "English instrumental marker excluded");
    items[1]["score"] = 60;
    check(select().valid && select().id == 0, "equal lyric scores remain ambiguous");
    items[0].remove("score"); items[1].remove("score");
    check(select().id == 0, "missing scores do not invent a winner");
    items[0]["score"] = 60; items[1]["score"] = 100;
    items[1]["singer"] = "Different singer";
    check(select().id == 42, "score cannot override wrong singer");
    items[1]["singer"] = actual.artist; items[1]["song"] = actual.title + " (Live)";
    check(select().id == 42, "score cannot erase a live version suffix");
    items[1]["song"] = actual.title; items[1]["duration"] = expected.duration_ms + 2001;
    check(select().id == 42, "score cannot widen the duration gate");
    items[1]["duration"] = expected.duration_ms + 27;
    check(select().id == 42, "closer duration precedes lyric quality");
    items[1]["duration"] = actual.duration_ms; items[1]["album"] = "Other edition";
    check(select().id == 42, "matching album precedes lyric quality");
    for (const char* invalid : {"-1", "101", "1.5", "true", "\"60\""}) {
        JsonDocument value; deserializeJson(value, invalid); items[0]["score"] = value.as<JsonVariant>();
        check(select().id == 43, "invalid score row is skipped while the other valid recording remains eligible");
    }
    // No score may decide between distinct raw identities or two equally close
    // recording durations, including many-to-one Chinese script collisions.
    auto collision = expected; collision.title = std::string(utf8(u8"发"));
    auto a = collision; a.title = std::string(utf8(u8"發"));
    auto b = collision; b.title = std::string(utf8(u8"髮"));
    items.clear(); kugou_song(items, a, 42); kugou_song(items, b, 43);
    items[0]["score"] = 20; items[1]["score"] = 60;
    check(parse_kugou_search(encode(document), collision).id == 0,
          "quality never resolves a many-to-one raw identity collision");
    items.clear(); a = b = expected; a.duration_ms += 26; b.duration_ms -= 26;
    kugou_song(items, a, 42); kugou_song(items, b, 43);
    items[0]["score"] = 20; items[1]["score"] = 60;
    check(select().id == 0, "different recording durations cannot be disambiguated by score");
    kugou_song(items, a, 44); items[2]["score"] = 100;
    check(select().id == 0, "later higher score cannot erase an earlier recording conflict");
    std::array<unsigned, 3> order{0, 1, 2};
    do {
        items.clear();
        for (unsigned index : order) {
            kugou_song(items, actual, 42 + index);
            items[items.size() - 1]["score"] = 60 - 10 * index;
        }
        check(select().id == 42, "lyric score winner does not depend on source ordering");
    } while (std::next_permutation(order.begin(), order.end()));
    kugou_song(items, actual, 42); items[3]["score"] = 60;
    check(select().id == 42, "same lyric ID and key repeated is not ambiguous");
}

void catalog_unique_identity_recovery()
{
    // Only the measured duration shapes are retained; identities and lyrics
    // are original fixtures, never copied from a music provider.
    const struct { std::uint32_t phone_ms, catalog_ms; } cases[]{
        {288763,311000}, {211452,226000}
    };
    bool recovered_all = true;
    for (const auto& value : cases) {
        auto phone = track(); phone.duration_ms = value.phone_ms;
        auto catalog = phone; catalog.duration_ms = value.catalog_ms;
        JsonDocument document;
        catalog_record(document.to<JsonArray>(), catalog, 42);
        const auto selected = parse_lrclib_catalog_search(encode(document), phone);
        const auto response = parse_lrclib_catalog_lyrics(200, response_body(catalog,
            "[00:01.000]A quiet room wakes slowly\n[03:20.040]Sunlight rests on painted walls"), phone, selected);
        const bool recovered = selected.valid && selected.id == 42 && selected.unique_identity && selected.duration_ms == value.catalog_ms &&
            response.state == LyricState::Synced && response.lines && response.lines->size() == 2 &&
            response.lines->front().time_ms == 1000 && response.lines->back().time_ms == 200040;
        std::cout << "catalog duration fixture phone_ms=" << value.phone_ms << " catalog_ms=" << value.catalog_ms
                  << " selected_id=" << selected.id << " state=" << static_cast<int>(response.state)
                  << " recovered=" << recovered << '\n';
        recovered_all &= recovered;
    }
    check(recovered_all, "unique exact-identity catalog recovers both observed duration gaps without changing cue times");
}

void catalog_unique_identity_guards()
{
    auto phone = track(); phone.duration_ms = 288763;
    auto catalog = phone; catalog.duration_ms = 311000;
    JsonDocument document;
    auto records = document.to<JsonArray>();
    auto reset_records = [&] { records.clear(); catalog_record(records, catalog, 42); };
    auto select = [&] { return parse_lrclib_catalog_search(encode(document), phone); };
    reset_records();
    const auto selected = select();
    check(selected.valid && selected.id == 42 && selected.unique_identity, "unique duration fallback is explicitly marked");
    check(parse_response(200, response_body(catalog), phone).state == LyricState::NotFound,
          "unique catalog fallback does not weaken the original exact response parser");

    catalog_record(records, catalog, 42);
    check(select().id == 42 && select().unique_identity, "identical duplicate IDs do not prevent unique identity fallback");
    records[1]["duration"] = 311.001;
    check(select().valid && !select().id, "same fallback ID with conflicting duration is rejected");

    reset_records();
    auto other = catalog; other.duration_ms = 320000;
    catalog_record(records, other, 43);
    check(select().valid && !select().id, "different-distance IDs cannot use nearest-duration ranking outside the strict window");
    records[1]["hasSyncedLyrics"] = false;
    check(select().valid && !select().id, "a second exact identity without synced lyrics still blocks duration fallback");
    records[1]["hasSyncedLyrics"] = true; records[1]["instrumental"] = true;
    check(select().valid && !select().id, "a second exact instrumental identity still blocks duration fallback");

    for (const auto* field : {"trackName", "artistName", "albumName"}) {
        reset_records(); catalog_record(records, other, 43); records[1][field] = "A different identity";
        check(select().id == 42 && select().unique_identity, "unrelated catalog identities do not make the exact identity ambiguous");
        records.remove(1); records[0][field] = "A different identity";
        check(select().valid && !select().id, "duration fallback never relaxes any identity field");
    }
    for (const auto* suffix : {" (Live)", " - Remix", " (Instrumental)"}) {
        reset_records(); records[0]["trackName"] = phone.title + suffix;
        check(select().valid && !select().id, "duration fallback preserves recording-version suffixes");
    }
    reset_records(); records[0]["hasSyncedLyrics"] = false;
    check(select().valid && !select().id, "a unique plain-only recording cannot qualify for fallback");
    records[0]["hasSyncedLyrics"] = true; records[0]["instrumental"] = true;
    check(select().valid && !select().id, "a unique instrumental recording cannot qualify for fallback");

    reset_records();
    auto strict = phone; strict.duration_ms += 1000;
    catalog_record(records, strict, 43);
    check(select().id == 43 && !select().unique_identity, "strict candidate remains preferred despite wider catalog identity ambiguity");
    catalog_record(records, strict, 44);
    check(select().id == 43 && select().fallback_id == 44 && !select().unique_identity,
          "equivalent strict editions retain an alternative without selecting the wider recording");

    auto unmarked = selected; unmarked.unique_identity = false;
    check(parse_lrclib_catalog_lyrics(200, response_body(catalog), phone, unmarked).state == LyricState::Invalid,
          "unmarked candidates cannot bypass the original download duration guard");
    JsonDocument download; deserializeJson(download, response_body(catalog));
    auto parse = [&] { return parse_lrclib_catalog_lyrics(200, encode(download), phone, selected); };
    download["duration"] = 311.001;
    check(parse().state == LyricState::Synced, "fallback download allows one millisecond catalog conversion precision");
    download["duration"] = 311.002;
    check(parse().state == LyricState::NotFound, "fallback download rejects duration drift beyond the selected catalog record");
    download["duration"] = 311; download["id"] = 43;
    check(parse().state == LyricState::NotFound, "fallback download must retain the selected ID");
    download["id"] = 42;
    for (const auto* field : {"trackName", "artistName", "albumName"}) {
        const auto original = download[field].as<std::string>(); download[field] = "A changed identity";
        check(parse().state == LyricState::NotFound, "fallback download rechecks title artist and album");
        download[field] = original;
    }
    download["syncedLyrics"] = "[00:01.000]A quiet room wakes slowly\n[04:48.763]Sunlight rests on painted walls";
    auto response = parse();
    check(response.state == LyricState::Synced && response.lines && response.lines->back().time_ms == phone.duration_ms,
          "fallback accepts an unchanged final cue exactly at the phone duration");
    download["syncedLyrics"] = "[00:01.000]A quiet room wakes slowly\n[04:50.764]Sunlight rests on painted walls";
    response = parse();
    check(response.state == LyricState::Unsynced && response.lines && response.lines->back().time_ms == 290764,
          "fallback preserves late text but never labels it synced");
    download["syncedLyrics"] = "[00:01.000]A quiet room wakes slowly\n[00:04.000]Sunlight rests on painted walls\n[04:48.764]";
    response = parse();
    check(response.state == LyricState::Synced && response.lines && response.lines->back().time_ms == 288764,
          "empty final markers remain intact without disabling the compatible lyric timing");
    download["syncedLyrics"] = nullptr;
    check(parse().state == LyricState::NotFound, "fallback download cannot substitute plain-only lyrics");
    download["instrumental"] = true;
    check(parse().state == LyricState::NotFound, "fallback download cannot become instrumental");
}

void catalog_matching_boundaries()
{
    auto actual=track();actual.duration_ms=293016;
    auto catalog=actual;catalog.duration_ms=299000;
    const auto full=response_body(catalog);
    check(parse_response(200,full,actual).state==LyricState::NotFound,"original strict lookup still rejects 293016 versus 299000 ms");
    check(lrclib_catalog_search_url(actual)=="https://lrclib.net/api/v2/search?track_name=Window%20Light&artist_name=Botifier%20Test%20Artist&album_name=Original%20Demo","lightweight catalog URL uses all three identities without duration");
    JsonDocument document;auto records=document.to<JsonArray>();catalog_record(records,catalog,42);
    auto selected=parse_lrclib_catalog_search(encode(document),actual);
    check(selected.valid&&selected.id==42&&selected.duration_ms==299000&&selected.access_key.empty()&&!selected.unique_identity,"catalog supplies its verified duration without changing actual metadata");
    check(lrclib_catalog_lyric_url(selected)=="https://lrclib.net/api/get/42","catalog fetch uses only a validated absolute id");
    auto result=parse_lrclib_catalog_lyrics(200,full,actual,selected);
    check(result.state==LyricState::Synced&&result.lines&&result.lines->size()==2&&result.lines->front().time_ms==1000&&
          result.lines->back().time_ms==4500,"catalog duration compatibility preserves original LRC timestamps");
    check(parse_response(200,encode(document),actual).state==LyricState::Invalid,"legacy response parser remains object-only");
    auto closer=actual;closer.duration_ms=290000;catalog_record(records,closer,43);
    check(parse_lrclib_catalog_search(encode(document),actual).id==43,"catalog selects nearest recording rather than a fixed known id");
    for(const auto total:{100000u,333333u,400000u}) {
        auto phone=track();phone.duration_ms=total;
        const auto tolerance=std::min(10000u,total*3u/100u);
        for(const bool longer:{false,true}) {
            auto summary=phone;summary.duration_ms=longer?total+tolerance:total-tolerance;
            records.clear();catalog_record(records,summary,42);
            const auto strict=parse_lrclib_catalog_search(encode(document),phone);
            check(strict.id==42&&!strict.unique_identity,"catalog 3-percent / ten-second boundary inclusive on either side");
            summary.duration_ms=longer?summary.duration_ms+1:summary.duration_ms-1;
            records.clear();catalog_record(records,summary,42);
            const auto unique=parse_lrclib_catalog_search(encode(document),phone);
            check(unique.valid&&unique.id==42&&unique.unique_identity,"one millisecond beyond strict duration uses only the marked unique identity fallback");
            auto another=summary;another.duration_ms=longer?summary.duration_ms+1:summary.duration_ms-1;
            catalog_record(records,another,43);
            const auto rejected=parse_lrclib_catalog_search(encode(document),phone);
            check(rejected.valid&&!rejected.id,"two identities beyond the strict duration boundary remain rejected");
        }
    }
    records.clear();catalog=actual;catalog.duration_ms+=3000;catalog_record(records,catalog,42);
    catalog.duration_ms=actual.duration_ms-3000;catalog_record(records,catalog,43);
    check(parse_lrclib_catalog_search(encode(document),actual).valid&&!parse_lrclib_catalog_search(encode(document),actual).id,"equidistant different catalog ids rejected");
    records.clear();catalog_record(records,catalog,42);catalog_record(records,catalog,42);
    check(parse_lrclib_catalog_search(encode(document),actual).id==42,"same id with identical catalog duration may repeat");
    records[1]["duration"]=(catalog.duration_ms+1)/1000.0;
    check(parse_lrclib_catalog_search(encode(document),actual).valid&&!parse_lrclib_catalog_search(encode(document),actual).id,"same id with conflicting durations rejected instead of silently selecting one");
    for(const auto* field:{"trackName","artistName","albumName"}) {
        records.clear();catalog_record(records,catalog,42);records[0][field]="A different identity";
        const auto mismatch=parse_lrclib_catalog_search(encode(document),actual);
        check(mismatch.valid&&!mismatch.id,"catalog duration flexibility never relaxes title artist or album");
    }
    records.clear();catalog_record(records,catalog,42);records[0]["trackName"]=actual.title+" (Live)";
    check(!parse_lrclib_catalog_search(encode(document),actual).id,"catalog cannot strip recording-version suffixes");
    records[0]["trackName"]="  WINDOW   Light ";records[0]["artistName"]="botifier test artist";records[0]["albumName"]=" ORIGINAL   DEMO ";
    check(parse_lrclib_catalog_search(encode(document),actual).id==42,"catalog identity uses existing bounded case and space normalization");
    records.clear();catalog_record(records,catalog,42);records[0]["hasSyncedLyrics"]=false;
    check(parse_lrclib_catalog_search(encode(document),actual).valid&&!parse_lrclib_catalog_search(encode(document),actual).id,"plain-only catalog record cannot qualify");
    records[0]["hasSyncedLyrics"]=true;records[0]["instrumental"]=true;
    check(!parse_lrclib_catalog_search(encode(document),actual).id,"instrumental catalog record cannot qualify for compatibility");
    for(const auto& album:{std::string{},std::string("   "),std::string("x\0y",3)}) {
        auto missing=actual;missing.album=album;
        check(lrclib_catalog_search_url(missing).empty()&&!parse_lrclib_catalog_search(encode(document),missing).valid,"catalog compatibility requires a legitimate nonempty album");
    }
    for(const Candidate invalid:{Candidate{},Candidate{true,42,{},0},Candidate{true,0,{},299000},Candidate{false,42,{},299000},
        Candidate{true,UINT64_MAX,{},299000},Candidate{true,42,"foreign_key",299000}})
        check(lrclib_catalog_lyric_url(invalid).empty()&&parse_lrclib_catalog_lyrics(200,full,actual,invalid).state==LyricState::Invalid,"catalog candidate id duration and source fields validated");
}

void catalog_response_validation()
{
    auto phone=track();phone.duration_ms=293016;
    auto catalog=phone;catalog.duration_ms=299000;
    Candidate selected{true,42,{},299000};
    JsonDocument document;deserializeJson(document,response_body(catalog));
    auto parse=[&] {return parse_lrclib_catalog_lyrics(200,encode(document),phone,selected);};
    document["duration"]=299.001;
    check(parse().state==LyricState::Synced,"catalog download duration tolerates one millisecond conversion precision");
    document["duration"]=299.002;
    check(parse().state==LyricState::NotFound,"catalog download cannot change the selected duration by two milliseconds");
    document["duration"]=299;document["id"]=43;
    check(parse().state==LyricState::NotFound,"catalog download must retain selected id");
    document["id"]="42";check(parse().state==LyricState::Invalid,"catalog download id cannot be coerced from string");
    document["id"]=42;
    for(const auto* field:{"trackName","artistName","albumName"}) {
        auto original=document[field].as<std::string>();document[field]="A changed identity";
        check(parse().state==LyricState::NotFound,"catalog download rechecks every identity field");document[field]=original;
    }
    document["syncedLyrics"]="[00:01.00]First original phrase\n[04:53.016]Final original phrase";
    auto response=parse();
    check(response.state==LyricState::Synced&&response.lines&&response.lines->back().time_ms==phone.duration_ms,"catalog timeline may end exactly at actual phone duration");
    document["syncedLyrics"]="[00:01.00]First original phrase\n[04:53.017]Beyond the phone duration";
    response=parse();check(response.state==LyricState::Synced&&response.lines&&response.lines->back().time_ms==293017,
                          "catalog uses the same two-second tail tolerance as direct lyrics");
    document["syncedLyrics"]="[00:01.00]First original phrase\n[00:04.00]Second original phrase\n[04:53.017]";
    response=parse();check(response.state==LyricState::Synced&&response.lines&&response.lines->back().time_ms==293017,
                          "an empty trailing cue does not disable otherwise compatible synchronization");
    document["syncedLyrics"]="[00:01.00]First original phrase\n[04:55.017]Beyond the allowed phone tail";
    response=parse();check(response.state==LyricState::Unsynced&&response.lines,
                          "nonempty lyrics beyond the shared tail tolerance remain uncalibrated");
    document["syncedLyrics"]=nullptr;
    check(parse().state==LyricState::NotFound,"catalog download plain-only response is not a compatibility success");
    document["instrumental"]=true;
    check(parse().state==LyricState::NotFound,"catalog download instrumental response is not a compatibility success");
    for(const auto* value:{"-1","0","299.0e100","\"299\"","null"}) {
        document["duration"]=serialized(value);check(parse().state==LyricState::Invalid,"catalog download duration validates number type range and sign");
    }
    check(parse_lrclib_catalog_lyrics(429,"ignored",phone,selected,"321").retry_after_ms==321000,"catalog Retry-After preserved");
    check(parse_lrclib_catalog_lyrics(404,"ignored",phone,selected).state==LyricState::NotFound&&
          parse_lrclib_catalog_lyrics(503,"ignored",phone,selected).state==LyricState::NetworkError,"catalog HTTP status semantics preserved");
}

void catalog_document_validation()
{
    const auto song=track();JsonDocument document;auto records=document.to<JsonArray>();catalog_record(records,song,42);
    const auto good=encode(document);
    check(parse_lrclib_catalog_search("[]",song).valid&&!parse_lrclib_catalog_search("[]",song).id,"empty catalog remains a valid no-match");
    for(const auto& bad:{std::string("{}"),std::string("null"),good+" trailing",std::string("/* comment */")+good,
        std::string(65537,'x'),std::string("[{\"id\":42,\"id\":43}]"),std::string("[{\"x\":\"\\ud800\"}]"),
        std::string("[{\"x\":\"\xc0\xaf\"}]"),std::string("[{\"x\":[[[[[[]]]]]]}]")})
        check(!parse_lrclib_catalog_search(bad,song).valid,"catalog enforces root array strict framing duplicate keys UTF8 and nesting cap");
    for(const auto* field:{"trackName","artistName","albumName","hasSyncedLyrics","instrumental","duration","id"}) {
        deserializeJson(document,good);document[0].remove(field);
        check(!parse_lrclib_catalog_search(encode(document),song).valid,"catalog requires typed complete summary fields");
    }
    for(const auto* value:{"-1","0","1.0e100","\"90\"","true","null"}) {
        deserializeJson(document,good);document[0]["duration"]=serialized(value);
        check(!parse_lrclib_catalog_search(encode(document),song).valid,"catalog numeric duration rejects coercion overflow and missing values");
    }
    deserializeJson(document,good);document[0]["hasSyncedLyrics"]="true";
    check(!parse_lrclib_catalog_search(encode(document),song).valid,"catalog synced flag must be boolean");
    deserializeJson(document,good);records=document.as<JsonArray>();for(unsigned i=1;i<21;++i)catalog_record(records,song,42+i);
    check(!parse_lrclib_catalog_search(encode(document),song).valid,"catalog caps summary count at twenty records");
}

void engine_progress_and_lifecycle()
{
    Engine engine;
    Request request;
    check(!engine.take_request(10000, request), "disabled default");
    connected(engine);
    metadata(engine, track(), 1100);
    check(!engine.take_request(1899, request) && engine.take_request(1900, request), "metadata settle interval");
    check(!engine.take_request(3000, request), "one lookup in flight");
    const auto original = request;
    check(engine.attribute(0, 1, "1,1,10", 2000), "playing starts");
    check(engine.snapshot(4500).position_ms == 12500, "position advances from local clock");
    check(engine.attribute(0, 1, "0,1,12.5", 4500), "pause event");
    check(engine.snapshot(20000).position_ms == 12500 && engine.snapshot(20000).paused_since_ms == 4500, "pause stable position and timestamp");
    check(engine.attribute(0, 1, "1,1,30", 21000), "seek event");
    check(engine.snapshot(21500).position_ms == 30500, "seek corrects anchor immediately");
    check(engine.attribute(0, 1, "1,2,30", 22000) && engine.snapshot(23000).position_ms == 32000, "non-unit playback rate");
    check(engine.attribute(0, 1, "2,-1,10", 24000) && engine.snapshot(39000).position_ms == 0, "rewind clamps at zero");
    check(engine.attribute(0, 1, "1,1,89", 40000) && engine.snapshot(50000).position_ms == 90000, "elapsed clamp at duration");
    auto synced = parse_response(200, response_body(track()), track());
    synced.phase = 4; synced.elapsed_ms = 12000;
    check(engine.complete(original, std::move(synced), 51000), "current result publish");
    const auto first = engine.snapshot(51000), second = engine.snapshot(51001);
    check(first.lines && first.lines == second.lines && first.state == LyricState::Synced, "snapshot retains immutable shared lines");
    check(first.phase == 4 && first.elapsed_ms == 12000, "same-generation response diagnostics published");
    engine.invalidate_track(52000);
    check(!engine.snapshot(52000).lines && !engine.snapshot(52000).track_ready && !engine.current(original), "switch immediately discards old lyrics");
    check(engine.snapshot(52000).phase == 0 && engine.snapshot(52000).elapsed_ms == 0, "switch clears old diagnostic phase and timing");
    check(!engine.complete(original, parse_response(200, response_body(track()), track()), 53000), "old HTTP result cannot reappear");
    auto song = track(); song.title = "Second Window";
    metadata(engine, song, 53000);
    check(engine.take_request(53800, request), "new song lookup");
    engine.link(LinkState::Pairing, 0, 54000);
    check(!engine.snapshot(54000).lines && !engine.snapshot(54000).connected && !engine.current(request), "disconnect expires song and request");
    engine.link(LinkState::Ready, 0, 55000);
    check(!engine.take_request(60000, request), "reconnect requires fresh metadata");
    metadata(engine, song, 61000);
    check(engine.take_request(61800, request), "reconnect fresh track");
    engine.set_enabled(false, 62000);
    check(!engine.current(request) && !engine.snapshot(62000).lines && engine.snapshot(62000).link == LinkState::Off, "disable drops result and link");
    check(!engine.attribute(2, 2, "Ignored", 63000), "disabled attribute ignored");
    engine.link(LinkState::Ready, 0, 63000);
    check(!engine.snapshot(63000).connected, "late BLE callback cannot re-enable service");
}

void engine_success_cache()
{
    const auto first = track();
    auto second = first; second.title = "Second song";
    auto third = first; third.title = "Third song";
    Engine settling; connected(settling);
    metadata(settling, first, 1100);
    metadata(settling, first, 1500);
    settling.attribute(2, 2, first.title, 1800);
    Request initial;
    check(settling.take_request(1900, initial), "repeated unchanged metadata cannot indefinitely postpone initial lookup");
    std::uint64_t now = 2000;
    auto warm = [&](Engine& engine, const Track& song, Response response) {
        metadata(engine, song, now);
        Request request;
        check(engine.take_request(now + 800, request), "uncached signature starts one lookup");
        check(engine.complete(request, std::move(response), now + 900), "current response completes");
        now += 2000;
    };
    Engine engine; connected(engine);
    warm(engine, first, parse_response(200, response_body(first), first));
    const auto shared = engine.snapshot(now).lines;
    metadata(engine, first, now);
    Request request;
    check(engine.snapshot(now).track_ready && !engine.take_request(now + 800, request), "same complete metadata keeps readiness and successful lookup");
    check(engine.attribute(2, 0, first.artist, now + 1) && engine.snapshot(now + 1).track_ready,
          "unchanged attribute cannot restart metadata settling");
    engine.invalidate_track(now);
    check(!engine.snapshot(now).lines, "invalidation clears display even when song is cached");
    metadata(engine, first, now + 1);
    check(engine.snapshot(now + 1).state == LyricState::Synced && engine.snapshot(now + 1).lines == shared &&
          engine.snapshot(now + 1).track_ready && !engine.take_request(now + 900, request), "complete cached identity restores immediately without another request");
    warm(engine, second, parse_response(200, response_body(second), second));
    metadata(engine, first, now);
    check(engine.snapshot(now).lines == shared && !engine.take_request(now + 800, request), "return to previous song shares immutable cached lyrics");
    warm(engine, third, parse_response(200, response_body(third), third));
    metadata(engine, second, now);
    check(!engine.snapshot(now).lines && engine.take_request(now + 800, request), "third song evicts least recently used of two cache entries");

    Engine reconnect; connected(reconnect);
    warm(reconnect, first, parse_response(200, response_body(first), first));
    reconnect.link(LinkState::Pairing, 0, now);
    check(!reconnect.snapshot(now).lines, "disconnected cache never stays visible");
    reconnect.link(LinkState::Ready, 0, now + 1);
    check(!reconnect.snapshot(now + 1).lines && !reconnect.snapshot(now + 1).track_ready, "reconnect waits for complete identity");
    metadata(reconnect, first, now + 2);
    check(reconnect.snapshot(now + 2).state == LyricState::Synced && !reconnect.take_request(now + 900, request), "reconnect same song reuses cache");
    reconnect.set_enabled(false, now + 1000);
    check(!reconnect.snapshot(now + 1000).lines, "disabled cached song is hidden");
    connected(reconnect, now + 1001);
    check(!reconnect.snapshot(now + 1001).lines, "enable alone cannot display cached song");
    reconnect.attribute(2, 0, first.artist, now + 1002);
    reconnect.attribute(2, 1, first.album, now + 1003);
    reconnect.attribute(2, 2, first.title, now + 1004);
    check(!reconnect.snapshot(now + 1004).lines, "partial cached signature cannot restore lyrics");
    reconnect.attribute(2, 3, "90", now + 1005);
    check(reconnect.snapshot(now + 1005).state == LyricState::Synced && !reconnect.take_request(now + 2000, request), "four complete fields restore cached song");

    for (unsigned field = 0; field < 5; ++field) {
        Engine distinct; connected(distinct);
        warm(distinct, first, parse_response(200, response_body(first), first));
        auto changed = first;
        if (field == 0) changed.title += " changed";
        else if (field == 1) changed.artist += " changed";
        else if (field == 2) changed.album += " changed";
        else if (field == 3) ++changed.duration_ms;
        else changed.player += " changed";
        metadata(distinct, changed, now);
        check(!distinct.snapshot(now).lines && distinct.take_request(now + 800, request), "cache signature includes every exact Track field");
    }
    Engine stale; connected(stale);
    metadata(stale, first, now);
    check(stale.take_request(now + 800, request), "stale cache setup request");
    metadata(stale, second, now + 900);
    check(!stale.complete(request, parse_response(200, response_body(first), first), now + 1000), "stale success cannot populate cache");
    metadata(stale, first, now + 1100);
    check(!stale.snapshot(now + 1100).lines && stale.take_request(now + 2000, request), "rejected stale success requires fresh lookup");

    for (auto state : {LyricState::NotFound, LyricState::NetworkError, LyricState::Invalid}) {
        Engine failed; connected(failed);
        Response response; response.state = state;
        warm(failed, first, response);
        failed.invalidate_track(now);
        metadata(failed, first, now + 1);
        check(failed.take_request(now + 900, request), "missing or failed response never cached");
    }
    Engine instrumental; connected(instrumental);
    Response silence; silence.state = LyricState::Instrumental;
    warm(instrumental, first, silence);
    instrumental.invalidate_track(now); metadata(instrumental, first, now + 1);
    check(instrumental.snapshot(now + 1).state == LyricState::Instrumental && !instrumental.snapshot(now + 1).lines &&
          !instrumental.take_request(now + 900, request), "instrumental success is cached without lyric lines");

    Engine bounded; connected(bounded);
    auto large_response = [] {
        Response response; response.state = LyricState::Synced;
        auto lines = std::make_shared<std::vector<Line>>();
        for (unsigned i = 0; i < 512; ++i) lines->push_back({i * 100, std::string(100, 'a')});
        response.lines = std::move(lines); return response;
    };
    warm(bounded, first, large_response());
    warm(bounded, second, large_response());
    metadata(bounded, first, now);
    check(!bounded.snapshot(now).lines && bounded.take_request(now + 800, request), "combined large timelines evict older entry at RAM payload bound");
}

void metadata_orders_and_retries()
{
    int order[] = {0, 1, 2, 3};
    unsigned permutations = 0;
    do {
        Engine engine; connected(engine);
        const std::string fields[] = {"Botifier Test Artist", "", "Window Light", "90"};
        for (unsigned i = 0; i < 4; ++i) check(engine.attribute(2, static_cast<std::uint8_t>(order[i]), fields[order[i]], 1100 + i), "unordered fresh Track attribute");
        Request request;
        check(engine.take_request(2000, request) && request.track.album.empty(), "four field permutation including empty album completes");
        check(engine.attribute(2, 2, "New title", 2100), "changed title event");
        check(!engine.current(request) && !engine.snapshot(2200).lines, "single changed field invalidates lookup");
        check(!engine.take_request(5000, request), "partial metadata never mixed with previous song");
        check(engine.attribute(2, 0, fields[0], 5100) && engine.attribute(2, 1, "", 5101) && engine.attribute(2, 3, "90", 5102), "unchanged fields refresh normally");
        check(engine.take_request(6000, request) && request.track.title == "New title", "complete refreshed song ready");
        ++permutations;
    } while (std::next_permutation(std::begin(order), std::end(order)));
    check(permutations == 24, "all field arrival permutations covered");

    Engine engine; connected(engine);
    const auto song = track(); metadata(engine, song, 1100);
    Request request;
    check(engine.attribute(1, 0, "3", 1200) && engine.take_request(2000, request), "initial queue index does not clear first song");
    check(engine.attribute(1, 0, "4", 2100) && !engine.current(request), "same song on new queue item invalidates");
    metadata(engine, song, 2200);
    check(engine.take_request(3000, request), "same title replay has new generation");
    check(engine.complete(request, parse_response(429, "", song, "120"), 3100), "rate limit published");
    check(!engine.take_request(123099, request) && engine.take_request(123100, request), "rate-limit deadline enforced");
    check(engine.complete(request, parse_response(500, "", song), 123200), "second transient failure");
    check(!engine.take_request(153199, request) && engine.take_request(153200, request), "bounded network backoff");
    check(engine.complete(request, parse_response(500, "", song), 154000) && !engine.take_request(9999999, request), "three attempts then stop until song changes");
    engine.invalidate_track(10000000);
    auto bad = song; bad.artist.clear();
    check(!engine.track_snapshot(bad, 10000001) && !engine.snapshot(10001000).track_ready, "missing artist atomic batch rejected");
    bad = song; bad.duration_ms = 0;
    check(!engine.track_snapshot(bad, 10000001), "missing duration atomic batch rejected");
    bad = song; bad.title.assign("x\0y", 3);
    check(!engine.track_snapshot(bad, 10000001), "batch UTF8 validation");
    check(!engine.attribute(1, 0, "4294967296", 10000001), "queue overflow rejected");
    check(!engine.attribute(2, 3, "NaN", 10000001) && !engine.attribute(2, 2, "   ", 10000001), "malformed required metadata rejected");

    // Match the transport's full-read sequence: Player Name, atomic Track,
    // PlaybackInfo. A player switch must not permanently freeze the new song.
    Engine ordered; connected(ordered, 1000);
    auto unnamed = song; unnamed.player.clear();
    metadata(ordered, unnamed, 1100);
    check(ordered.attribute(0, 1, "1,1,7", 1200) && ordered.snapshot(2200).playing &&
          ordered.snapshot(2200).position_ms == 8000 && ordered.snapshot(2200).track.player == "Spotify", "initial batch then playback stays live");
    check(ordered.attribute(0, 0, "New Player", 2300) && !ordered.snapshot(2300).track_ready, "player change invalidates old track");
    metadata(ordered, unnamed, 2400);
    check(ordered.attribute(0, 1, "1,1,20", 2500) && ordered.snapshot(3500).position_ms == 21000 &&
          ordered.snapshot(3500).track.player == "New Player", "fresh player batch and playback resume with correct identity");
}
void provisional_title_failure_budget()
{
    for (const auto failure : {LyricState::NotFound, LyricState::Invalid}) {
        auto phone = track(); phone.player = std::string(utf8(u8"网易云音乐"));
        phone.artist += '/' + phone.title; phone.title = "Invented first phrase";
        Engine engine; connected(engine); metadata(engine, phone, 1100);
        check(engine.attribute(0, 1, "1,1,1", 1100), "phone failure playback anchor");
        Request request; check(engine.take_request(2000, request) && request.phone_metadata, "start provisional lookup");
        phone.title = "Invented next phrase"; metadata(engine, phone, 2200);
        Response response; response.state = failure; response.http_status = 200;
        response.phase = 4; response.elapsed_ms = 1000; response.error = 7;
        check(engine.complete(request, response, 3000), "failed provisional lookup retains its generation across title updates");
        for (std::uint64_t now = 3500; now <= 303500; now += 500) {
            phone.title = "Invented changing phrase " + std::to_string(now);
            metadata(engine, phone, now);
            const auto snap = engine.snapshot(now);
            check(snap.generation == request.generation && snap.track_ready && snap.playing &&
                  snap.state == failure && !snap.lines && snap.http_status == 200 && snap.phase == 4 &&
                  snap.elapsed_ms == 1000 && snap.last_error == 7 && !engine.take_request(now, request),
                  "five-minute title stream preserves terminal failure, diagnostics and attempt budget");
        }
        phone.album += " changed"; metadata(engine, phone, 304000);
        check(engine.take_request(304800, request) && request.phone_metadata,
              "changed stable identity can start a fresh source probe");
    }
}

void phone_alias_and_stable_search()
{
    const auto source = track();
    auto phone = source; phone.player = std::string(utf8(u8"网易云音乐"));
    phone.artist += '/' + source.title + " (Original screen description)";
    phone.title = "An invented current phrase";
    JsonDocument document; document["code"] = 200;
    auto songs = document["result"]["songs"].to<JsonArray>(); netease_song(songs, source, 42);
    auto item = songs[0];
    const auto select = [&] { return parse_netease_search(encode(document), phone); };
    check(!select().id, "unproven phone suffix remains rejected");
    item["alias"].to<JsonArray>().add("Original screen description");
    auto candidate = select();
    check(candidate.id == 42 && candidate.phone_identity && !candidate.phone_canonical_title,
          "source alias reconstructs the complete phone signature without assuming current title");
    phone.title = source.title;
    check(select().phone_canonical_title, "exact source canonical title proves an intro title");
    phone.title += " (Live)";
    check(!select().phone_canonical_title, "canonical proof does not remove version suffixes");
    const auto ordinary_url = netease_search_url(phone);
    const auto stable_url = netease_search_url(phone, true);
    check(stable_url == "https://music.163.com/api/search/get?s=" + percent_encode(phone.album) + "&type=1&limit=8&offset=0",
          "phone search uses the bounded stable album as its exact query seed");
    phone.title = "Another invented current phrase";
    check(netease_search_url(phone, true) == stable_url && netease_search_url(phone) != ordinary_url,
          "phone title changes leave the source query stable while ordinary queries retain title");
    auto missing_album = phone; missing_album.album = "   ";
    check(netease_search_url(missing_album, true).empty() && !netease_search_url(missing_album).empty(),
          "empty normalized album has no phone query but retains ordinary search");
    missing_album.album.assign(kMaxAttributeBytes + 1, 'x');
    check(netease_search_url(missing_album, true).empty(), "phone search album remains bounded");
    auto slash = source; slash.artist = "AC/DC";
    check(netease_search_url(slash) == "https://music.163.com/api/search/get?s=" +
          percent_encode(slash.title + " " + slash.artist) + "&type=1&limit=8&offset=0",
          "ordinary slash artist query is unchanged");
    for (const auto bad : {"Other screen description", "Original screen description (Live)"}) {
        item["alias"][0] = bad;
        check(!select().id, "alias proof requires the full source-provided description");
    }
    item["alias"] = "Original screen description";
    check(!select().id, "alias must be a string array");
    auto aliases = item["alias"].to<JsonArray>(); aliases.add("Original screen description");
    for (unsigned i = 0; i < 8; ++i) aliases.add("extra");
    check(!select().id, "alias array bound is enforced even if its first element matches");
    aliases.clear(); aliases.add(std::string("Original screen description\0bad", 31));
    check(!select().id, "alias proof rejects embedded control bytes");
    aliases.clear(); aliases.add(9);
    check(!select().id, "alias proof ignores a non-string element");
    aliases.clear(); aliases.add("Original screen description");
    for (unsigned field = 0; field < 3; ++field) {
        const auto valid = phone;
        if (field == 0) phone.artist += " (Live)";
        if (field == 1) phone.album += " changed";
        if (field == 2) phone.duration_ms += 2001;
        check(!select().id, "alias support retains full artist album and duration guards");
        phone = valid;
    }
    check(!parse_netease_search(encode(document), phone, false).id,
          "ordinary requests cannot select a compound-only source alias");
}

void phone_content_and_canonical_proof()
{
    const auto response = [] {
        Response value; value.state = LyricState::Synced; value.phone_identity = true;
        value.http_status = 200; value.phase = 4; value.elapsed_ms = 500;
        value.lines = std::make_shared<const std::vector<Line>>(std::vector<Line>{
            {60000, "An invented opening phrase"}, {80000, "An invented closing phrase"}});
        return value;
    };
    auto phone = track(); phone.player = std::string(utf8(u8"网易云音乐"));
    phone.artist += '/' + phone.title;
    const auto initial = phone;
    check(phone_title_in_lyrics("An invented opening phrase", *response().lines) &&
          !phone_title_in_lyrics("invented opening", *response().lines) &&
          !phone_title_in_lyrics(" ", *response().lines), "content proof requires a complete nonempty lyric line");
    for (const bool aligned : {false, true}) {
        Engine engine; connected(engine); phone = initial; phone.title = "An invented opening phrase";
        metadata(engine, phone, 1100); engine.attribute(0, 1, aligned ? "1,1,61" : "1,1,5", 1900);
        Request request; check(engine.take_request(2000, request), "prepare phone content proof");
        auto lyrics = response();
        check(engine.complete(request, lyrics, 2100), "source-proven lyric content accepted");
        const auto published = engine.snapshot(2100);
        check(published.phone_lyrics && published.lines == lyrics.lines &&
              published.state == (aligned ? LyricState::Synced : LyricState::Unsynced),
              "whole-line content at another cue is visible only as unsynced text");
        for (std::uint64_t now = 2600; now < 182600; now += 500) {
            phone.title = "Changing phone phrase " + std::to_string(now); metadata(engine, phone, now);
            check(engine.snapshot(now).generation == request.generation && engine.snapshot(now).lines == lyrics.lines &&
                  !engine.take_request(now, request), "verified recording retains lyrics and budget across title flow");
        }
    }
    {
        Engine engine; connected(engine); phone = initial; metadata(engine, phone, 1100);
        engine.attribute(0, 1, "1,1,0", 1900);
        Request request; check(engine.take_request(2000, request), "prepare canonical long intro");
        auto lyrics = response(); lyrics.phone_canonical_title = true;
        check(engine.complete(request, lyrics, 2100) && engine.snapshot(2100).phone_lyrics &&
              engine.snapshot(2100).lines == lyrics.lines, "canonical source title verifies before a sixty-second intro ends");
        engine.advance(62000);
        check(engine.snapshot(62000).generation == request.generation && engine.snapshot(62000).lines == lyrics.lines &&
              !engine.take_request(62000, request), "canonical proof survives the old forty-five second deadline");
    }
    for (const bool stale_canonical : {false, true}) {
        Engine engine; connected(engine); phone = initial; metadata(engine, phone, 1100);
        Request request; check(engine.take_request(2000, request), "prepare unproven title");
        auto lyrics = response(); lyrics.phone_canonical_title = stale_canonical;
        if (stale_canonical) { phone.title = "Unrelated title after request"; metadata(engine, phone, 2050); }
        check(engine.complete(request, lyrics, 2100) && !engine.snapshot(2100).lines &&
              !engine.snapshot(2100).phone_lyrics, "source signature and stale canonical proof cannot expose unproven content");
        engine.advance(47000);
        auto expired = engine.snapshot(47000);
        check(expired.generation == request.generation && expired.state == LyricState::NotFound && !expired.lines &&
              expired.http_status == 200 && expired.phase == 4 && expired.elapsed_ms == 500,
              "title-proof timeout retains the stable generation and source diagnostics");
        phone.title = "A later unrelated phrase"; metadata(engine, phone, 48000);
        check(engine.snapshot(48000).generation == request.generation && !engine.take_request(600000, request),
              "expired content proof cannot restart searches through later lyric titles");
    }
    {
        Engine engine; connected(engine); phone = initial; metadata(engine, phone, 1100);
        Request request; check(engine.take_request(2000, request), "prepare bad source proof");
        auto lyrics = response(); lyrics.phone_identity = false; lyrics.phone_canonical_title = true;
        check(engine.complete(request, lyrics, 2100) && engine.snapshot(2100).state == LyricState::Invalid &&
              !engine.snapshot(2100).lines && !engine.snapshot(2100).phone_lyrics,
              "canonical flag without complete phone source identity is invalid");
    }
    for (unsigned field = 0; field < 5; ++field) {
        Engine engine; connected(engine); phone = initial; metadata(engine, phone, 1100);
        engine.attribute(1, 0, "1", 1200);
        Request request; check(engine.take_request(2000, request), "prepare verified stable identity");
        auto lyrics = response(); lyrics.phone_canonical_title = true;
        check(engine.complete(request, lyrics, 2100), "complete canonical source proof");
        if (field == 0) phone.artist += " changed";
        if (field == 1) phone.album += " changed";
        if (field == 2) ++phone.duration_ms;
        if (field == 3) phone.player = "Other player";
        if (field == 4) engine.attribute(1, 0, "2", 2200);
        else metadata(engine, phone, 2200);
        check(engine.snapshot(2200).generation != request.generation && !engine.snapshot(2200).lines &&
              !engine.snapshot(2200).phone_lyrics, "real stable identity or queue changes clear verified phone lyrics");
    }
    {
        Engine engine; connected(engine); phone = initial; phone.artist = "AC/DC"; metadata(engine, phone, 1100);
        Request request; check(engine.take_request(2000, request), "prepare ordinary slash artist");
        auto lyrics = response(); lyrics.phone_identity = false;
        check(engine.complete(request, lyrics, 2100) && engine.snapshot(2100).lines &&
              !engine.snapshot(2100).phone_lyrics, "successful ordinary slash artist remains ordinary");
        phone.title = "Another real recording"; metadata(engine, phone, 2200);
        check(engine.snapshot(2200).generation != request.generation && !engine.snapshot(2200).lines &&
              engine.take_request(3000, request) && !request.phone_metadata,
              "ordinary slash artist still invalidates on an actual changed song title");
    }
}

void resilient_catalog_and_phone_metadata()
{
    const auto song = track();
    for (const bool kugou : {false, true}) {
        JsonDocument document; document[kugou ? "status" : "code"] = 200;
        auto songs = kugou ? document["candidates"].to<JsonArray>() : document["result"]["songs"].to<JsonArray>();
        auto add = [&](const Track& t, std::uint64_t id) { if (kugou) kugou_song(songs, t, id); else netease_song(songs, t, id); };
        auto parse = [&] { return kugou ? parse_kugou_search(encode(document), song) : parse_netease_search(encode(document), song); };
        add(song, 1); add(song, 42);
        songs[0][kugou ? "song" : "name"] = std::string("bad\0name", 8);
        check(parse().id == 42, "one malformed metadata row cannot poison a valid provider result");
        songs.remove(1);
        check(!parse().valid, "a response containing only malformed candidates is still invalid");
        for (const auto* tag : {" (Album Version)", " (album ver.)", "（唱片版）", " (专辑版)"}) {
            songs.clear(); auto edition = song; edition.title += tag; add(edition, 42);
            check(parse().id == 42, "album edition label may match only with corroborating album and duration");
            if (kugou) songs[0]["album"] = "Other recording";
            else songs[0]["album"]["name"] = "Other recording";
            check(!parse().id, "edition title cannot cross albums");
        }
        for (const auto* tag : {" (Live)", " (Remix)", " (Acoustic)", " (Re-recorded)", " (Instrumental)"}) {
            songs.clear(); auto edition = song; edition.title += tag; add(edition, 42);
            check(!parse().id, "different recording versions are not stripped even with equal album and duration");
        }
    }
    JsonDocument catalog;
    auto records = catalog.to<JsonArray>();
    catalog_record(records, song, 1); catalog_record(records, song, 42);
    records[0]["trackName"] = std::string("bad\0title", 9);
    check(parse_lrclib_catalog_search(encode(catalog), song).id == 42, "bad LRCLIB row does not invalidate nineteen healthy rows");

    JsonDocument document; document["code"] = 200;
    auto songs = document["result"]["songs"].to<JsonArray>();
    auto compilation = song; compilation.album = "Compilation";
    auto single = song; single.album = "Single";
    netease_song(songs, compilation, 80); netease_song(songs, single, 42);
    auto candidate = parse_netease_search(encode(document), song);
    check(candidate.id == 42 && candidate.fallback_id == 80 && candidate.duration_ms == song.duration_ms,
          "equivalent editions keep deterministic primary and one bounded alternative");
    songs[0]["duration"] = song.duration_ms - 500; songs[1]["duration"] = song.duration_ms + 500;
    check(!parse_netease_search(encode(document), song).id, "equally distant distinct recording lengths remain ambiguous");

    songs.clear(); netease_song(songs, song, 42);
    songs[0]["transNames"].to<JsonArray>().add("OT: Original Demo");
    auto phone = song; phone.player = std::string(utf8(u8"网易云音乐"));
    phone.title = "A short invented lyric";
    phone.artist = song.artist + "/" + song.title + " (OT: Original Demo)";
    candidate = parse_netease_search(encode(document), phone);
    check(candidate.id == 42 && candidate.phone_identity, "source fields reconstruct complete phone signature without splitting artists");
    auto mismatch = phone; mismatch.player = "Spotify";
    check(!parse_netease_search(encode(document), mismatch).id, "compound input compatibility is not guessed for another player");
    mismatch = phone; mismatch.album = "Other album";
    check(!parse_netease_search(encode(document), mismatch).id, "compound signature requires the exact album");
    mismatch = phone; mismatch.duration_ms += 2001;
    check(!parse_netease_search(encode(document), mismatch).id, "compound signature cannot relax duration");
    mismatch = phone; mismatch.artist += " (Live)";
    check(!parse_netease_search(encode(document), mismatch).id, "unverified phone suffix is not discarded");
    songs[0]["transNames"].clear();
    check(!parse_netease_search(encode(document), phone).id, "translated title credit must be supplied by the provider");
    songs.clear(); auto slash = song; slash.artist = "AC/DC"; netease_song(songs, slash, 77);
    phone.artist = "AC/DC/" + slash.title;
    candidate = parse_netease_search(encode(document), phone);
    check(candidate.id == 77 && candidate.phone_identity, "source reconstruction preserves slash inside AC/DC");
    slash.player = phone.player;
    candidate = parse_netease_search(encode(document), slash);
    check(candidate.id == 77 && !candidate.phone_identity, "ordinary AC/DC metadata remains ordinary");

    const std::vector<Line> lines{{10000, "A short invented lyric"}, {10000, "另一种文字"}, {20000, "The following line"}};
    check(phone_title_matches("A short invented lyric", lines, 14000), "current phone title can be confirmed against the active lyric group");
    check(phone_title_matches("另一种文字", lines, 14000), "same-time translation also verifies the phone line");
    check(!phone_title_matches("invented", lines, 14000), "a substring is not phone lyric verification");
    check(!phone_title_matches("A short invented lyric", lines, 22000), "an old verse outside notification skew cannot verify current title");
    check(phone_title_matches("The following line", lines, 19500), "bounded early BLE title delivery accepted");
    check(!phone_title_matches("The following line", lines, 19000), "future lyric beyond early-delivery bound rejected");
}
} // namespace

int main()
{
    try {
        provisional_title_failure_budget();
        phone_alias_and_stable_search(); phone_content_and_canonical_proof();
        resilient_catalog_and_phone_metadata();
        unsynced_timeline_fallback(); utf8_and_playback(); lrc_boundaries(); response_validation();
        fallback_urls_and_legacy_boundaries();fallback_candidate_matching();fallback_candidate_validation();fallback_lyric_validation();
        catalog_unique_identity_recovery();catalog_unique_identity_guards();catalog_matching_boundaries();catalog_response_validation();catalog_document_validation();
        han_metadata_compatibility(); kugou_lyric_quality();
        engine_progress_and_lifecycle(); engine_success_cache(); metadata_orders_and_retries();
        std::cout << "lyrics protocol: " << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "lyrics protocol failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
