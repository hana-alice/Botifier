/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_mac.h"

/* BLE */
#include "bleprph.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/ble_esp_gatt.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "apps/common/lyrics/ble_client.h"
#include <apps/common/runtime_diagnostics.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdatomic.h>

#if CONFIG_EXAMPLE_EXTENDED_ADV
static uint8_t ext_adv_pattern_1[] = {
    0x02,
    BLE_HS_ADV_TYPE_FLAGS,
    0x06,
    0x03,
    BLE_HS_ADV_TYPE_COMP_UUIDS16,
    0xab,
    0xcd,
    0x03,
    BLE_HS_ADV_TYPE_COMP_UUIDS16,
    0x18,
    0x11,
    0x11,
    BLE_HS_ADV_TYPE_COMP_NAME,
    'n',
    'i',
    'm',
    'b',
    'l',
    'e',
    '-',
    'b',
    'l',
    'e',
    'p',
    'r',
    'p',
    'h',
    '-',
    'e',
};
#endif

static const char *tag = "NimBLE_BLE_PRPH";
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);
#if CONFIG_EXAMPLE_RANDOM_ADDR
static uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;
#else
static uint8_t own_addr_type;
#endif

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
static uint16_t cids[MYNEWT_VAL(BLE_EATT_CHAN_NUM)];
static uint16_t bearers;
#endif

static bool s_use_alt_uuid = false;
static bool s_init_attempted;
static int s_init_result;
static bool s_synced;
static bool s_media_mode;
static struct ble_npl_event s_legacy_event;
static StaticSemaphore_t s_init_mutex_storage;
static SemaphoreHandle_t s_init_mutex;
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_legacy_io, s_legacy_bonding, s_legacy_sc, s_legacy_mitm;
static uint8_t s_legacy_our_keys, s_legacy_their_keys;
static atomic_bool s_requested_alt;
static bool s_legacy_pending;
static unsigned s_connections;

static const ble_uuid128_t ams_service_uuid = BLE_UUID128_INIT(
    0xdc, 0xf8, 0x55, 0xad, 0x02, 0xc5, 0xf4, 0x8e,
    0x3a, 0x43, 0x36, 0x0f, 0x2b, 0x50, 0xd3, 0x89);

void ble_store_config_init(void);

#if NIMBLE_BLE_CONNECT
/**
 * Logs information about a connection to the console.
 */
static void bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=", desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=", desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=", desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=", desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    MODLOG_DFLT(INFO,
                " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency, desc->supervision_timeout, desc->sec_state.encrypted,
                desc->sec_state.authenticated, desc->sec_state.bonded);
}
#endif

#if CONFIG_EXAMPLE_EXTENDED_ADV
/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void ext_bleprph_advertise(void)
{
    struct ble_gap_ext_adv_params params;
    struct os_mbuf *data;
    uint8_t instance = 0;
    int rc;

    /* First check if any instance is already active */
    if (ble_gap_ext_adv_active(instance)) {
        return;
    }

    /* use defaults for non-set params */
    memset(&params, 0, sizeof(params));

    /* enable connectable advertising */
    params.connectable = 1;

    /* advertise using random addr */
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;

    params.primary_phy   = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_2M;
    params.tx_power      = 127;
    params.sid           = 1;

    params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;

    /* configure instance 0 */
    rc = ble_gap_ext_adv_configure(instance, &params, NULL, bleprph_gap_event, NULL);
    assert(rc == 0);

    /* in this case only scan response is allowed */

    /* get mbuf for scan rsp data */
    data = os_msys_get_pkthdr(sizeof(ext_adv_pattern_1), 0);
    assert(data);

    /* fill mbuf with scan rsp data */
    rc = os_mbuf_append(data, ext_adv_pattern_1, sizeof(ext_adv_pattern_1));
    assert(rc == 0);

    rc = ble_gap_ext_adv_set_data(instance, data);
    assert(rc == 0);

    /* start advertising */
    rc = ble_gap_ext_adv_start(instance, 0, 0);
    assert(rc == 0);
}
#else

// /**
//  * Enables advertising with the following parameters:
//  *     o General discoverable mode.
//  *     o Undirected connectable mode.
//  */
// static void bleprph_advertise(void)
// {
//     struct ble_gap_adv_params adv_params;
//     struct ble_hs_adv_fields fields;
// #if CONFIG_BT_NIMBLE_GAP_SERVICE
//     const char *name;
// #endif
//     int rc;

