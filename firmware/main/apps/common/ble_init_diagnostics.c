#include "runtime_diagnostics.h"
#include <esp_bt.h>
#include <esp_attr.h>
#include <esp_phy_init.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <esp_nimble_hci.h>
#include <nimble/transport_impl.h>

/* Link-wrap only cross-object references from the pinned NimBLE port. Keep the
 * SDK's arguments, return values, order and cleanup unchanged. In particular,
 * esp_nimble_init lives in the same object as its caller and is not wrapped. */
esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t *config);
esp_err_t __real_esp_bt_controller_enable(esp_bt_mode_t mode);
esp_err_t __real_esp_nimble_hci_init(void);
void __real_ble_transport_hs_init(void);
void __real_esp_phy_enable(esp_phy_modem_t modem);
esp_err_t __real_coex_enable(void);
int __real_btdm_controller_enable(esp_bt_mode_t mode);

/* These SDK entry points are shared with Wi-Fi. An internal-DRAM atomic owner
 * limits nested breadcrumbs to the calling BLE task without serializing SDK
 * calls. A concurrent enable cannot replace or clear the first caller's owner. */
static DRAM_ATTR _Atomic(TaskHandle_t) s_enable_task = NULL;

static bool current_enable_task(void)
{
    const TaskHandle_t owner = atomic_load_explicit(&s_enable_task, memory_order_acquire);
    return owner != NULL && !xPortInIsrContext() && owner == xTaskGetCurrentTaskHandle();
}

esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t *config)
{
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 32, 0);
    const esp_err_t result = __real_esp_bt_controller_init(config);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 33, result);
    return result;
}

esp_err_t __wrap_esp_bt_controller_enable(esp_bt_mode_t mode)
{
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 34, 0);
    botifier_diag_sample_memory(BOTIFIER_DIAG_BLE);
    const TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TaskHandle_t expected = NULL;
    const bool owner = task != NULL && !xPortInIsrContext() &&
        atomic_compare_exchange_strong_explicit(&s_enable_task, &expected, task,
                                               memory_order_release, memory_order_relaxed);
    const esp_err_t result = __real_esp_bt_controller_enable(mode);
    if (owner) atomic_store_explicit(&s_enable_task, NULL, memory_order_release);
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 35, result);
    return result;
}

esp_err_t __wrap_esp_nimble_hci_init(void)
{
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 36, 0);
    const esp_err_t result = __real_esp_nimble_hci_init();
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 37, result);
    return result;
}

void __wrap_ble_transport_hs_init(void)
{
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 38, 0);
    __real_ble_transport_hs_init();
    botifier_diag_mark(BOTIFIER_DIAG_BLE, 39, 0);
}

void __wrap_esp_phy_enable(esp_phy_modem_t modem)
{
    const bool trace = modem == PHY_MODEM_BT && current_enable_task();
    if (trace) botifier_diag_mark(BOTIFIER_DIAG_BLE, 40, 0);
    __real_esp_phy_enable(modem);
    if (trace) botifier_diag_mark(BOTIFIER_DIAG_BLE, 41, 0);
}

esp_err_t __wrap_coex_enable(void)
{
    const bool trace = current_enable_task();
    if (trace) botifier_diag_mark(BOTIFIER_DIAG_BLE, 42, 0);
    const esp_err_t result = __real_coex_enable();
    if (trace) botifier_diag_mark(BOTIFIER_DIAG_BLE, 43, result);
    return result;
}

int __wrap_btdm_controller_enable(esp_bt_mode_t mode)
{
    const bool trace = current_enable_task();
    if (trace) botifier_diag_mark(BOTIFIER_DIAG_BLE, 44, 0);
    const int result = __real_btdm_controller_enable(mode);
    if (trace) botifier_diag_mark(BOTIFIER_DIAG_BLE, 45, result);
    return result;
}
