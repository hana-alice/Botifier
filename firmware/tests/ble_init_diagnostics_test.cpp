#include <atomic>
#include <functional>
#include <thread>
// Real production wrapper bodies, with the real SDK calls replaced by bounded
// spies. Actual linker interception is verified separately in the firmware ELF.
extern "C" {
#include "../main/apps/common/ble_init_diagnostics.c"
}
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
void check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)
struct Event {
    uint32_t lane, stage;
    int32_t error;
    bool operator==(const Event&) const = default;
};
std::vector<Event> events;
esp_bt_controller_config_t* observed_config = nullptr;
esp_bt_mode_t observed_mode = ESP_BT_MODE_IDLE;
esp_err_t init_result = ESP_OK, enable_result = ESP_OK, hci_result = ESP_OK;
unsigned init_calls = 0, enable_calls = 0, hci_calls = 0, host_calls = 0, memory_samples = 0;
unsigned phy_calls = 0, coex_calls = 0, btdm_calls = 0;
esp_phy_modem_t observed_modem = PHY_MODEM_WIFI;
esp_bt_mode_t observed_btdm_mode = ESP_BT_MODE_IDLE;
esp_err_t coex_result = ESP_OK;
int btdm_result = 0;
std::function<void()> enable_body;
TaskHandle_t const main_task = reinterpret_cast<TaskHandle_t>(uintptr_t{0x1234});
TaskHandle_t const other_task = reinterpret_cast<TaskHandle_t>(uintptr_t{0x5678});
thread_local TaskHandle_t current_task = main_task;
thread_local bool in_isr = false;