//     /**
//      *  Set the advertisement data included in our advertisements:
//      *     o Flags (indicates advertisement type and other general info).
//      *     o Advertising tx power.
//      *     o Device name.
//      *     o 16-bit service UUIDs (alert notifications).
//      */

//     memset(&fields, 0, sizeof fields);

//     /* Advertise two flags:
//      *     o Discoverability in forthcoming advertisement (general)
//      *     o BLE-only (BR/EDR unsupported).
//      */
//     fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

//     /* Indicate that the TX power level field should be included; have the
//      * stack fill this value automatically.  This is done by assigning the
//      * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
//      */
//     fields.tx_pwr_lvl_is_present = 1;
//     fields.tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO;

// #if CONFIG_BT_NIMBLE_GAP_SERVICE
//     name                         = ble_svc_gap_device_name();
//     fields.name                  = (uint8_t *)name;
//     fields.name_len              = strlen(name);
//     fields.name_is_complete      = 1;
// #endif

//     fields.uuids16             = (ble_uuid16_t[]){BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)};
//     fields.num_uuids16         = 1;
//     fields.uuids16_is_complete = 1;

//     rc = ble_gap_adv_set_fields(&fields);
//     if (rc != 0) {
//         MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
//         return;
//     }

//     /* Begin advertising. */
//     memset(&adv_params, 0, sizeof adv_params);
//     adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
//     adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
//     rc                   = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event,
//     NULL); if (rc != 0) {
//         MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
//         return;
//     }
// }

static int bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;
    if (s_media_mode && s_connections != 0) return 0;

    // 获取MAC地址
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);

    // 厂商数据
    // uint8_t mfg_data[10];
    uint8_t mfg_data[8];
    memset(mfg_data, 0, sizeof(mfg_data));

    mfg_data[0] = 0xE5;
    mfg_data[1] = 0x02;
    memcpy(&mfg_data[2], mac, 6);
    // mfg_data[8] = 0x01;
    // mfg_data[9] = 0x10;

    // ========== 广播包：只放必要信息 ==========
    memset(&fields, 0, sizeof fields);

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // 只放UUID
    // fields.uuids16             = (ble_uuid16_t[]){BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)};
    // fields.num_uuids16         = 1;
    // fields.uuids16_is_complete = 1;

    ble_uuid128_t stackchan_uuid     = BLE_UUID128_INIT(STACKCHAN_SVC_UUID_BASE);
    ble_uuid128_t stackchan_uuid_alt = BLE_UUID128_INIT(STACKCHAN_SVC_UUID_BASE_ALT);

    if (s_media_mode) {
        fields.sol_uuids128 = &ams_service_uuid;
        fields.sol_num_uuids128 = 1;
    } else if (s_use_alt_uuid) {
        fields.uuids128 = &stackchan_uuid_alt;
    } else {
        fields.uuids128 = &stackchan_uuid;
    }

    fields.num_uuids128         = s_media_mode ? 0 : 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return rc;
    }

    // ========== 扫描响应包：放详细信息 ==========
    memset(&rsp_fields, 0, sizeof rsp_fields);

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    const char *name            = s_media_mode ? "Botifier Music" : ble_svc_gap_device_name();
    rsp_fields.name             = (uint8_t *)name;
    rsp_fields.name_len         = strlen(name);
    rsp_fields.name_is_complete = 1;
#endif

    // TX Power放在扫描响应
    rsp_fields.tx_pwr_lvl_is_present = 1;
    rsp_fields.tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // 厂商数据放在扫描响应
    rsp_fields.mfg_data     = mfg_data;
    rsp_fields.mfg_data_len = sizeof(mfg_data);

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting scan response data; rc=%d\n", rc);
        return rc;
    }

    // 启动广播
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return rc;
    }
    return 0;
}

#endif

#if MYNEWT_VAL(BLE_POWER_CONTROL)
static void bleprph_power_control(uint16_t conn_handle)
{
    int rc;

    rc = ble_gap_read_remote_transmit_power_level(conn_handle, 0x01);  // Attempting on LE 1M phy
    assert(rc == 0);

    rc = ble_gap_set_transmit_power_reporting_enable(conn_handle, 0x1, 0x1);
    assert(rc == 0);
}
#endif

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
#if NIMBLE_BLE_CONNECT
    struct ble_gap_conn_desc desc;
    int rc;
