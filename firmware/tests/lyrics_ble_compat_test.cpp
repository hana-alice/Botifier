#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "host/ble_hs.h"

constexpr std::size_t STACKCHAN_MAX_JSON_LEN = 2048;
constexpr int MALLOC_CAP_SPIRAM = 1;
constexpr int BLE_HS_ENOMEM = 11;
#define MODLOG_DFLT(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define CONFIG_BT_NIMBLE_GAP_SERVICE 1

static char pool[4][STACKCHAN_MAX_JSON_LEN + 1];
static unsigned pool_used, allocations;
static void* heap_caps_malloc(std::size_t size, int capability)
{
    assert(size == STACKCHAN_MAX_JSON_LEN + 1 && capability == MALLOC_CAP_SPIRAM);
    ++allocations;
    if (allocations == 2) return nullptr; // partial first allocation must recover
    assert(pool_used < 4);
    return pool[pool_used++];
}
static char *stackchan_motion_data, *stackchan_avatar_data, *stackchan_config_data, *stackchan_rgb_data;
static std::uint16_t stackchan_motion_len, stackchan_avatar_len, stackchan_config_len, stackchan_rgb_len;
const ble_uuid128_t stackchan_svc_uuid = BLE_UUID128_INIT(1);
const ble_uuid128_t stackchan_svc_uuid_alt = BLE_UUID128_INIT(2);
static struct { const ble_uuid_t* uuid; } gatt_svr_svcs[1];
static std::vector<std::string> events;
static unsigned max_services, max_attrs, max_client_configs;
static bool att_pool = true, att_list = true;
static int init_error, start_error;
static bool gap_name_live;
static unsigned gap_allocations, gap_frees;
static std::string device_name;
static void ble_svc_gap_init() { assert(!gap_name_live);gap_name_live=true;++gap_allocations;device_name="nimble";events.push_back("gap"); }
static void ble_svc_gap_deinit() { assert(gap_name_live);gap_name_live=false;++gap_frees;events.push_back("gap-free"); }
static int ble_svc_gap_device_name_set(const char* name) { assert(gap_name_live);device_name=name;events.push_back("gap-name");return 0; }
static void ble_svc_gatt_init() { events.push_back("gatt"); }
static int ble_gatts_count_cfg(const void*) { events.push_back("count");max_services+=2;max_attrs+=12;max_client_configs+=5;return init_error; }
static int ble_gatts_add_svcs(const void*) { events.push_back("add");return 0; }

// Exact production function bodies, extracted by CMake on source changes.
#include "lyrics_gatt_init.inc"

static bool s_use_alt_uuid, s_legacy_pending, s_synced=true;
static std::atomic<bool> s_requested_alt{false};
static unsigned s_connections;
static void botifier_ams_legacy_requested() { events.push_back("release-media"); }
static int ble_prph_media_mode(bool enabled) { assert(!enabled);events.push_back("legacy-adv");return 0; }
static int ble_gap_adv_stop() { events.push_back("stop");return 0; }
static int ble_gatts_reset() { assert(s_connections==0 && att_pool);att_list=false;events.push_back("reset");return 0; }
static void ble_gatts_stop() {
    // The pinned SDK's reset clears ATT lists but does not clear these budgets;
    // stop clears budgets and frees the backing pool. Preserve that distinction.
    assert(!att_list && att_pool);att_pool=false;
    max_services=max_attrs=max_client_configs=0;events.push_back("gatt-stop");
}
static int ble_gatts_start() { events.push_back("start");att_pool=att_list=true;return start_error; }
#include "lyrics_legacy_request.inc"

int main()
{
    assert(gatt_svr_init(false)==BLE_HS_ENOMEM);
    assert(allocations==4 && pool_used==3);
    assert(gatt_svr_init(false)==0);
    assert(allocations==5 && pool_used==4);
    assert(std::strcmp(stackchan_motion_data,"{}")==0 && stackchan_motion_len==2);
    std::strcpy(stackchan_motion_data,"{\"yaw\":1}");stackchan_motion_len=9;
    std::strcpy(stackchan_avatar_data,"face");stackchan_avatar_len=4;
    char* const original=stackchan_motion_data;
    for (unsigned i=0;i<200;++i) {
        const bool alt=(i%2)==0;
        s_requested_alt.store(alt);s_connections=1;events.clear();
        bleprph_legacy_request(nullptr);
        assert(s_legacy_pending);
        assert(events==std::vector<std::string>({"release-media"}));
        assert(s_use_alt_uuid!=alt);
        s_connections=0;events.clear();bleprph_legacy_request(nullptr);
        assert(!s_legacy_pending && s_use_alt_uuid==alt);
        assert(events==std::vector<std::string>({"release-media","stop","reset","gap-free","gatt-stop","gap","gatt","count","add","gap-name","start","legacy-adv"}));
        assert(gatt_svr_svcs[0].uuid==(alt?&stackchan_svc_uuid_alt.u:&stackchan_svc_uuid.u));
        assert(stackchan_motion_data==original && allocations==5);
        assert(max_services==2 && max_attrs==12 && max_client_configs==5);
        assert(gap_allocations==gap_frees+1 && device_name=="StackChan");
        assert(std::strcmp(stackchan_motion_data,"{\"yaw\":1}")==0 && stackchan_motion_len==9);
        assert(std::strcmp(stackchan_avatar_data,"face")==0 && stackchan_avatar_len==4);
    }
    // Repeated selection of the existing UUID never rebuilds its table.
    events.clear();bleprph_legacy_request(nullptr);
    assert(events==std::vector<std::string>({"release-media","legacy-adv"}));
    s_requested_alt.store(!s_use_alt_uuid);events.clear();init_error=77;bleprph_legacy_request(nullptr);
    assert(events.back()=="count" && s_legacy_pending); // no start/advert on bad registration
    init_error=0;att_pool=true;att_list=true;events.clear();start_error=78;bleprph_legacy_request(nullptr);
    assert(events.back()=="start" && s_legacy_pending); // no advert on failed table start
    std::cout << "lyrics BLE compatibility: partial allocation retry, 200 deferred UUID rebuilds, no repeated allocation, preserved JSON and same-mode reuse passed\n";
}