void reset()
{
    events.clear(); observed_config = nullptr; observed_mode = ESP_BT_MODE_IDLE;
    init_result = enable_result = hci_result = ESP_OK;
    init_calls = enable_calls = hci_calls = host_calls = memory_samples = 0;
    phy_calls = coex_calls = btdm_calls = 0;
    observed_modem = PHY_MODEM_WIFI; observed_btdm_mode = ESP_BT_MODE_IDLE;
    coex_result = ESP_OK; btdm_result = 0; enable_body = {};
    current_task = main_task; in_isr = false;
}
void controller_init()
{
    for (const auto result : {ESP_OK, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG, -77}) {
        reset(); init_result = result;
        esp_bt_controller_config_t config{{0x01234567, 0x89abcdef, 0xa5a5a5a5, 0x5a5a5a5a}};
        const auto original = config;
        CHECK(__wrap_esp_bt_controller_init(&config) == result);
        CHECK(observed_config == &config && init_calls == 1);
        CHECK(memory_samples == 0);
        for (unsigned i = 0; i < 4; ++i) CHECK(config.unchanged[i] == original.unchanged[i]);
        CHECK(enable_calls == 0 && hci_calls == 0 && host_calls == 0);
        CHECK(events == std::vector<Event>({{BOTIFIER_DIAG_BLE, 32, 0}, {99, 1, 0},
                                            {BOTIFIER_DIAG_BLE, 33, result}}));
    }
    reset(); init_result = ESP_ERR_INVALID_ARG;
    CHECK(__wrap_esp_bt_controller_init(nullptr) == ESP_ERR_INVALID_ARG);
    CHECK(observed_config == nullptr && init_calls == 1); // pass even invalid input through
}
void controller_enable()
{
    for (const auto mode : {ESP_BT_MODE_IDLE, ESP_BT_MODE_BLE, ESP_BT_MODE_BTDM}) {
        for (const auto result : {ESP_OK, ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE, -88}) {
            reset(); enable_result = result;
            CHECK(__wrap_esp_bt_controller_enable(mode) == result);
            CHECK(observed_mode == mode && enable_calls == 1);
            CHECK(memory_samples == 1);
            CHECK(init_calls == 0 && hci_calls == 0 && host_calls == 0);
            CHECK(events == std::vector<Event>({{BOTIFIER_DIAG_BLE, 34, 0}, {98, 34, 0}, {99, 2, 0},
                                                {BOTIFIER_DIAG_BLE, 35, result}}));
        }
    }
}
void host_layers()
{
    for (const auto result : {ESP_OK, ESP_ERR_NO_MEM, ESP_FAIL, -99}) {
        reset(); hci_result = result;
        CHECK(__wrap_esp_nimble_hci_init() == result);
        CHECK(hci_calls == 1 && init_calls == 0 && enable_calls == 0 && host_calls == 0);
        CHECK(memory_samples == 0);
        CHECK(events == std::vector<Event>({{BOTIFIER_DIAG_BLE, 36, 0}, {99, 3, 0},
                                            {BOTIFIER_DIAG_BLE, 37, result}}));
    }
    reset();
    __wrap_ble_transport_hs_init();
    CHECK(host_calls == 1 && init_calls == 0 && enable_calls == 0 && hci_calls == 0);
    CHECK(memory_samples == 0);
    CHECK(events == std::vector<Event>({{BOTIFIER_DIAG_BLE, 38, 0}, {99, 4, 0},
                                        {BOTIFIER_DIAG_BLE, 39, 0}}));
}
void nested_layers()
{
    for (const auto result : {ESP_OK, ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE, -101}) {
        reset(); coex_result = result; btdm_result = result; enable_result = -102;
        enable_body = [] {
            __wrap_esp_phy_enable(PHY_MODEM_BT);
            CHECK(__wrap_coex_enable() == coex_result);
            CHECK(__wrap_btdm_controller_enable(ESP_BT_MODE_BLE) == btdm_result);
        };
        CHECK(__wrap_esp_bt_controller_enable(ESP_BT_MODE_BLE) == -102);
        CHECK(memory_samples == 1 && phy_calls == 1 && coex_calls == 1 && btdm_calls == 1);
        CHECK(observed_modem == PHY_MODEM_BT && observed_btdm_mode == ESP_BT_MODE_BLE);
        CHECK(events == std::vector<Event>({
            {BOTIFIER_DIAG_BLE, 34, 0}, {98, 34, 0}, {99, 2, 0},
            {BOTIFIER_DIAG_BLE, 40, 0}, {99, 5, PHY_MODEM_BT}, {BOTIFIER_DIAG_BLE, 41, 0},
            {BOTIFIER_DIAG_BLE, 42, 0}, {99, 6, 0}, {BOTIFIER_DIAG_BLE, 43, result},
            {BOTIFIER_DIAG_BLE, 44, 0}, {99, 7, ESP_BT_MODE_BLE}, {BOTIFIER_DIAG_BLE, 45, result},
            {BOTIFIER_DIAG_BLE, 35, -102}}));
        events.clear();
        __wrap_esp_phy_enable(PHY_MODEM_BT);
        CHECK(__wrap_coex_enable() == result);
        CHECK(__wrap_btdm_controller_enable(ESP_BT_MODE_BTDM) == result);
        CHECK(events == std::vector<Event>({{99, 5, PHY_MODEM_BT}, {99, 6, 0},
                                            {99, 7, ESP_BT_MODE_BTDM}})); // owner cleared on error too
        CHECK(observed_btdm_mode == ESP_BT_MODE_BTDM && memory_samples == 1);
    }
}
std::vector<uint32_t> diagnostic_stages()
{
    std::vector<uint32_t> result;
    for (const auto& event : events) if (event.lane == BOTIFIER_DIAG_BLE) result.push_back(event.stage);
    return result;
}
void input_isolation()
{
    reset();
    __wrap_esp_phy_enable(PHY_MODEM_WIFI); __wrap_esp_phy_enable(PHY_MODEM_BT);
    CHECK(__wrap_coex_enable() == ESP_OK);
    CHECK(__wrap_btdm_controller_enable(ESP_BT_MODE_IDLE) == 0);
    CHECK(diagnostic_stages().empty() && memory_samples == 0);
    CHECK(phy_calls == 2 && coex_calls == 1 && btdm_calls == 1);

    reset();
    enable_body = [] {
        __wrap_esp_phy_enable(PHY_MODEM_WIFI); // same task, unrelated modem
        __wrap_esp_phy_enable(PHY_MODEM_IEEE802154);
        current_task = other_task;
        __wrap_esp_phy_enable(PHY_MODEM_BT); __wrap_coex_enable();
        __wrap_btdm_controller_enable(ESP_BT_MODE_BLE);
        current_task = main_task;
        in_isr = true; // ISR reports the interrupted task; it still must not mark
        __wrap_esp_phy_enable(PHY_MODEM_BT); __wrap_coex_enable();
        __wrap_btdm_controller_enable(ESP_BT_MODE_BLE);
        in_isr = false;
        __wrap_esp_phy_enable(PHY_MODEM_BT); __wrap_coex_enable();
        __wrap_btdm_controller_enable(ESP_BT_MODE_BLE);
    };
    CHECK(__wrap_esp_bt_controller_enable(ESP_BT_MODE_BLE) == ESP_OK);
    CHECK(diagnostic_stages() == std::vector<uint32_t>({34, 40, 41, 42, 43, 44, 45, 35}));
    CHECK(phy_calls == 5 && coex_calls == 3 && btdm_calls == 3 && memory_samples == 1);
}
void cross_thread_isolation()
{
    reset();
    enable_body = [] {
        std::thread other([] {
            current_task = other_task;
            for (unsigned i = 0; i < 100; ++i) {
                __wrap_esp_phy_enable(PHY_MODEM_BT); __wrap_coex_enable();
                __wrap_btdm_controller_enable(ESP_BT_MODE_BLE);
            }
        });
        other.join();
        __wrap_esp_phy_enable(PHY_MODEM_BT); __wrap_coex_enable();
        __wrap_btdm_controller_enable(ESP_BT_MODE_BLE);
    };
    CHECK(__wrap_esp_bt_controller_enable(ESP_BT_MODE_BLE) == ESP_OK);
    CHECK(diagnostic_stages() == std::vector<uint32_t>({34, 40, 41, 42, 43, 44, 45, 35}));
    CHECK(phy_calls == 101 && coex_calls == 101 && btdm_calls == 101 && memory_samples == 1);

    reset();
    enable_body = [] {
        if (current_task == main_task) {
            std::thread other([] {
                current_task = other_task;
                __wrap_esp_bt_controller_enable(ESP_BT_MODE_BLE);
            });
            other.join();
        }
        __wrap_esp_phy_enable(PHY_MODEM_BT); __wrap_coex_enable();
        __wrap_btdm_controller_enable(ESP_BT_MODE_BLE);
    };
    CHECK(__wrap_esp_bt_controller_enable(ESP_BT_MODE_BLE) == ESP_OK);
    CHECK(diagnostic_stages() == std::vector<uint32_t>({34, 34, 35, 40, 41, 42, 43, 44, 45, 35}));
    CHECK(enable_calls == 2 && memory_samples == 2);
    CHECK(phy_calls == 2 && coex_calls == 2 && btdm_calls == 2);
}
} // namespace