#endif

    /* Media connections use the same host callback, but must not masquerade
     * as the legacy control/configuration client's connection. */
    bool media = false;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            media = s_media_mode;
            if (event->connect.status == 0) ++s_connections;
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            media = botifier_ams_owns_connection(event->disconnect.conn.conn_handle);
            if (s_connections != 0) --s_connections;
            if (s_legacy_pending) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_legacy_event);
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            media = botifier_ams_owns_connection(event->enc_change.conn_handle);
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            if (botifier_ams_owns_connection(event->repeat_pairing.conn_handle)) {
                /* Re-pairing requires explicit FORGET; never evict another bond. */
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            break;
        default:
            break;
    }
    botifier_ams_gap_event(event);
    if (media) {
        if (event->type == BLE_GAP_EVENT_DISCONNECT ||
            (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status != 0)) {
#if !CONFIG_EXAMPLE_EXTENDED_ADV
            bleprph_advertise();
#endif
        }
        return 0;
    }

    switch (event->type) {
#if NIMBLE_BLE_CONNECT
        case BLE_GAP_EVENT_CONNECT:
            /* A new connection was established or a connection attempt failed. */
            MODLOG_DFLT(INFO, "connection %s; status=%d ", event->connect.status == 0 ? "established" : "failed",
                        event->connect.status);
            if (event->connect.status == 0) {
                rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
                assert(rc == 0);
                bleprph_print_conn_desc(&desc);
                stackchan_ble_set_conn_handle(event->connect.conn_handle);
            }
            MODLOG_DFLT(INFO, "\n");

            if (event->connect.status != 0) {
                /* Connection failed; resume advertising. */
#if CONFIG_EXAMPLE_EXTENDED_ADV
                ext_bleprph_advertise();
#else
                bleprph_advertise();
#endif
            }

#if MYNEWT_VAL(BLE_POWER_CONTROL)
            bleprph_power_control(event->connect.conn_handle);
#endif
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
            bleprph_print_conn_desc(&event->disconnect.conn);
            stackchan_ble_set_conn_handle(BLE_HS_CONN_HANDLE_NONE);
            MODLOG_DFLT(INFO, "\n");

            /* Connection terminated; resume advertising. */
#if CONFIG_EXAMPLE_EXTENDED_ADV
            ext_bleprph_advertise();
#else
            bleprph_advertise();
#endif
            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE:
            /* The central has updated the connection parameters. */
            MODLOG_DFLT(INFO, "connection updated; status=%d ", event->conn_update.status);
            rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);
            MODLOG_DFLT(INFO, "\n");
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            MODLOG_DFLT(INFO, "advertise complete; reason=%d", event->adv_complete.reason);
#if CONFIG_EXAMPLE_EXTENDED_ADV
            ext_bleprph_advertise();
#else
            bleprph_advertise();
