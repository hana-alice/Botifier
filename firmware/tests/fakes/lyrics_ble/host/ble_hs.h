#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_HS_EBUSY 1
#define BLE_HS_ENOENT 2
#define BLE_HS_EAUTHEN 3
#define BLE_HS_EBADDATA 4
#define BLE_HS_EALREADY 5
#define BLE_HS_ENOTCONN 6
#define BLE_HS_ETIMEOUT 7
#define BLE_HS_ENOTSUP 8
#define BLE_HS_EMSGSIZE 9
#define BLE_HS_EDONE 10
#define BLE_HS_ATT_ERR(x) ((x) ? 0x100 + (x) : 0)
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_GATT_CHR_PROP_INDICATE 0x20
#define BLE_GATT_CHR_PROP_NOTIFY 0x10
#define BLE_GATT_CHR_PROP_WRITE 8
#define BLE_GATT_CHR_PROP_READ 2
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_ENC_CHANGE 3
#define BLE_GAP_EVENT_NOTIFY_RX 4

struct ble_uuid_t { std::uint8_t type; };
struct ble_uuid128_t { ble_uuid_t u; std::uint8_t value[16]; };
struct ble_uuid16_t { ble_uuid_t u; std::uint16_t value; };
union ble_uuid_any_t { ble_uuid_t u; ble_uuid128_t u128; ble_uuid16_t u16; };
#define BLE_UUID128_INIT(...) {{128}, {__VA_ARGS__}}
#define BLE_UUID16_INIT(v) {{16}, (v)}
inline int ble_uuid_cmp(const ble_uuid_t* a, const ble_uuid_t* b)
{
    if (a->type != b->type) return 1;
    if (a->type == 16) return reinterpret_cast<const ble_uuid16_t*>(a)->value != reinterpret_cast<const ble_uuid16_t*>(b)->value;
    return std::memcmp(reinterpret_cast<const ble_uuid128_t*>(a)->value, reinterpret_cast<const ble_uuid128_t*>(b)->value, 16);
}
struct ble_addr_t { std::uint8_t type; std::uint8_t val[6]; };
struct ble_gap_conn_desc { std::uint16_t conn_handle; ble_addr_t peer_id_addr; struct { bool encrypted, bonded; } sec_state; };
struct os_mbuf { const void* data; std::size_t length; };
#define OS_MBUF_PKTLEN(m) ((m)->length)
inline int os_mbuf_copydata(const os_mbuf* m, int offset, int length, void* data)
{
    if (offset < 0 || length < 0 || static_cast<std::size_t>(offset + length) > m->length) return -1;
    std::memcpy(data, static_cast<const char*>(m->data) + offset, length);
    return 0;
}
struct ble_gap_event {
    int type;
    struct { int status; std::uint16_t conn_handle; } connect;
    struct { int reason; ble_gap_conn_desc conn; } disconnect;
    struct { int status; std::uint16_t conn_handle; } enc_change;
    struct { std::uint16_t conn_handle, attr_handle; os_mbuf* om; } notify_rx;
};
struct ble_gatt_error { int status; };
struct ble_gatt_svc { std::uint16_t start_handle, end_handle; ble_uuid_any_t uuid; };
struct ble_gatt_chr { std::uint16_t def_handle, val_handle; std::uint8_t properties; ble_uuid_any_t uuid; };
struct ble_gatt_dsc { std::uint16_t handle; ble_uuid_any_t uuid; };
struct ble_gatt_attr { std::uint16_t handle, offset; os_mbuf* om; };
using ble_gatt_disc_svc_fn = int(std::uint16_t,const ble_gatt_error*,const ble_gatt_svc*,void*);
using ble_gatt_chr_fn = int(std::uint16_t,const ble_gatt_error*,const ble_gatt_chr*,void*);
using ble_gatt_dsc_fn = int(std::uint16_t,const ble_gatt_error*,std::uint16_t,const ble_gatt_dsc*,void*);
using ble_gatt_attr_fn = int(std::uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*);
struct ble_npl_event { void (*fn)(ble_npl_event*); void* arg; bool queued; };
struct ble_npl_eventq { ble_npl_event* event; };
struct ble_npl_callout { ble_npl_event event; std::uint32_t ticks; bool active; };
extern "C" {
int ble_gap_conn_find(std::uint16_t, ble_gap_conn_desc*);
int ble_gap_terminate(std::uint16_t, int);
int ble_gap_security_initiate(std::uint16_t);
int ble_gattc_disc_all_svcs(std::uint16_t,ble_gatt_disc_svc_fn*,void*);
int ble_gattc_disc_all_chrs(std::uint16_t,std::uint16_t,std::uint16_t,ble_gatt_chr_fn*,void*);
int ble_gattc_disc_all_dscs(std::uint16_t,std::uint16_t,std::uint16_t,ble_gatt_dsc_fn*,void*);
int ble_gattc_write_flat(std::uint16_t,std::uint16_t,const void*,std::uint16_t,ble_gatt_attr_fn*,void*);
int ble_gattc_read_long(std::uint16_t,std::uint16_t,std::uint16_t,ble_gatt_attr_fn*,void*);
int ble_store_util_delete_peer(const ble_addr_t*);
void ble_npl_event_init(ble_npl_event*,void (*)(ble_npl_event*),void*);
void ble_npl_eventq_put(ble_npl_eventq*,ble_npl_event*);
int ble_npl_callout_init(ble_npl_callout*,ble_npl_eventq*,void (*)(ble_npl_event*),void*);
int ble_npl_callout_reset(ble_npl_callout*,std::uint32_t);
void ble_npl_callout_stop(ble_npl_callout*);
ble_npl_eventq* nimble_port_get_dflt_eventq();
}
inline std::uint32_t ble_npl_time_ms_to_ticks32(std::uint32_t ms) { return ms; }
