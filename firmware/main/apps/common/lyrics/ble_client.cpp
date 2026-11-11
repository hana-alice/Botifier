#include "ble_client.h"
#include "service.h"
#include "protocol.h"
#include <apps/common/runtime_diagnostics.h>
#include "hal/utils/bleprph/bleprph.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
extern "C" {
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
}

namespace {
constexpr char Tag[] = "lyrics.ams";
constexpr std::size_t MaxValue = 1024;
const ble_uuid128_t Ams = BLE_UUID128_INIT(0xdc,0xf8,0x55,0xad,0x02,0xc5,0xf4,0x8e,0x3a,0x43,0x36,0x0f,0x2b,0x50,0xd3,0x89);
const ble_uuid128_t EntityUpdate = BLE_UUID128_INIT(0x02,0xc1,0x96,0xba,0x92,0xbb,0x0c,0x9a,0x1f,0x41,0x8d,0x80,0xce,0xab,0x7c,0x2f);
const ble_uuid128_t EntityAttribute = BLE_UUID128_INIT(0xd7,0xd5,0xbb,0x70,0xa8,0xa3,0xab,0xa6,0xd8,0x46,0xab,0x23,0x8c,0xf3,0xb2,0xc6);
const ble_uuid16_t Gatt = BLE_UUID16_INIT(0x1801);
const ble_uuid16_t Changed = BLE_UUID16_INIT(0x2a05);
const ble_uuid16_t Ccc = BLE_UUID16_INIT(0x2902);
enum class Op { Idle, Services, GattChars, GattDescriptors, GattSubscribe,
    AmsChars, AmsDescriptors, AmsSubscribe, Player, Track, Queue, Select, Read };
struct Range { std::uint16_t start = 0, end = 0; };
struct Characteristic { std::uint16_t value = 0, end = 0, ccc = 0; };
struct MediaBuffers {
    char value[MaxValue];
    char notification[MaxValue];
    char track[4][MaxValue];
};
struct Client {
    bool enabled = false, ready = false, rediscover = false, dirty = false, refreshing = false, retiring = false;
    bool waiting_media = false, complete_track = false;
    bool have_queue = false;
    bool forget_connection = false;
    std::uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
    std::uintptr_t token = 1;
    Op op = Op::Idle;
    Range ams, gatt;
    Characteristic update, changed;
    std::uint16_t attribute = 0;
    std::uint8_t entity = 0, attr = 0, field = 0;
    std::uint8_t pending = 0;
    std::uint32_t player_revision[2]{}, read_revision = 0;
    std::uint32_t queue_index = 0;
    MediaBuffers* buffers = nullptr;
    std::size_t length = 0;
    std::size_t track_length[4]{};
    bool have_phone = false;
    ble_addr_t phone{};
} s;
lyrics::Track published_track;
std::atomic<bool> desired{false}, forget_requested{false}, initialized{false};
std::mutex init_mutex;
ble_npl_event control_event;
ble_npl_callout timer;

void pump();
void discover();
void fail(int error);
void complete_value();
bool absent_attribute(int error);
int services(std::uint16_t, const ble_gatt_error*, const ble_gatt_svc*, void*);
int chars(std::uint16_t, const ble_gatt_error*, const ble_gatt_chr*, void*);
int descriptors(std::uint16_t, const ble_gatt_error*, std::uint16_t, const ble_gatt_dsc*, void*);
int written(std::uint16_t, const ble_gatt_error*, ble_gatt_attr*, void*);
int read(std::uint16_t, const ble_gatt_error*, ble_gatt_attr*, void*);

void* begin(Op op)
{
    s.op = op;
    if (++s.token == 0) ++s.token;
    return reinterpret_cast<void*>(s.token);
}
bool current(std::uint16_t conn, void* arg)
{
    return s.enabled && !s.retiring && conn == s.conn && reinterpret_cast<std::uintptr_t>(arg) == s.token;
}
void schedule(std::uint32_t ms)
{
    const int rc = ble_npl_callout_reset(&timer, ble_npl_time_ms_to_ticks32(ms));
    if (rc != 0) lyrics::on_link(lyrics::LinkState::Error, rc);
}
bool equal(const ble_addr_t& a, const ble_addr_t& b)
{
    return a.type == b.type && std::memcmp(a.val, b.val, sizeof(a.val)) == 0;
}
bool utf8(const char* data, std::size_t length, bool plain = false)
{
    for (std::size_t i = 0; i < length;) {
        const auto c = static_cast<unsigned char>(data[i++]);
        if (c == 0 || (plain && (c < 0x20 || c == 0x7f))) return false;
        if (c < 0x80) continue;
        unsigned n = 0, cp = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; cp = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; cp = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp = c & 7; minimum = 0x10000; }
        else return false;
        if (i + n > length) return false;
        while (n--) {
            const auto v = static_cast<unsigned char>(data[i++]);
            if ((v & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (v & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            (plain && cp >= 0x80 && cp <= 0x9f)) return false;
    }
    return true;
}
bool queue_index(std::string_view value, std::uint32_t& index)
{
    if (value.empty() || value.size() > lyrics::protocol::kMaxAttributeBytes) return false;
    index = 0;
    for (const auto c : value) {
        if (c < '0' || c > '9' || index > (UINT32_MAX - (c - '0')) / 10u) return false;
        index = index * 10u + (c - '0');
    }
    return true;
}
int load_phone()
{
    s.have_phone = false;
    nvs_handle_t nvs;
    int rc = nvs_open("botifier_ams", NVS_READONLY, &nvs);
    if (rc == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (rc != ESP_OK) return rc;
    std::size_t length = sizeof(s.phone);
    rc = nvs_get_blob(nvs, "phone", &s.phone, &length);
    s.have_phone = rc == ESP_OK && length == sizeof(s.phone);
    nvs_close(nvs);
    if (rc == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (rc != ESP_OK) return rc;
    return length == sizeof(s.phone) ? 0 : BLE_HS_EBADDATA;
}
int save_phone(const ble_addr_t& phone)
{
    // Keep the just-paired identity recoverable by FORGET even if this small
    // metadata commit fails. The caller removes that new bond on failure.
    s.phone = phone;
    s.have_phone = true;
    nvs_handle_t nvs;
    int rc = nvs_open("botifier_ams", NVS_READWRITE, &nvs);
    if (rc != ESP_OK) return rc;
    rc = nvs_set_blob(nvs, "phone", &phone, sizeof(phone));
    if (rc == ESP_OK) rc = nvs_commit(nvs);
    nvs_close(nvs);
    return rc;
}
void clear_handles()
{
    ++s.token;
    s.op = Op::Idle;
    s.ready = false;
    s.ams = {}; s.gatt = {}; s.update = {}; s.changed = {};
    s.attribute = 0;
    s.pending = 0;
    s.dirty = false;
    s.refreshing = false;
    s.waiting_media = false;
    s.complete_track = false;
    s.have_queue = false;
    published_track.player.clear();
    s.rediscover = false;
    s.length = 0;
}
void disconnect_owned()
{
    if (s.conn == BLE_HS_CONN_HANDLE_NONE) return;
    const int rc = ble_gap_terminate(s.conn, BLE_ERR_REM_USER_CONN_TERM);
    if (rc == 0 || rc == BLE_HS_EALREADY) s.retiring = true;
    if (rc != 0 && rc != BLE_HS_ENOTCONN) ESP_LOGW(Tag, "Disconnect failed: %d", rc);
}
void fail(int error)
{
    ESP_LOGW(Tag, "AMS operation %d entity %u attribute %u failed: %d", static_cast<int>(s.op),
             static_cast<unsigned>(s.entity), static_cast<unsigned>(s.attr), error);
    clear_handles();
    lyrics::on_link(lyrics::LinkState::Error, error);
    // One delayed retry per failure. No immediate ATT retry loop.
    if (s.enabled && s.conn != BLE_HS_CONN_HANDLE_NONE) schedule(10000);
}
void check_start(int rc)
{
    if (rc != 0) fail(rc);
    else schedule(30000);
}
void write(Op op, std::uint16_t handle, const std::uint8_t* value, std::size_t length)
{
    void* context = begin(op);
    check_start(ble_gattc_write_flat(s.conn, handle, value, length, written, context));
}
void discover()
{
    clear_handles();
    lyrics::on_link(lyrics::LinkState::Discovering);
    void* context = begin(Op::Services);
    check_start(ble_gattc_disc_all_svcs(s.conn, services, context));
}
void ams_chars()
{
    if (!s.ams.start) {
        s.op = Op::Idle;
        lyrics::on_link(lyrics::LinkState::Discovering, BLE_HS_ENOENT);
        schedule(10000); // iOS can publish AMS later; Service Changed also wakes it.
        return;
    }
    s.update = {};
    s.attribute = 0;
    void* context = begin(Op::AmsChars);
    check_start(ble_gattc_disc_all_chrs(s.conn, s.ams.start, s.ams.end, chars, context));
}
int services(std::uint16_t conn, const ble_gatt_error* error, const ble_gatt_svc* svc, void* arg)
{
    if (!current(conn, arg)) return 0;
    if (error->status == 0 && svc) {
        if (ble_uuid_cmp(&svc->uuid.u, &Ams.u) == 0) s.ams = {svc->start_handle, svc->end_handle};
        if (ble_uuid_cmp(&svc->uuid.u, &Gatt.u) == 0) s.gatt = {svc->start_handle, svc->end_handle};
        return 0;
    }
    if (error->status != BLE_HS_EDONE) { fail(error->status); return 0; }
    if (!s.gatt.start) { fail(BLE_HS_ENOENT); return 0; }
    void* context = begin(Op::GattChars);
    check_start(ble_gattc_disc_all_chrs(s.conn, s.gatt.start, s.gatt.end, chars, context));
    return 0;
}
int chars(std::uint16_t conn, const ble_gatt_error* error, const ble_gatt_chr* chr, void* arg)
{
    if (!current(conn, arg)) return 0;
    Characteristic& target = s.op == Op::GattChars ? s.changed : s.update;
    const auto& range = s.op == Op::GattChars ? s.gatt : s.ams;
    if (error->status == 0 && chr) {
        // Descriptor discovery must stop before the next characteristic.
        if (target.value && chr->def_handle > target.value) target.end = std::min<std::uint16_t>(target.end, chr->def_handle - 1);
        const bool match = s.op == Op::GattChars ? ble_uuid_cmp(&chr->uuid.u, &Changed.u) == 0 : ble_uuid_cmp(&chr->uuid.u, &EntityUpdate.u) == 0;
        if (match) {
            const auto required = s.op == Op::GattChars ? BLE_GATT_CHR_PROP_INDICATE : (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_WRITE);
            if ((chr->properties & required) != required) { fail(BLE_HS_ENOTSUP); return BLE_HS_ENOTSUP; }
            target.value = chr->val_handle;
            target.end = range.end;
        }
        if (s.op == Op::AmsChars && ble_uuid_cmp(&chr->uuid.u, &EntityAttribute.u) == 0 &&
            (chr->properties & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_READ)) == (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_READ)) s.attribute = chr->val_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) { fail(error->status); return 0; }
    if (!target.value || target.end <= target.value || (s.op == Op::AmsChars && !s.attribute)) { fail(BLE_HS_ENOENT); return 0; }
    void* context = begin(s.op == Op::GattChars ? Op::GattDescriptors : Op::AmsDescriptors);
    check_start(ble_gattc_disc_all_dscs(s.conn, target.value, target.end, descriptors, context));
    return 0;
}
int descriptors(std::uint16_t conn, const ble_gatt_error* error, std::uint16_t value, const ble_gatt_dsc* dsc, void* arg)
{
    if (!current(conn, arg)) return 0;
    const bool gatt = s.op == Op::GattDescriptors;
    Characteristic& target = gatt ? s.changed : s.update;
    if (error->status == 0 && dsc) {
        if (value == target.value && dsc->handle > target.value && dsc->handle <= target.end && ble_uuid_cmp(&dsc->uuid.u, &Ccc.u) == 0) target.ccc = dsc->handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) { fail(error->status); return 0; }
    if (!target.ccc) { fail(BLE_HS_ENOENT); return 0; }
    const std::uint8_t ccc[]{static_cast<std::uint8_t>(gatt ? 2 : 1), 0};
    write(gatt ? Op::GattSubscribe : Op::AmsSubscribe, target.ccc, ccc, sizeof(ccc));
    return 0;
}
int written(std::uint16_t conn, const ble_gatt_error* error, ble_gatt_attr*, void* arg)
{
    if (!current(conn, arg)) return 0;
    if (error->status != 0) { if (!absent_attribute(error->status)) fail(error->status); return 0; }
    if (s.rediscover) { s.op = Op::Idle; pump(); return 0; }
    static const std::uint8_t player[]{0, 0, 1}, track[]{2, 0, 1, 2, 3}, queue[]{1, 0};
    switch (s.op) {
    case Op::GattSubscribe: ams_chars(); break;
    case Op::AmsSubscribe: write(Op::Player, s.update.value, player, sizeof(player)); break;
    case Op::Player: write(Op::Track, s.update.value, track, sizeof(track)); break;
    case Op::Track: write(Op::Queue, s.update.value, queue, sizeof(queue)); break;
    case Op::Queue: {
        ble_gap_conn_desc desc{};
        int rc = ble_gap_conn_find(s.conn, &desc);
        if (rc == 0 && desc.sec_state.bonded && !s.have_phone) rc = save_phone(desc.peer_id_addr);
        if (rc != 0) { fail(rc); break; }
        s.ready = true;
        s.dirty = true;
        s.op = Op::Idle;
        lyrics::on_link(lyrics::LinkState::Ready);
        schedule(250);
        break;
    }
    case Op::Select: {
        s.length = 0;
        void* context = begin(Op::Read);
        check_start(ble_gattc_read_long(s.conn, s.attribute, 0, read, context));
        break;
    }
    default: break;
    }
    return 0;
}
void select(std::uint8_t entity, std::uint8_t attr)
{
    s.entity = entity;
    s.attr = attr;
    s.read_revision = entity == 0 ? s.player_revision[attr] : 0;
    const std::uint8_t selection[]{entity, attr};
    write(Op::Select, s.attribute, selection, sizeof(selection));
}
void pump()
{
    if (!s.enabled || s.conn == BLE_HS_CONN_HANDLE_NONE || s.op != Op::Idle) return;
    if (s.rediscover) { discover(); return; }
    if (!s.ready) return;
    if (s.dirty) {
        s.dirty = false;
        s.waiting_media = false;
        s.refreshing = true;
        s.field = 0;
        // Name can invalidate the core's current track; publish it before the
        // atomic metadata batch, then refresh the clock after that batch.
        select(0, 0);
        return;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (s.pending & (1u << i)) { s.pending &= ~(1u << i); select(0, i); return; }
    }
    schedule(15000);
}
int read(std::uint16_t conn, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg)
{
    if (!current(conn, arg)) return 0;
    if (error->status == 0 && attr && attr->om) {
        const auto length = OS_MBUF_PKTLEN(attr->om);
        if (attr->offset != s.length || length > MaxValue - s.length) { fail(BLE_HS_EMSGSIZE); return BLE_HS_EMSGSIZE; }
        if (os_mbuf_copydata(attr->om, 0, length, s.buffers->value + s.length) != 0) { fail(BLE_HS_EBADDATA); return BLE_HS_EBADDATA; }
        s.length += length;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) { if (!absent_attribute(error->status)) fail(error->status); return 0; }
    complete_value();
    return 0;
}
void complete_value()
{
    if (!utf8(s.buffers->value, s.length)) { fail(BLE_HS_EBADDATA); return; }
    s.op = Op::Idle;
    if (s.rediscover) { pump(); return; }
    if (s.entity == 0 && s.read_revision != s.player_revision[s.attr]) {
        // A later notification is authoritative over this older selected value.
        // Truncated notifications already scheduled a new complete read.
        if (s.refreshing) { s.refreshing = false; s.dirty = true; schedule(250); }
        else pump();
        return;
    }
    if (s.refreshing) {
        if (s.dirty) { s.refreshing = false; schedule(250); return; }
        if (s.entity == 0) {
            const std::string_view player{s.buffers->value, s.length};
            if (published_track.player != player) s.have_queue = false;
            if (!player.empty() && player.size() <= lyrics::protocol::kMaxAttributeBytes &&
                utf8(player.data(), player.size(), true)) published_track.player.assign(player);
            else published_track.player.clear();
            lyrics::on_attribute(0, 0, {s.buffers->value, s.length});
            select(2, 0);
            return;
        }
        std::memcpy(s.buffers->track[s.field], s.buffers->value, s.length);
        s.track_length[s.field] = s.length;
        if (++s.field < 4) { select(2, s.field); return; }
        s.refreshing = false;
        if (s.track_length[3] >= 32) { fail(BLE_HS_EBADDATA); return; }
        char duration[32]{};
        std::memcpy(duration, s.buffers->track[3], s.track_length[3]);
        char* end = nullptr;
        const double seconds = std::strtod(duration, &end);
        if (end != duration + s.track_length[3] || !std::isfinite(seconds) || seconds <= 0 || seconds > 86400) {
            fail(BLE_HS_EBADDATA); return;
        }
        published_track.artist.assign(s.buffers->track[0], s.track_length[0]);
        published_track.album.assign(s.buffers->track[1], s.track_length[1]);
        published_track.title.assign(s.buffers->track[2], s.track_length[2]);
        published_track.duration_ms = static_cast<std::uint32_t>(seconds * 1000);
        lyrics::on_track_snapshot(published_track);
        s.complete_track = true;
        s.pending |= 2; // Player PlaybackInfo: anchor after every invalidation.
    } else {
        lyrics::on_attribute(s.entity, s.attr, {s.buffers->value, s.length});
    }
    pump();
}
bool absent_attribute(int error)
{
    if (error != BLE_HS_ATT_ERR(0xa2) || !s.ready || (s.op != Op::Select && s.op != Op::Read)) return false;
    // AMS defines A2 as an empty attribute, which can occur while no player
    // is active. Keep the working encrypted subscription and its handles.
    if (s.rediscover || (s.entity == 0 && s.read_revision != s.player_revision[s.attr]) ||
        (s.refreshing && s.dirty)) {
        s.op = Op::Idle;
        if (s.refreshing && !s.rediscover) {
            s.refreshing = false;
            s.dirty = true;
            schedule(250);
        } else pump();
        return true;
    }
    if (s.refreshing && s.entity == 2 && s.attr == 1) {
        s.length = 0; // Optional album: complete this field as empty in this batch.
        complete_value();
        return true;
    }
    ++s.token;
    s.op = Op::Idle;
    s.refreshing = s.dirty = false;
    s.pending = 0;
    s.waiting_media = true;
    s.complete_track = false;
    s.have_queue = false;
    published_track.player.clear();
    lyrics::on_track_invalidated();
    schedule(5000); // Notifications can wake it sooner; never rediscover in a loop.
    return true;
}
void timer_event(ble_npl_event*)
{
    if (!s.enabled) return;
    if (s.retiring) return;
    if (s.conn == BLE_HS_CONN_HANDLE_NONE) return;
    if (s.op != Op::Idle) {
        lyrics::on_link(lyrics::LinkState::Error, BLE_HS_ETIMEOUT);
        // A timeout may leave a live ATT procedure. Disconnect rather than
        // reusing a handle/context while it can still complete.
        clear_handles();
        disconnect_owned();
        return;
    }
    if (!s.ready) {
        ble_gap_conn_desc desc{};
        if (ble_gap_conn_find(s.conn, &desc) != 0 || !desc.sec_state.encrypted) {
            lyrics::on_link(lyrics::LinkState::Error, BLE_HS_ETIMEOUT);
            disconnect_owned();
        } else discover();
        return;
    }
    if (s.waiting_media) s.dirty = true;
    pump();
}
int forget_phone()
{
    // Only this media peer is removed; never ble_store_clear or NVS erase.
    if (s.have_phone) {
        const int rc = ble_store_util_delete_peer(&s.phone);
        if (rc != 0) return rc;
        nvs_handle_t nvs;
        int err = nvs_open("botifier_ams", NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            err = nvs_erase_key(nvs, "phone");
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
            if (err == ESP_OK) err = nvs_commit(nvs);
            nvs_close(nvs);
        }
        if (err != ESP_OK) return err;
        s.have_phone = false;
    }
    return 0;
}
void control(ble_npl_event*)
{
    if (forget_requested.exchange(false)) {
        if (s.conn != BLE_HS_CONN_HANDLE_NONE) s.forget_connection = true;
        clear_handles();
        disconnect_owned();
        int rc = s.have_phone ? 0 : load_phone();
        if (rc == 0) rc = forget_phone();
        if (rc != 0) { lyrics::on_link(lyrics::LinkState::Error, rc); return; }
    }
    const bool enabled = desired.load();
    if (enabled != s.enabled) {
        clear_handles();
        s.enabled = enabled;
        if (!enabled) disconnect_owned();
        else {
            const int rc = load_phone();
            if (rc != 0) {
                s.enabled = false;
                desired.store(false);
                lyrics::on_link(lyrics::LinkState::Error, rc);
                return;
            }
        }
    }
    if (enabled && s.conn != BLE_HS_CONN_HANDLE_NONE) return;
    const int rc = ble_prph_media_mode(enabled);
    if (rc != 0) {
        s.enabled = false;
        desired.store(false);
        lyrics::on_link(lyrics::LinkState::Error, rc);
    }
    else lyrics::on_link(enabled ? lyrics::LinkState::Pairing : lyrics::LinkState::Off);
}
} // namespace

extern "C" int botifier_ams_set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(init_mutex);
    if (!initialized.load()) {
        if (!enabled && !forget_requested.load()) return 0;
        // CPU-only AMS scratch must not consume the internal RAM needed by
        // TLS/DMA and task stacks. Keep it across disable and late callbacks.
        if (!s.buffers) {
            void* memory = heap_caps_malloc(sizeof(MediaBuffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!memory) {
                botifier_diag_mark(BOTIFIER_DIAG_BLE, 2, BLE_HS_ENOMEM);
                botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
                return BLE_HS_ENOMEM;
            }
            s.buffers = new (memory) MediaBuffers{};
        }
        botifier_diag_mark(BOTIFIER_DIAG_BLE, 2, 0);
        botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
        const int rc = ble_prph_ensure_initialized();
        if (rc != 0) return rc;
        published_track.artist.reserve(MaxValue);
        published_track.album.reserve(MaxValue);
        published_track.title.reserve(MaxValue);
        ble_npl_event_init(&control_event, control, nullptr);
        const int timer_rc = ble_npl_callout_init(&timer, nimble_port_get_dflt_eventq(), timer_event, nullptr);
        if (timer_rc != 0) {
            botifier_diag_mark(BOTIFIER_DIAG_BLE, 14, timer_rc);
            return timer_rc;
        }
        initialized.store(true);
        botifier_diag_mark(BOTIFIER_DIAG_BLE, 14, 0);
        botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
    }
    desired.store(enabled);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &control_event);
    return 0;
}
extern "C" void botifier_ams_forget_phone()
{
    forget_requested.store(true);
    if (!initialized.load()) return; // the worker's following set_enabled initializes it
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &control_event);
}
extern "C" bool botifier_ams_advertising_enabled() { return s.enabled; }
extern "C" bool botifier_ams_owns_connection(unsigned short conn)
{
    return conn != BLE_HS_CONN_HANDLE_NONE && conn == s.conn;
}
extern "C" void botifier_ams_legacy_requested()
{
    desired.store(false);
    if (!initialized.load()) return;
    clear_handles();
    s.enabled = false;
    ble_npl_callout_stop(&timer);
    disconnect_owned();
    lyrics::on_link(lyrics::LinkState::Off);
}
extern "C" void botifier_ams_host_reset(int reason)
{
    if (!initialized.load()) return;
    clear_handles();
    s.conn = BLE_HS_CONN_HANDLE_NONE;
    s.retiring = false;
    s.forget_connection = false;
    ble_npl_callout_stop(&timer);
    if (s.enabled) lyrics::on_link(lyrics::LinkState::Error, reason);
}
extern "C" void botifier_ams_host_synced()
{
    if (initialized.load()) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &control_event);
}
extern "C" void botifier_ams_gap_event(ble_gap_event* event)
{
    if (!initialized.load()) return;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (!s.enabled || event->connect.status != 0 || s.conn != BLE_HS_CONN_HANDLE_NONE) return;
        s.conn = event->connect.conn_handle;
        s.retiring = false;
        s.forget_connection = false;
        clear_handles();
        lyrics::on_link(lyrics::LinkState::Connecting);
        const int rc = ble_gap_security_initiate(s.conn);
        if (rc != 0 && rc != BLE_HS_EALREADY) { fail(rc); disconnect_owned(); }
        else schedule(30000);
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle != s.conn) return;
        if (s.forget_connection) {
            // HCI terminate is asynchronous: SMP may persist a new bond before
            // this final disconnect even after FORGET erased the earlier one.
            // The event's resolved identity belongs to this owned connection.
            const int rc = ble_store_util_delete_peer(&event->disconnect.conn.peer_id_addr);
            if (rc != 0) ESP_LOGW(Tag, "Final forgotten-media bond cleanup failed: %d", rc);
        }
        clear_handles();
        s.conn = BLE_HS_CONN_HANDLE_NONE;
        s.retiring = false;
        s.forget_connection = false;
        ble_npl_callout_stop(&timer);
        lyrics::on_link(s.enabled ? lyrics::LinkState::Pairing : lyrics::LinkState::Off);
        // A disable/re-enable can race the asynchronous disconnect. Apply the
        // latest mode now that the old handle is gone, not its stale advert.
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &control_event);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        if (!s.enabled || s.retiring || event->enc_change.conn_handle != s.conn) return;
        ble_gap_conn_desc desc{};
        int rc = event->enc_change.status;
        if (rc == 0) rc = ble_gap_conn_find(s.conn, &desc);
        if (rc == 0 && !desc.sec_state.encrypted) rc = BLE_HS_EAUTHEN;
        if (rc == 0 && s.have_phone && !equal(desc.peer_id_addr, s.phone)) rc = BLE_HS_EAUTHEN;
        if (rc == 0 && desc.sec_state.bonded && !s.have_phone) {
            // NimBLE persists the bond before ENC_CHANGE. Record ownership
            // now, before AMS discovery can fail or the device can reboot.
            rc = save_phone(desc.peer_id_addr);
            if (rc != ESP_OK) {
                const int deleted = ble_store_util_delete_peer(&desc.peer_id_addr);
                if (deleted != 0) ESP_LOGW(Tag, "Failed to remove unrecorded media bond: %d", deleted);
            }
        }
        if (rc != 0) { fail(rc); disconnect_owned(); }
        else discover();
        break;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (!s.enabled || s.retiring || event->notify_rx.conn_handle != s.conn) return;
        const auto handle = event->notify_rx.attr_handle;
        if (handle == s.changed.value && s.changed.value != 0) {
            if (OS_MBUF_PKTLEN(event->notify_rx.om) != 4) return;
            s.ready = false;
            s.rediscover = true;
            lyrics::on_link(lyrics::LinkState::Discovering);
            if (s.op == Op::Idle) schedule(250);
            return;
        }
        if (handle != s.update.value || !s.update.value) return;
        const auto length = OS_MBUF_PKTLEN(event->notify_rx.om);
        std::uint8_t prefix[3];
        if (length < 3 || os_mbuf_copydata(event->notify_rx.om, 0, 3, prefix) != 0) return;
        if (prefix[0] == 0 && prefix[1] == 1) ++s.player_revision[1];
        if ((prefix[0] == 2 && prefix[1] < 4) || (prefix[0] == 1 && prefix[1] == 0) ||
            (prefix[0] == 0 && prefix[1] == 0)) {
            // Repeated complete values do not describe a new recording. Keep
            // the current lyric request and playback anchor. Unknown values
            // still refresh; duplicate player/queue events cannot restart a batch.
            const auto size = static_cast<std::size_t>(length - 3);
            const bool complete = !(prefix[2] & 1) && size <= MaxValue &&
                os_mbuf_copydata(event->notify_rx.om, 3, size, s.buffers->notification) == 0;
            const std::string_view value{s.buffers->notification, complete ? size : 0};
            const bool same_context = s.ready && !s.rediscover && !s.waiting_media;
            if (prefix[0] == 1) {
                std::uint32_t index = 0;
                const bool valid = complete && queue_index(value, index);
                const bool same = valid && s.have_queue && index == s.queue_index;
                s.have_queue = valid;
                s.queue_index = index;
                if (same && same_context) return;
            } else if (prefix[0] == 0) {
                if (complete && !value.empty() && value.size() <= lyrics::protocol::kMaxAttributeBytes &&
                    utf8(value.data(), value.size(), true) && value == published_track.player && same_context) return;
                // A changed/unknown player owns a separate queue. Equal names
                // return before advancing the revision of a selected name read.
                ++s.player_revision[0];
                published_track.player.clear();
                s.have_queue = false;
            }
            if (prefix[0] == 2 && complete && s.complete_track &&
                !s.dirty && !s.refreshing && !s.waiting_media &&
                size == s.track_length[prefix[1]] &&
                utf8(s.buffers->notification, size) &&
                std::memcmp(s.buffers->notification, s.buffers->track[prefix[1]], size) == 0) return;
            s.complete_track = false;
            s.dirty = true;
            // Some players publish the current lyric as title. Read a
            // coherent batch before Engine decides whether its verified
            // recording identity changed; do not erase the clock first.
            if (!(prefix[0] == 2 && prefix[1] == 2 &&
                  lyrics::protocol::phone_metadata_candidate(published_track)))
                lyrics::on_track_invalidated();
            if (s.op == Op::Idle) schedule(250);
            return;
        }
        if (prefix[0] != 0 || prefix[1] > 1) return;
        if ((prefix[2] & 1) || length - 3 > MaxValue) {
            s.pending |= 1u << prefix[1];
            if (s.op == Op::Idle) pump();
            return;
        }
        // Separate notification scratch: a notify can arrive between Read Blob
        // chunks and must never overwrite that in-flight long read buffer.
        if (os_mbuf_copydata(event->notify_rx.om, 3, length - 3, s.buffers->notification) == 0 && utf8(s.buffers->notification, length - 3)) lyrics::on_attribute(prefix[0], prefix[1], {s.buffers->notification, static_cast<std::size_t>(length - 3)});
        break;
    }
    default: break;
    }
}