#endif
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            /* Encryption has been enabled or disabled for this connection. */
            MODLOG_DFLT(INFO, "encryption change event; status=%d ", event->enc_change.status);
            rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);
            MODLOG_DFLT(INFO, "\n");
            return 0;

        case BLE_GAP_EVENT_NOTIFY_TX:
            MODLOG_DFLT(INFO,
                        "notify_tx event; conn_handle=%d attr_handle=%d "
                        "status=%d is_indication=%d",
                        event->notify_tx.conn_handle, event->notify_tx.attr_handle, event->notify_tx.status,
                        event->notify_tx.indication);
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            MODLOG_DFLT(INFO,
                        "subscribe event; conn_handle=%d attr_handle=%d "
                        "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                        event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.reason,
                        event->subscribe.prev_notify, event->subscribe.cur_notify, event->subscribe.prev_indicate,
                        event->subscribe.cur_indicate);
            return 0;

        case BLE_GAP_EVENT_MTU:
            MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n", event->mtu.conn_handle,
                        event->mtu.channel_id, event->mtu.value);
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
            /* We already have a bond with the peer, but it is attempting to
             * establish a new secure link.  This app sacrifices security for
             * convenience: just throw away the old bond and accept the new link.
             */

            /* Delete the old bond. */
            rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            assert(rc == 0);
            ble_store_util_delete_peer(&desc.peer_id_addr);

            /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
             * continue with the pairing operation.
             */
            return BLE_GAP_REPEAT_PAIRING_RETRY;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI(tag, "PASSKEY_ACTION_EVENT started");
            struct ble_sm_io pkey = {0};
            int key               = 0;

            if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                pkey.action  = event->passkey.params.action;
                pkey.passkey = 123456;  // This is the passkey to be entered on peer
                ESP_LOGI(tag, "Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
            } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                ESP_LOGI(tag, "Passkey on device's display: %" PRIu32, event->passkey.params.numcmp);
                ESP_LOGI(tag,
                         "Accept or reject the passkey through console in this "
                         "format -> key Y or key N");
                pkey.action = event->passkey.params.action;
                if (scli_receive_key(&key)) {
                    pkey.numcmp_accept = key;
                } else {
                    pkey.numcmp_accept = 0;
                    ESP_LOGE(tag, "Timeout! Rejecting the key");
                }
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
            } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
                static uint8_t tem_oob[16] = {0};
                pkey.action                = event->passkey.params.action;
                for (int i = 0; i < 16; i++) {
                    pkey.oob[i] = tem_oob[i];
                }
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
            } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
                ESP_LOGI(tag, "Enter the passkey through console in this format-> key 123456");
                pkey.action = event->passkey.params.action;
                if (scli_receive_key(&key)) {
                    pkey.passkey = key;
                } else {
                    pkey.passkey = 0;
                    ESP_LOGE(tag, "Timeout! Passing 0 as the key");
                }
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
            }
            return 0;

        case BLE_GAP_EVENT_AUTHORIZE:
            MODLOG_DFLT(INFO, "authorize event: conn_handle=%d attr_handle=%d is_read=%d", event->authorize.conn_handle,
                        event->authorize.attr_handle, event->authorize.is_read);

            /* The default behaviour for the event is to reject authorize request */
            event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
            return 0;

#if MYNEWT_VAL(BLE_POWER_CONTROL)
        case BLE_GAP_EVENT_TRANSMIT_POWER:
            MODLOG_DFLT(INFO,
                        "Transmit power event : status=%d conn_handle=%d reason=%d "
                        "phy=%d power_level=%x power_level_flag=%d delta=%d",
                        event->transmit_power.status, event->transmit_power.conn_handle, event->transmit_power.reason,
                        event->transmit_power.phy, event->transmit_power.transmit_power_level,
                        event->transmit_power.transmit_power_level_flag, event->transmit_power.delta);
            return 0;

        case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
            MODLOG_DFLT(INFO,
                        "Pathloss threshold event : conn_handle=%d current path loss=%d "
                        "zone_entered =%d",
                        event->pathloss_threshold.conn_handle, event->pathloss_threshold.current_path_loss,
                        event->pathloss_threshold.zone_entered);
            return 0;
#endif

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
        case BLE_GAP_EVENT_EATT:
            MODLOG_DFLT(INFO, "EATT %s : conn_handle=%d cid=%d", event->eatt.status ? "disconnected" : "connected",
                        event->eatt.conn_handle, event->eatt.cid);
            if (event->eatt.status) {
                /* Abort if disconnected */
                return 0;
            }
            cids[bearers] = event->eatt.cid;
            bearers += 1;
            if (bearers != MYNEWT_VAL(BLE_EATT_CHAN_NUM)) {
                /* Wait until all EATT bearers are connected before proceeding */
                return 0;
            }
            /* Set the default bearer to use for further procedures */
            rc = ble_att_set_default_bearer_using_cid(event->eatt.conn_handle, cids[0]);
            if (rc != 0) {
                MODLOG_DFLT(INFO, "Cannot set default EATT bearer, rc = %d\n", rc);
                return rc;
            }

            return 0;
#endif

#if MYNEWT_VAL(BLE_CONN_SUBRATING)
        case BLE_GAP_EVENT_SUBRATE_CHANGE:
            MODLOG_DFLT(INFO, "Subrate change event : conn_handle=%d status=%d factor=%d",
                        event->subrate_change.conn_handle, event->subrate_change.status,
                        event->subrate_change.subrate_factor);
            return 0;
#endif
#endif
    }
    return 0;
}

