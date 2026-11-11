/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <distance_sensor/service.h>
#include <apps/common/idle_program/service.h>
#include <apps/common/idle_program/view.h>
#include <apps/common/codex_weekly/service.h>
#include <apps/common/codex_weekly/network_debug.h>
#include <apps/common/lyrics/service.h>
#include <apps/common/runtime_diagnostics.h>
#include <apps/common/panic_diagnostics.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Preserve the last boot's numeric breadcrumbs before HAL starts BLE/tasks.
    botifier_panic_boot();
    botifier_diag_boot();

    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Scheduled sensing is independent of Mooncake apps, including AI mode.
    const esp_err_t distance_result = distance_sensor::start();
    if (distance_result != ESP_OK) {
        mclog::tagError("distance", "sampler could not start: {}", esp_err_to_name(distance_result));
    }

    const esp_err_t idle_result = idle_program::start();
    if (idle_result != ESP_OK) {
        mclog::tagError("idle", "program service could not start: {}", esp_err_to_name(idle_result));
    }

    const esp_err_t weekly_result = codex_weekly::start();
    if (weekly_result != ESP_OK) {
        mclog::tagError("weekly", "native account service could not start: {}", esp_err_to_name(weekly_result));
    } else {
        codex_weekly::network_debug::start();
    }

    const esp_err_t lyrics_result = lyrics::start();
    if (lyrics_result != ESP_OK) {
        mclog::tagError("lyrics", "media service could not start: {}", esp_err_to_name(lyrics_result));
    }

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    const bool skip_mooncake =
        GetHAL().getXiaozhiConfig().startAiAgentOnBoot && GetHAL().getWarmRebootTarget() < 0;

    if (!skip_mooncake) {
        // Install apps
        GetMooncake().installApp(std::make_unique<AppLauncher>());
        GetMooncake().installApp(std::make_unique<AppAiAgent>());
        GetMooncake().installApp(std::make_unique<AppAvatar>());
        GetMooncake().installApp(std::make_unique<AppEspnowControl>());
        GetMooncake().installApp(std::make_unique<AppAppCenter>());
        GetMooncake().installApp(std::make_unique<AppEzdata>());
        GetMooncake().installApp(std::make_unique<AppDance>());
        GetMooncake().installApp(std::make_unique<AppSetup>());
        GetMooncake().installApp(std::make_unique<AppDistance>());

        // Main loop
        while (1) {
            GetHAL().feedTheDog();
            GetHAL().updateHeapStatusLog();

            GetMooncake().update();

            if (GetHAL().isXiaozhiStartRequested()) {
                break;
            }
        }

        // Uninstall all apps and destroy mooncake
        GetMooncake().uninstallAllApps();
        DestroyMooncake();
    }

    idle_program::set_context({});
    {
        LvglLockGuard lock;
        idle_program::clear_locked();
    }

    // Start xiaozhi, never returns
    GetHAL().startXiaozhi();
}
