#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "host/ble_hs.h"
#include "apps/common/lyrics/service.h"
constexpr int BLE_HS_ENOMEM = 11;

namespace fake {
struct Pending {
    int kind = 0;
    std::uint16_t conn = 0, first = 0, last = 0;
    ble_gatt_disc_svc_fn* svc = nullptr;
    ble_gatt_chr_fn* chr = nullptr;
    ble_gatt_dsc_fn* dsc = nullptr;
    ble_gatt_attr_fn* attr = nullptr;
    void* arg = nullptr;
    std::vector<unsigned char> bytes;
} pending;
ble_npl_eventq queue{};
ble_gap_conn_desc peer{7, {0,{1,2,3,4,5,6}}, {true,true}};
lyrics::LinkState link = lyrics::LinkState::Off;
bool media = false, playing = false, track_ready = false;
int initialized = 0, deleted = 0, disconnected = 0, committed = 0, invalidated = 0;
int next_error = 0, last_error = 0;
int service_discoveries = 0;
std::vector<std::string> publications;
std::vector<lyrics::Track> tracks;
ble_addr_t saved{};
bool have_saved = false;
std::vector<ble_addr_t> bonds;
const ble_addr_t legacy_phone{0,{9,8,7,6,5,4}};
bool same(const ble_addr_t& a,const ble_addr_t& b) { return a.type==b.type && std::memcmp(a.val,b.val,6)==0; }
bool bonded(const ble_addr_t& a) { for(const auto& b:bonds)if(same(a,b))return true;return false; }
void persist_phone_bond() { if(!bonded(peer.peer_id_addr))bonds.push_back(peer.peer_id_addr); }
int request(Pending p) { pending = p; const int rc = next_error; next_error = 0; return rc; }
void drain() { while (queue.event) { auto* e = queue.event; queue.event = nullptr; e->queued = false; e->fn(e); } }
void write_done() { auto p = pending; pending = {}; ble_gatt_error ok{0}; assert(p.kind == 4); p.attr(p.conn,&ok,nullptr,p.arg); }
void attribute_error(int error) { auto p=pending;pending={};assert(p.kind==4 || p.kind==5);ble_gatt_error err{error};p.attr(p.conn,&err,nullptr,p.arg); }
void read_value(const std::string& text) {
    auto p = pending; assert(p.kind == 5); pending = {};
    os_mbuf mb{text.data(),text.size()}; ble_gatt_attr a{p.first,0,&mb}; ble_gatt_error ok{0}, done{BLE_HS_EDONE};
    p.attr(p.conn,&ok,&a,p.arg); p.attr(p.conn,&done,nullptr,p.arg);
}
}
namespace lyrics {
void on_link(LinkState value,int error) { fake::link = value; fake::last_error = error; if (value != LinkState::Ready) { fake::playing=false;fake::track_ready=false; } }
void on_attribute(std::uint8_t e,std::uint8_t a,std::string_view v) {
    if (fake::link != LinkState::Ready) return;
    fake::publications.push_back(std::to_string(e)+":"+std::to_string(a)+":"+std::string(v));
    if (e == 0 && a == 1) fake::playing = v.substr(0,2) == "1,";
}
void on_track_invalidated() { ++fake::invalidated; fake::track_ready = false; fake::playing = false; }
void on_track_snapshot(const Track& t) { fake::tracks.push_back(t); fake::track_ready = true; }
}
extern "C" {
int ble_prph_ensure_initialized() { ++fake::initialized; return 0; }
int ble_prph_media_mode(bool enabled) { fake::media = enabled; return 0; }
int ble_gap_conn_find(std::uint16_t conn,ble_gap_conn_desc* out) { if (conn != fake::peer.conn_handle) return BLE_HS_ENOTCONN; *out = fake::peer; return 0; }
int ble_gap_terminate(std::uint16_t,int) { ++fake::disconnected; return 0; }
int ble_gap_security_initiate(std::uint16_t) { return 0; }
int ble_gattc_disc_all_svcs(std::uint16_t c,ble_gatt_disc_svc_fn* cb,void* arg) { ++fake::service_discoveries;fake::Pending p; p.kind=1;p.conn=c;p.svc=cb;p.arg=arg;return fake::request(p); }
int ble_gattc_disc_all_chrs(std::uint16_t c,std::uint16_t first,std::uint16_t last,ble_gatt_chr_fn* cb,void* arg) { fake::Pending p; p.kind=2;p.conn=c;p.first=first;p.last=last;p.chr=cb;p.arg=arg;return fake::request(p); }
int ble_gattc_disc_all_dscs(std::uint16_t c,std::uint16_t first,std::uint16_t last,ble_gatt_dsc_fn* cb,void* arg) { fake::Pending p; p.kind=3;p.conn=c;p.first=first;p.last=last;p.dsc=cb;p.arg=arg;return fake::request(p); }
int ble_gattc_write_flat(std::uint16_t c,std::uint16_t h,const void* data,std::uint16_t length,ble_gatt_attr_fn* cb,void* arg) { fake::Pending p; p.kind=4;p.conn=c;p.first=h;p.attr=cb;p.arg=arg;p.bytes.assign(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+length);return fake::request(p); }
int ble_gattc_read_long(std::uint16_t c,std::uint16_t h,std::uint16_t,ble_gatt_attr_fn* cb,void* arg) { fake::Pending p; p.kind=5;p.conn=c;p.first=h;p.attr=cb;p.arg=arg;return fake::request(p); }
int ble_store_util_delete_peer(const ble_addr_t* a) {
    assert(fake::same(*a,fake::peer.peer_id_addr));
    for(auto it=fake::bonds.begin();it!=fake::bonds.end();)if(fake::same(*a,*it))it=fake::bonds.erase(it);else ++it;
    ++fake::deleted;return 0;
}
void ble_npl_event_init(ble_npl_event* e,void (*fn)(ble_npl_event*),void* arg) { *e={fn,arg,false}; }
void ble_npl_eventq_put(ble_npl_eventq* q,ble_npl_event* e) { if (!e->queued) { assert(q->event == nullptr); e->queued=true; q->event=e; } }
int ble_npl_callout_init(ble_npl_callout* c,ble_npl_eventq*,void (*fn)(ble_npl_event*),void* arg) { c->event={fn,arg,false};return 0; }
int ble_npl_callout_reset(ble_npl_callout* c,std::uint32_t ticks) { c->ticks=ticks;c->active=true;return 0; }
void ble_npl_callout_stop(ble_npl_callout* c) { c->active=false; }
ble_npl_eventq* nimble_port_get_dflt_eventq() { return &fake::queue; }
int nvs_open(const char*,int,unsigned* h) { *h=1;return 0; }
int nvs_get_blob(unsigned,const char*,void* data,std::size_t* size) { if (!fake::have_saved) return 100; assert(*size >= sizeof(fake::saved));std::memcpy(data,&fake::saved,sizeof(fake::saved));*size=sizeof(fake::saved);return 0; }
int nvs_set_blob(unsigned,const char*,const void* data,std::size_t size) { assert(size==sizeof(fake::saved));std::memcpy(&fake::saved,data,size);fake::have_saved=true;return 0; }
int nvs_erase_key(unsigned,const char*) { fake::have_saved=false;return 0; }
int nvs_commit(unsigned) { ++fake::committed;return 0; }
void nvs_close(unsigned) {}
}