static void bleprph_on_reset(int reason)
{
    s_synced = false;
    s_connections = 0;
    botifier_ams_host_reset(reason);
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

#if CONFIG_EXAMPLE_RANDOM_ADDR
static void ble_app_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);

    assert(rc == 0);
}
#endif

static void bleprph_on_sync(void)
{
    int rc;

#if CONFIG_EXAMPLE_RANDOM_ADDR
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();
#endif

    /* Make sure we have proper identity address set (public preferred) */
#if CONFIG_EXAMPLE_RANDOM_ADDR
    rc = ble_hs_util_ensure_addr(1);
#else
    rc = ble_hs_util_ensure_addr(0);
#endif
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }
    s_synced = true;
    botifier_ams_host_synced();
    if (s_legacy_pending) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_legacy_event);

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc                  = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    MODLOG_DFLT(INFO, "Device Address: ");
    print_addr(addr_val);
    MODLOG_DFLT(INFO, "\n");
    /* Begin advertising. */
#if CONFIG_EXAMPLE_EXTENDED_ADV
    ext_bleprph_advertise();
#else
    bleprph_advertise();
#endif
}

void bleprph_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

static void bleprph_legacy_request(struct ble_npl_event *event)
{
    (void)event;
    botifier_ams_legacy_requested();
    /* Changing an already-registered legacy UUID needs the explicit rebuild
     * below; advertising a different UUID alone breaks app provisioning. */
    const bool alt = atomic_load(&s_requested_alt);
    if (s_use_alt_uuid == alt) {
        s_legacy_pending = false;
        const int rc = ble_prph_media_mode(false);
        if (rc != 0) ESP_LOGE(tag, "Legacy advertising restore failed: %d", rc);
        return;
    }
    s_legacy_pending = true;
    if (!s_synced || s_connections != 0) return;
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(tag, "Stop advertising before GATT rebuild failed: %d", rc);
        return;
    }
    rc = ble_gatts_reset();
    if (rc == 0) {
        /* reset unregisters ATT before stop frees its storage; stop also clears
         * the cumulative count_cfg budgets in this pinned NimBLE release. */
        ble_svc_gap_deinit();
        ble_gatts_stop();
        rc = gatt_svr_init(alt);
    }
#if CONFIG_BT_NIMBLE_GAP_SERVICE
    if (rc == 0) rc = ble_svc_gap_device_name_set("StackChan");
#endif
    if (rc == 0) rc = ble_gatts_start();
    if (rc != 0) {
        ESP_LOGE(tag, "Legacy GATT rebuild failed: %d", rc);
        return;
    }
    s_use_alt_uuid = alt;
    s_legacy_pending = false;
    rc = ble_prph_media_mode(false);
    if (rc != 0) ESP_LOGE(tag, "Legacy advertising restore failed: %d", rc);
}

static int bleprph_store_status(struct ble_store_status_event *event, void *arg)
{
    /* The example's round-robin helper deletes old bonds on overflow. Media
     * pairing must fail instead of deleting configuration/control peers. */
    return s_media_mode ? BLE_HS_ENOMEM : ble_store_util_status_rr(event, arg);
}

static int bleprph_initialize(bool use_alt_uuid)
{
    s_use_alt_uuid = use_alt_uuid;
    int rc;
    esp_err_t ret;

    // /* Initialize NVS — it is used to store PHY calibration data */
    // ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);

    botifier_diag_mark(BOTIFIER_DIAG_BLE, 8, 0);
    botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
    ret = nimble_port_init();
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 9, ret);
    botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return (int)ret;
    }
    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb          = bleprph_on_reset;
    ble_hs_cfg.sync_cb           = bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb   = bleprph_store_status;

    ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;
#ifdef CONFIG_EXAMPLE_BONDING
    ble_hs_cfg.sm_bonding = 1;
    /* Enable the appropriate bit masks to make sure the keys
     * that are needed are exchanged
     */
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
#endif
#ifdef CONFIG_EXAMPLE_MITM
    ble_hs_cfg.sm_mitm = 1;
#endif
#ifdef CONFIG_EXAMPLE_USE_SC
    ble_hs_cfg.sm_sc = 1;
#else
    ble_hs_cfg.sm_sc = 0;