extern "C" void botifier_diag_mark(uint32_t lane, uint32_t stage, int32_t error)
{ events.push_back({lane, stage, error}); }
extern "C" void botifier_diag_sample_memory(uint32_t lane)
{ ++memory_samples; events.push_back({98, events.empty() ? 0 : events.back().stage, static_cast<int32_t>(lane)}); }
extern "C" esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t* config)
{ ++init_calls; observed_config = config; events.push_back({99, 1, 0}); return init_result; }
extern "C" esp_err_t __real_esp_bt_controller_enable(esp_bt_mode_t mode)
{ ++enable_calls; observed_mode = mode; events.push_back({99, 2, 0});
  if (enable_body) enable_body(); return enable_result; }
extern "C" esp_err_t __real_esp_nimble_hci_init(void)
{ ++hci_calls; events.push_back({99, 3, 0}); return hci_result; }
extern "C" void __real_ble_transport_hs_init(void)
{ ++host_calls; events.push_back({99, 4, 0}); }

extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
extern "C" BaseType_t xPortInIsrContext(void) { return in_isr ? 1 : 0; }
extern "C" void __real_esp_phy_enable(esp_phy_modem_t modem)
{ ++phy_calls; observed_modem = modem; events.push_back({99, 5, static_cast<int32_t>(modem)}); }
extern "C" esp_err_t __real_coex_enable(void)
{ ++coex_calls; events.push_back({99, 6, 0}); return coex_result; }
extern "C" int __real_btdm_controller_enable(esp_bt_mode_t mode)
{ ++btdm_calls; observed_btdm_mode = mode; events.push_back({99, 7, static_cast<int32_t>(mode)}); return btdm_result; }

int main()
{
    try {
        controller_init(); controller_enable(); host_layers();
        nested_layers(); input_isolation(); cross_thread_isolation();
        std::cout << checks << " BLE initialization wrapper checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