// Compiling the production translation unit also exercises its real static
// state machine. Only platform I/O and the downstream publication sink are fake.
#include "../main/apps/common/lyrics/ble_client.cpp"

static void tick() { assert(timer.active);timer.active=false;timer.event.fn(&timer.event); }
static void connect() {
    ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect={0,7};botifier_ams_gap_event(&e);
    assert(fake::link==lyrics::LinkState::Connecting);
    fake::persist_phone_bond(); // real SMP persists before the ENC_CHANGE callback
    e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change={0,7};botifier_ams_gap_event(&e);
    assert(fake::link==lyrics::LinkState::Discovering);
}
static ble_uuid_any_t uuid(const ble_uuid128_t& id) { ble_uuid_any_t out{};out.u128=id;return out; }
static ble_uuid_any_t uuid(const ble_uuid16_t& id) { ble_uuid_any_t out{};out.u16=id;return out; }
static void service_discovery(bool ams=true) {
    auto p=fake::pending; assert(p.kind==1); fake::pending={};
    ble_gatt_error ok{0},done{BLE_HS_EDONE};
    ble_gatt_svc g{1,8,uuid(Gatt)},a{10,40,uuid(Ams)};
    p.svc(7,&ok,&g,p.arg);if(ams)p.svc(7,&ok,&a,p.arg);p.svc(7,&done,nullptr,p.arg);
}
static void characteristic_discovery(bool gatt) {
    auto p=fake::pending;assert(p.kind==2);fake::pending={};
    ble_gatt_error ok{0},done{BLE_HS_EDONE};
    ble_gatt_chr first{static_cast<std::uint16_t>(gatt?2:11),static_cast<std::uint16_t>(gatt?3:12),static_cast<std::uint8_t>(gatt?BLE_GATT_CHR_PROP_INDICATE:BLE_GATT_CHR_PROP_NOTIFY|BLE_GATT_CHR_PROP_WRITE),gatt?uuid(Changed):uuid(EntityUpdate)};
    ble_gatt_chr next{static_cast<std::uint16_t>(gatt?7:15),static_cast<std::uint16_t>(gatt?8:16),BLE_GATT_CHR_PROP_READ|BLE_GATT_CHR_PROP_WRITE,uuid(EntityAttribute)};
    p.chr(7,&ok,&first,p.arg);p.chr(7,&ok,&next,p.arg);p.chr(7,&done,nullptr,p.arg);
    assert(fake::pending.kind==3);
    assert(fake::pending.first==(gatt?3:12));assert(fake::pending.last==(gatt?6:14));
}
static void descriptor_discovery(bool gatt) {
    auto p=fake::pending;assert(p.kind==3);fake::pending={};ble_gatt_error ok{0},done{BLE_HS_EDONE};
    ble_gatt_dsc d{static_cast<std::uint16_t>(gatt?4:13),uuid(Ccc)};
    p.dsc(7,&ok,p.first,&d,p.arg);p.dsc(7,&done,p.first,nullptr,p.arg);
    assert(fake::pending.bytes==std::vector<unsigned char>({static_cast<unsigned char>(gatt?2:1),0}));
}
static void notify(unsigned e,unsigned a,const std::string& value,bool truncated=false) {
    std::string bytes{static_cast<char>(e),static_cast<char>(a),static_cast<char>(truncated?1:0)};bytes+=value;
    os_mbuf mb{bytes.data(),bytes.size()};ble_gap_event event{};event.type=BLE_GAP_EVENT_NOTIFY_RX;event.notify_rx={7,s.update.value,&mb};botifier_ams_gap_event(&event);
}
static void ready() {
    service_discovery();characteristic_discovery(true);descriptor_discovery(true);fake::write_done();
    characteristic_discovery(false);descriptor_discovery(false);fake::write_done();
    assert(fake::pending.bytes==std::vector<unsigned char>({0,0,1}));
    notify(0,1,"1,1,42");assert(!fake::playing); // initial registration notifications precede Ready
    fake::write_done();assert(fake::pending.bytes==std::vector<unsigned char>({2,0,1,2,3}));
    fake::write_done();assert(fake::pending.bytes==std::vector<unsigned char>({1,0}));
    fake::write_done();assert(fake::link==lyrics::LinkState::Ready);assert(!fake::playing);
}
static void finish_refresh(const std::string& title,const std::string& artist="Artist",const std::string& album="Album") {
    const std::string values[]{artist,album,title,"240.5"};
    for (unsigned i=0;i<4;++i) {assert(fake::pending.bytes==std::vector<unsigned char>({2,static_cast<unsigned char>(i)}));fake::write_done();fake::read_value(values[i]);}
    assert(fake::pending.bytes==std::vector<unsigned char>({0,1}));fake::write_done();fake::read_value("1,1,42.5");
    assert(fake::track_ready && fake::playing);assert(fake::tracks.back().title==title);assert(fake::tracks.back().duration_ms==240500);
}
static void full_refresh(const std::string& title="Song",const std::string& player="Spotify",
                         const std::string& artist="Artist",const std::string& album="Album") {
    tick();assert(fake::pending.bytes==std::vector<unsigned char>({0,0}));fake::write_done();fake::read_value(player);
    finish_refresh(title,artist,album);
}
static void notification_identity_regressions() {
    auto before=fake::invalidated;
    const auto publications=fake::tracks.size();
    for(unsigned i=0;i<24;++i)notify(0,0,"Spotify");
    assert(fake::invalidated==before && fake::tracks.size()==publications && fake::playing && fake::track_ready);
    assert(fake::pending.kind==0 && !s.dirty);

    notify(1,0,"7");assert(fake::invalidated==before+1);full_refresh();
    before=fake::invalidated;
    for(unsigned i=0;i<24;++i) {notify(1,0,"7");notify(0,0,"Spotify");notify(2,2,"Song");}
    notify(1,0,"0007");
    assert(fake::invalidated==before && fake::playing && fake::track_ready && fake::pending.kind==0 && !s.dirty);
    notify(1,0,"8");notify(1,0,"8");
    assert(fake::invalidated==before+1 && !fake::playing && !fake::track_ready);full_refresh();

    // Same index never masks a changed title; an unchanged player notification
    // during its selected-name read must not obsolete that read's revision.
    before=fake::invalidated;notify(2,2,"Next");notify(1,0,"8");
    tick();fake::write_done();notify(0,0,"Spotify");fake::read_value("Spotify");
    assert(fake::invalidated==before+1);finish_refresh("Next");

    // Unknown values still invalidate and must not leave a trusted old index.
    for(const auto& invalid:std::vector<std::string>{"","8x","-1","4294967296",std::string("8\0",2),std::string(513,'0')}) {
        before=fake::invalidated;notify(1,0,invalid);
        assert(fake::invalidated==before+1 && !fake::playing);full_refresh("Next");
        before=fake::invalidated;notify(1,0,"8");assert(fake::invalidated==before+1);full_refresh("Next");
    }
    before=fake::invalidated;notify(1,0,"8",true);
    assert(fake::invalidated==before+1);full_refresh("Next");
    before=fake::invalidated;notify(1,0,"8");assert(fake::invalidated==before+1);full_refresh("Next");
    notify(1,0,"4294967295");full_refresh("Next");
    before=fake::invalidated;notify(1,0,"4294967295");assert(fake::invalidated==before);
    before=fake::invalidated;notify(0,0,"Spotify",true);
    assert(fake::invalidated==before+1);full_refresh("Next");
    for(const auto& invalid:std::vector<std::string>{"","Spotify\n",std::string("Spotify\0",8),
                                                  std::string("\xc2\x80",2),std::string("\xc0\xaf",2),std::string(MaxValue+1,'x')}) {
        before=fake::invalidated;notify(0,0,invalid);
        assert(fake::invalidated==before+1 && !fake::playing);full_refresh("Next");
    }
    // A selected invalid name must not make a later equal invalid notify safe.
    for(const auto& invalid:std::vector<std::string>{"","Spotify\n",std::string("\xc2\x80",2)}) {
        notify(0,0,invalid);full_refresh("Next",invalid);
        before=fake::invalidated;notify(0,0,invalid);
        assert(fake::invalidated==before+1);full_refresh("Next");
    }

    notify(1,0,"9");full_refresh("Next");
    before=fake::invalidated;notify(0,0,"Other player");
    assert(fake::invalidated==before+1);full_refresh("Next","Other player");
    before=fake::invalidated;notify(0,0,"Other player");assert(fake::invalidated==before);
    notify(1,0,"9");assert(fake::invalidated==before+1);full_refresh("Next","Other player");
    notify(0,0,"Spotify");full_refresh();

    const auto old_tracks=fake::tracks.size();
    notify(2,2,"Player changed during read");tick();fake::write_done();
    notify(0,0,"Race player");fake::read_value("Spotify");
    assert(fake::tracks.size()==old_tracks && !fake::playing);
    full_refresh("Player changed during read","Race player");
    notify(0,0,"Spotify");full_refresh();

    // Rediscovery and reconnect are distinct ownership epochs. Their first
    // queue value is unknown even if its digits match the previous epoch.
    notify(1,0,"42");full_refresh();
    const char handles[]{10,0,40,0};os_mbuf changes{handles,4};ble_gap_event event{};
    event.type=BLE_GAP_EVENT_NOTIFY_RX;event.notify_rx={7,s.changed.value,&changes};botifier_ams_gap_event(&event);
    tick();ready();full_refresh();
    before=fake::invalidated;notify(1,0,"42");assert(fake::invalidated==before+1);full_refresh();
    event.type=BLE_GAP_EVENT_DISCONNECT;event.disconnect.conn=fake::peer;botifier_ams_gap_event(&event);fake::drain();
    connect();ready();full_refresh();
    before=fake::invalidated;notify(1,0,"42");assert(fake::invalidated==before+1);full_refresh();
    std::cout<<"lyrics BLE duplicate identity: player/queue storms, real changes, invalid/truncated values and epochs passed\n";
}
static void phone_title_notification_regressions() {
    const std::string player=reinterpret_cast<const char*>(u8"网易云音乐");
    const std::string artist="Original artist/Original song",album="Original album";
    const auto playback_publications=[] {
        return std::count_if(fake::publications.begin(),fake::publications.end(),
            [](const std::string& value){return value.compare(0,4,"0:1:")==0;});
    };
    notify(0,0,player);full_refresh("Original verse one",player,artist,album);
    auto before=fake::invalidated;
    const auto tracks=fake::tracks.size(),events=fake::publications.size();
    const auto anchors=playback_publications();
    notify(2,2,"Original verse two");
    assert(fake::invalidated==before && fake::playing && fake::track_ready);
    assert(fake::tracks.size()==tracks && fake::publications.size()==events);

    // A title update during the artist read must retire the partial batch,
    // while preserving the downstream playback state until a coherent batch.
    tick();fake::write_done();fake::read_value(player);
    assert(fake::pending.bytes==std::vector<unsigned char>({2,0}));fake::write_done();
    notify(2,2,"Original verse three");fake::read_value(artist);
    assert(fake::invalidated==before && fake::playing && fake::track_ready);
    assert(fake::tracks.size()==tracks && playback_publications()==anchors && fake::pending.kind==0);
    full_refresh("Original verse three",player,artist,album);
    assert(fake::invalidated==before && fake::tracks.size()==tracks+1 && playback_publications()==anchors+1);
    assert(fake::tracks.back().title=="Original verse three" && fake::tracks.back().artist==artist &&
           fake::tracks.back().album==album && fake::tracks.back().player==player);

    const std::string next_artist="Another artist/Another song";
    notify(2,0,next_artist);
    assert(fake::invalidated==before+1 && !fake::playing && !fake::track_ready);
    full_refresh("Another verse",player,next_artist,album);
    notify(1,0,"77");full_refresh("Another verse",player,next_artist,album);
    before=fake::invalidated;notify(1,0,"78");
    assert(fake::invalidated==before+1 && !fake::playing && !fake::track_ready);
    full_refresh("Another verse",player,next_artist,album);
    before=fake::invalidated;notify(0,0,"Spotify");
    assert(fake::invalidated==before+1 && !fake::playing && !fake::track_ready);full_refresh();

    // Slash-bearing normal artists on other players do not enter this defer
    // path. Within NetEase it is only a candidate: BLE preserves raw fields
    // for Engine/source verification instead of treating '/' as a delimiter.
    for(const auto& ordinary:std::vector<std::string>{"AC/DC","First artist/Second artist"}) {
        notify(2,0,ordinary);full_refresh("Original song","Spotify",ordinary);
        before=fake::invalidated;notify(2,2,"Another song");
        assert(fake::invalidated==before+1 && !fake::playing);full_refresh("Another song","Spotify",ordinary);
        notify(0,0,player);full_refresh("Original song",player,ordinary);
        notify(2,2,"Another song");full_refresh("Another song",player,ordinary);
        assert(fake::tracks.back().artist==ordinary && fake::tracks.back().title=="Another song");
        notify(0,0,"Spotify");full_refresh();
    }
    std::cout<<"lyrics BLE phone title: coherent newest batch preserves playback, real identity changes invalidate, raw slash artists retained\n";
}
static void stable_fields_during_title_refresh() {
    const std::string player=reinterpret_cast<const char*>(u8"网易云音乐");
    const std::string artist="Storm artist/Storm recording",album="Storm album";
    notify(0,0,player);full_refresh("Storm phrase 0",player,artist,album);
    const auto before=fake::invalidated;
    const auto tracks=fake::tracks.size();
    for(unsigned i=0;i<60;++i) {
        const auto title="Storm phrase "+std::to_string(i+1);
        notify(2,2,title);
        notify(2,0,artist);notify(2,1,album);notify(2,3,"240.5");
        assert(fake::invalidated==before && fake::playing && fake::track_ready);
        full_refresh(title,player,artist,album);
    }
    assert(fake::tracks.size()==tracks+60);
    notify(0,0,"Spotify");full_refresh();
    std::cout<<"lyrics BLE stable-field storm: 60 title batches, 180 unchanged fields, zero invalidations passed\n";
}
static void duplicate_pending_title_during_read() {
    const std::string player=reinterpret_cast<const char*>(u8"网易云音乐");
    const std::string artist="Pending artist/Pending recording",album="Pending album";
    notify(0,0,player);full_refresh("Pending phrase 0",player,artist,album);
    const auto before=fake::invalidated;
    const auto tracks=fake::tracks.size();
    notify(2,2,"Pending phrase 1");tick();fake::write_done();fake::read_value(player);
    fake::write_done();assert(fake::pending.kind==5);
    for(unsigned i=0;i<120;++i) {
        notify(2,2,"Pending phrase 1");
        notify(2,0,artist);notify(2,1,album);notify(2,3,"240.5");
    }
    assert(fake::pending.kind==5 && s.refreshing && !s.dirty);
    fake::read_value(artist);
    fake::write_done();fake::read_value(album);
    fake::write_done();fake::read_value("Pending phrase 1");
    fake::write_done();fake::read_value("240.5");
    fake::write_done();fake::read_value("1,1,42.5");
    assert(fake::invalidated==before && fake::tracks.size()==tracks+1 && fake::playing);
    assert(fake::tracks.back().title=="Pending phrase 1");
    notify(0,0,"Spotify");full_refresh();
    std::cout<<"lyrics BLE pending-title storm: 120 duplicates preserve the in-flight batch and publish once passed\n";
}
static void pending_title_identity_guards() {
    const std::string player=reinterpret_cast<const char*>(u8"网易云音乐");
    const std::string artist="Guard artist/Guard recording",album="Guard album";
    notify(0,0,player);full_refresh("Guard phrase 0",player,artist,album);

    // Work buffers already contain the new artist, but the stable identity
    // still belongs to the last fully published track. This is a real change.
    notify(2,2,"Guard phrase 1");tick();fake::write_done();fake::read_value(player);
    const std::string changed="Changed artist/Changed recording";
    fake::write_done();fake::read_value(changed);
    auto before=fake::invalidated;const auto tracks=fake::tracks.size();
    notify(2,0,changed);assert(fake::invalidated==before+1 && s.dirty);
    fake::write_done();fake::read_value(album);
    assert(fake::tracks.size()==tracks && !s.refreshing);
    full_refresh("Guard phrase 1",player,changed,album);

    // A real change followed by a return to the published value is a newer
    // identity update, rather than a duplicate of that published value.
    before=fake::invalidated;notify(2,0,artist);notify(2,0,changed);
    assert(fake::invalidated==before+2 && s.dirty);
    full_refresh("Guard phrase 1",player,changed,album);

    // Truncation makes even an identical visible pending title unknown.
    notify(2,2,"Guard phrase 2");tick();fake::write_done();fake::read_value(player);
    fake::write_done();notify(2,2,"Guard phrase 2",true);fake::read_value(changed);
    assert(s.dirty && !s.refreshing && fake::pending.kind==0);
    full_refresh("Guard phrase 2",player,changed,album);

    // Pending values retain complete bytes through the legal attribute limit;
    // a different last byte must still obsolete the old read.
    std::string longest(lyrics::protocol::kMaxAttributeBytes,'x');
    notify(2,2,longest);tick();fake::write_done();fake::read_value(player);
    fake::write_done();notify(2,2,longest);
    assert(!s.dirty && s.refreshing);
    longest.back()='y';notify(2,2,longest);fake::read_value(changed);
    assert(s.dirty && !s.refreshing && fake::pending.kind==0);
    full_refresh(longest,player,changed,album);
    // Invalid long durations must fail before touching the spare bytes now
    // retaining the pending title inside the existing PSRAM duration buffer.
    const std::string duration_title="Duration guard";
    notify(2,2,duration_title);tick();fake::write_done();fake::read_value(player);
    for(const auto& value:std::vector<std::string>{changed,album,duration_title}) {
        fake::write_done();fake::read_value(value);
    }
    fake::write_done();fake::read_value(std::string(MaxValue,'1'));
    assert(!s.ready && fake::link==lyrics::LinkState::Error);
    assert(std::memcmp(s.buffers->pending_title(),duration_title.data(),duration_title.size())==0);
    tick();ready();full_refresh();
    std::cout<<"lyrics BLE pending identity: published fields, reversions, truncated/full titles and duration bounds passed\n";
}
int main() {
    // Disabled cold boot needs no allocation; failed PSRAM allocation must
    // neither start the host nor publish an initialized/partially ready client.
    assert(botifier_ams_set_enabled(false)==0);
    assert(fake::media_allocation_attempts==0);
    fake::fail_media_allocation=true;
    assert(botifier_ams_set_enabled(true)==BLE_HS_ENOMEM);
    assert(fake::initialized==0 && !initialized.load() && !s.buffers);
    assert(!fake::media && !desired.load());
    fake::fail_media_allocation=false;
    // FORGET on a cold boot while disabled must remove only the saved media
    // phone; it cannot silently disappear before the host is initialized.
    fake::saved=fake::peer.peer_id_addr;fake::have_saved=true;
    fake::bonds.push_back(fake::legacy_phone);fake::persist_phone_bond();
    botifier_ams_forget_phone();assert(fake::deleted==0);
    assert(botifier_ams_set_enabled(false)==0);fake::drain();
    assert(fake::deleted==1 && !fake::have_saved && !fake::media);
    assert(botifier_ams_set_enabled(true)==0);assert(!fake::media);fake::drain();assert(fake::media);
    // First-security FORGET can precede the real SM's persistence of its bond.
    // Keep the independent bond store and the metadata ownership both empty.
    ble_gap_event handshake{};handshake.type=BLE_GAP_EVENT_CONNECT;handshake.connect={0,7};botifier_ams_gap_event(&handshake);
    botifier_ams_forget_phone();fake::drain();assert(!s.have_phone && s.retiring);
    fake::persist_phone_bond();assert(fake::bonded(fake::peer.peer_id_addr));
    handshake.type=BLE_GAP_EVENT_ENC_CHANGE;handshake.enc_change={0,7};botifier_ams_gap_event(&handshake);assert(!fake::have_saved);
    handshake.type=BLE_GAP_EVENT_DISCONNECT;handshake.disconnect.conn=fake::peer;botifier_ams_gap_event(&handshake);fake::drain();
    assert(!fake::bonded(fake::peer.peer_id_addr) && fake::bonded(fake::legacy_phone) && !fake::have_saved);
    connect();assert(fake::have_saved); // ownership precedes AMS availability
    service_discovery(false);characteristic_discovery(true);descriptor_discovery(true);fake::write_done();
    assert(fake::link!=lyrics::LinkState::Ready);
    s.have_phone=false;load_phone();assert(s.have_phone); // identity survives a reboot/reload
    botifier_ams_forget_phone();fake::drain();assert(fake::deleted==3 && !fake::have_saved);
    ble_gap_event late_encryption{};late_encryption.type=BLE_GAP_EVENT_ENC_CHANGE;late_encryption.enc_change={0,7};
    botifier_ams_gap_event(&late_encryption);assert(!fake::have_saved && s.retiring);
    ble_gap_event first_drop{};first_drop.type=BLE_GAP_EVENT_DISCONNECT;first_drop.disconnect.conn=fake::peer;
    fake::pending={};botifier_ams_gap_event(&first_drop);fake::drain();
    assert(fake::deleted==4 && !fake::bonded(fake::peer.peer_id_addr) && fake::bonded(fake::legacy_phone));
    connect();ready();full_refresh();assert(fake::have_saved && fake::initialized==1);

    // Unchanged metadata must not discard a valid song/clock or start another
    // complete read, independently of how the phone generated the notification.
    const auto same_track_invalidations = fake::invalidated;
    const auto same_track_publications = fake::tracks.size();
    notify(2,0,"Artist");notify(2,1,"Album");notify(2,2,"Song");notify(2,3,"240.5");
    if (fake::invalidated != same_track_invalidations || fake::tracks.size() != same_track_publications ||
        !fake::track_ready || !fake::playing || fake::pending.kind != 0 || s.dirty) {
        std::cerr << "FAIL: unchanged complete metadata discarded the current song and playback clock\n";
        return 1;
    }
    notification_identity_regressions();
    phone_title_notification_regressions();
    stable_fields_during_title_refresh();
    duplicate_pending_title_during_read();
    pending_title_identity_guards();
    // A truncated value remains unknown even when its visible prefix matches.
    notify(2,2,"Song",true);assert(!fake::track_ready && !fake::playing);
    full_refresh();

    // AMS A2 (NimBLE ATT 0x1A2 / decimal 418) means an empty attribute.
    // With no current player the encrypted subscription remains healthy;
    // wait for media rather than clearing handles and rediscovering services.
    const auto discoveries=fake::service_discoveries;
    const auto old_handle=s.update.value;
    auto old_tracks=fake::tracks.size();
    notify(2,2,"No current media");tick();fake::attribute_error(BLE_HS_ATT_ERR(0xa2));
    assert(s.ready && s.waiting_media && fake::link==lyrics::LinkState::Ready);
    assert(!fake::track_ready && !fake::playing && s.update.value==old_handle);
    assert(fake::tracks.size()==old_tracks && timer.ticks==5000 && fake::pending.kind==0);
    for(unsigned i=0;i<3;++i) { tick();fake::attribute_error(BLE_HS_ATT_ERR(0xa2));assert(timer.ticks==5000 && fake::pending.kind==0); }
    assert(fake::service_discoveries==discoveries);
    notify(2,2,"Media returned");assert(timer.ticks==250);full_refresh("Media returned");

    // Empty album is optional and belongs to this complete batch; never reuse
    // the previous song's album. Exercise both Select and Read A2 replies.
    for(bool error_on_read:{false,true}) {
        const std::string title = error_on_read ? "Albumless again" : "Albumless";
        notify(2,2,title);tick();fake::write_done();fake::read_value("Spotify");
        fake::write_done();fake::read_value("Artist");
        assert(fake::pending.bytes==std::vector<unsigned char>({2,1}));
        if(error_on_read)fake::write_done();
        fake::attribute_error(BLE_HS_ATT_ERR(0xa2));
        assert(fake::pending.bytes==std::vector<unsigned char>({2,2}));
        fake::write_done();fake::read_value(title);fake::write_done();fake::read_value("240.5");
        fake::write_done();fake::read_value("1,1,10");
        assert(fake::tracks.back().album.empty() && fake::playing && s.ready);
    }

    // Required title absent after the new artist/album were read cannot commit
    // a mixed song or preserve old lyrics/playing. A timer can recover it.
    old_tracks=fake::tracks.size();notify(2,2,"Missing title");tick();fake::write_done();fake::read_value("Spotify");
    fake::write_done();fake::read_value("New Artist");fake::write_done();fake::read_value("New Album");
    fake::write_done();fake::attribute_error(BLE_HS_ATT_ERR(0xa2));
    assert(fake::tracks.size()==old_tracks && !fake::track_ready && !fake::playing && s.waiting_media);
    assert(fake::link==lyrics::LinkState::Ready && timer.ticks==5000);
    full_refresh("Recovered");assert(fake::service_discoveries==discoveries);

    for(unsigned missing:{0u,3u}) {
        notify(2,2,"Missing required field");tick();fake::write_done();fake::read_value("Spotify");
        const std::string values[]{"Artist","Album","Song"};
        for(unsigned i=0;i<missing;++i) { fake::write_done();fake::read_value(values[i]); }
        if(missing==3)fake::write_done(); // artist Select / duration Read
        fake::attribute_error(BLE_HS_ATT_ERR(0xa2));
        assert(s.waiting_media && !fake::track_ready && !fake::playing && s.ready);
        full_refresh();
    }
    for(bool error_on_read:{false,true}) {
        notify(0,1,"",true);
        if(error_on_read)fake::write_done();
        fake::attribute_error(BLE_HS_ATT_ERR(0xa2));
        assert(s.waiting_media && !fake::track_ready && !fake::playing && s.ready);
        full_refresh();
    }

    // A later pause/seek notification still wins over an older selected value,
    // including an A2 returned for that older selected PlaybackInfo.
    notify(0,1,"",true);fake::write_done();notify(0,1,"0,0,8");
    fake::attribute_error(BLE_HS_ATT_ERR(0xa2));
    assert(!s.waiting_media && fake::track_ready && !fake::playing);
    assert(fake::service_discoveries==discoveries);

    // Invalid-state ATT is not equivalent to absent media and still enters
    // the existing delayed subscription rebuild path.
    notify(0,1,"",true);fake::attribute_error(BLE_HS_ATT_ERR(0xa0));
    assert(fake::link==lyrics::LinkState::Error && !s.ready && timer.ticks==10000);
    tick();ready();full_refresh();

    // A metadata/queue event invalidates immediately; the replacement batch
    // carries a fresh playback anchor and never publishes partial metadata.
    notify(0,1,"1,1,48");notify(2,2,"Next");notify(1,0,"2");
    assert(!fake::track_ready && !fake::playing);full_refresh("Next");

    // A new song during a long read invalidates the entire in-flight batch.
    const auto count=fake::tracks.size();notify(2,0,"Another");tick();fake::write_done();fake::read_value("Spotify");
    fake::write_done();notify(2,2,"Changed again");fake::read_value("stale artist");
    assert(fake::tracks.size()==count);full_refresh("Final");

    // Truncated UTF-8 is never forwarded; select + multi-chunk read recovers
    // the complete attribute, with concurrent notifications in separate storage.
    notify(0,1,std::string("\xe4\xb8",2),true);assert(fake::pending.bytes==std::vector<unsigned char>({0,1}));fake::write_done();
    auto p=fake::pending;fake::pending={};ble_gatt_error ok{0},done{BLE_HS_EDONE};
    const char part1[]="1,1,";os_mbuf mb{part1,4};ble_gatt_attr attr{16,0,&mb};p.attr(7,&ok,&attr,p.arg);
    notify(0,1,"0,0,5");const char part2[]="55.0";mb={part2,4};attr.offset=4;p.attr(7,&ok,&attr,p.arg);p.attr(7,&done,nullptr,p.arg);
    // Regression: the previous implementation incorrectly expected the old
    // "playing 55s" read to overwrite this later "paused 5s" notification.
    assert(fake::publications.back()=="0:1:0,0,5" && !fake::playing);
    notify(0,1,"",true);fake::write_done();p=fake::pending;fake::pending={};
    notify(0,1,"1,1,100");const char old_seek[]="1,1,10";mb={old_seek,6};attr.offset=0;
    p.attr(7,&ok,&attr,p.arg);p.attr(7,&done,nullptr,p.arg);
    assert(fake::publications.back()=="0:1:1,1,100");

    // Service Changed stops Ready immediately and rebuilds handles only after
    // the serialized operation completes. AMS can also disappear and reappear.
    const char handles[]{10,0,40,0};os_mbuf changes{handles,4};ble_gap_event changed{};changed.type=BLE_GAP_EVENT_NOTIFY_RX;changed.notify_rx={7,s.changed.value,&changes};botifier_ams_gap_event(&changed);
    assert(fake::link==lyrics::LinkState::Discovering);tick();service_discovery(false);characteristic_discovery(true);descriptor_discovery(true);fake::write_done();
    assert(fake::link!=lyrics::LinkState::Ready && fake::pending.kind==0);tick();ready();full_refresh();

    // Epochs reject old ATT callbacks even when the controller reuses handle 7.
    notify(2,2,"New");tick();const auto old=fake::pending;
    ble_gap_event drop{};drop.type=BLE_GAP_EVENT_DISCONNECT;drop.disconnect.conn=fake::peer;botifier_ams_gap_event(&drop);
    fake::pending={};connect();const auto fresh=fake::pending;old.attr(7,&ok,nullptr,old.arg);assert(fake::pending.arg==fresh.arg);
    ready();full_refresh();

    // Rapid OFF/ON while termination is pending must restore the latest media
    // advert after DISCONNECT, instead of remaining on the legacy advert.
    assert(botifier_ams_set_enabled(false)==0);fake::drain();assert(!fake::media);
    assert(botifier_ams_set_enabled(true)==0);fake::drain();assert(!fake::media);
    fake::pending={};botifier_ams_gap_event(&drop);fake::drain();assert(fake::media);
    connect();ready();full_refresh();

    // An error retries on a timer; a never-completing ATT procedure disconnects.
    notify(2,2,"Error");fake::next_error=77;tick();assert(fake::link==lyrics::LinkState::Error && timer.ticks==10000);
    tick();assert(fake::pending.kind==1);const int before=fake::disconnected;tick();assert(fake::disconnected==before+1);

    botifier_ams_forget_phone();assert(fake::deleted==4);fake::drain();assert(fake::deleted==5 && !fake::have_saved);
    assert(botifier_ams_set_enabled(false)==0);fake::drain();assert(!fake::media && fake::link==lyrics::LinkState::Off);
    assert(botifier_ams_set_enabled(true)==0);fake::drain();assert(fake::initialized==1);
    // Every reconnect/forget/disable path above retained one stable buffer;
    // only the failed cold attempt and its successful retry allocated.
    assert(fake::media_allocation_attempts==2 && s.buffers);
    botifier_ams_legacy_requested();assert(!s.enabled && fake::link==lyrics::LinkState::Off);
    std::cout << "lyrics BLE: encrypted setup, CCC ranges, atomic track/clock refresh, UTF-8 long read, service changes, callback epochs, timeout, forget and shared-host lifecycle passed\n";
}