#endif
#ifdef CONFIG_EXAMPLE_RESOLVE_PEER_ADDR
    /* Stores the IRK */
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
#endif
    s_legacy_io = ble_hs_cfg.sm_io_cap;
    s_legacy_bonding = ble_hs_cfg.sm_bonding;
    s_legacy_sc = ble_hs_cfg.sm_sc;
    s_legacy_mitm = ble_hs_cfg.sm_mitm;
    s_legacy_our_keys = ble_hs_cfg.sm_our_key_dist;
    s_legacy_their_keys = ble_hs_cfg.sm_their_key_dist;
    ble_npl_event_init(&s_legacy_event, bleprph_legacy_request, NULL);

#if MYNEWT_VAL(STATIC_PASSKEY) && NIMBLE_BLE_CONNECT
    ble_sm_configure_static_passkey(456789, true);
#endif

#if MYNEWT_VAL(BLE_GATTS)
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 10, 0);
    rc = gatt_svr_init(use_alt_uuid);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 11, rc);
    if (rc != 0) return rc;
#endif

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("StackChan");
    assert(rc == 0);
#endif

    /* XXX Need to have template for store */
    ble_store_config_init();

    botifier_diag_mark(BOTIFIER_DIAG_BLE, 12, 0);
    nimble_port_freertos_init(bleprph_host_task);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 13, 0);

    /* Initialize command line interface to accept input from user */
    rc = scli_init();
    if (rc != ESP_OK) {
        ESP_LOGE(tag, "scli_init() failed");
    }

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
    bearers = 0;
    for (int i = 0; i < MYNEWT_VAL(BLE_EATT_CHAN_NUM); i++) {
        cids[i] = 0;
    }
#endif
    return 0;
}

static int bleprph_ensure(bool use_alt_uuid)
{
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 3, 0);
    portENTER_CRITICAL(&s_init_mux);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 4, 0);
    if (s_init_mutex == NULL) {
        s_init_mutex = xSemaphoreCreateMutexStatic(&s_init_mutex_storage);
        botifier_diag_mark(BOTIFIER_DIAG_BLE, 5, s_init_mutex ? 0 : BLE_HS_ENOMEM);
    }
    portEXIT_CRITICAL(&s_init_mux);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 6, 0);
    if (!s_init_mutex || xSemaphoreTake(s_init_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        botifier_diag_mark(BOTIFIER_DIAG_BLE, 7, BLE_HS_EBUSY);
        botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
        return BLE_HS_EBUSY;
    }
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 7, 0);
    botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
    int rc = 0;
    if (!s_init_attempted) {
        s_init_attempted = true;
        s_init_result = bleprph_initialize(use_alt_uuid);
    }
    rc = s_init_result;
    xSemaphoreGive(s_init_mutex);
    return rc;
}

int ble_prph_ensure_initialized(void)
{
    return bleprph_ensure(false);
}

void ble_prph_init(bool use_alt_uuid)
{
    int rc = bleprph_ensure(use_alt_uuid);
    if (rc != 0) {
        ESP_LOGE(tag, "Shared BLE host initialization failed: %d", rc);
        return;
    }
    atomic_store(&s_requested_alt, use_alt_uuid);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_legacy_event);
}

bool ble_prph_host_synced(void)
{
    return s_synced;
}

int ble_prph_media_mode(bool enabled)
{
    if (enabled && !s_media_mode && s_connections != 0) return BLE_HS_EBUSY;
    s_media_mode = enabled;
    ble_hs_cfg.sm_io_cap = enabled ? BLE_HS_IO_NO_INPUT_OUTPUT : s_legacy_io;
    ble_hs_cfg.sm_bonding = enabled ? 1 : s_legacy_bonding;
    ble_hs_cfg.sm_sc = enabled ? 1 : s_legacy_sc;
    ble_hs_cfg.sm_mitm = enabled ? 0 : s_legacy_mitm;
    ble_hs_cfg.sm_our_key_dist = enabled ? (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID) : s_legacy_our_keys;
    ble_hs_cfg.sm_their_key_dist = enabled ? (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID) : s_legacy_their_keys;
    if (!s_synced) return 0;
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) return rc;
#if !CONFIG_EXAMPLE_EXTENDED_ADV
    return bleprph_advertise();
#else
    return BLE_HS_ENOTSUP;
#endif
}
