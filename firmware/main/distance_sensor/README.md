# Minimal VL53L1X driver

`vl53l1x.h/.cpp` implements only fixed-mode ranging using the project's existing
ESP-IDF synchronous I2C master APIs. It has no Arduino/framework or new component
dependency. The caller creates and owns the bus/device and serializes all driver
calls. Keep the borrowed device alive until `stop()` returns. Bus configuration,
task scheduling, reminders, UI, and USB logging belong to the caller.

## Source and license

The register definitions, complete 91-byte initialization table, VHV calibration
sequence, Long-mode settings, 50 ms budget constants, intermeasurement conversion,
GPIO data-ready polarity algorithm, and valid-status mapping are adapted directly
from STMicroelectronics' Ultra Lite Driver implementation published by stm32duino:

- Repository: https://github.com/stm32duino/VL53L1X
- Exact commit: `5aa46e743913186c448aaf86dd0f12071fddc0a2`
- Implementation: https://github.com/stm32duino/VL53L1X/blob/5aa46e743913186c448aaf86dd0f12071fddc0a2/src/vl53l1x_class.cpp
- Register definitions: https://github.com/stm32duino/VL53L1X/blob/5aa46e743913186c448aaf86dd0f12071fddc0a2/src/vl53l1x_class.h
- Upstream license: https://github.com/stm32duino/VL53L1X/blob/5aa46e743913186c448aaf86dd0f12071fddc0a2/LICENSE.md
- Copyright: 2018 STMicroelectronics; BSD-3-Clause. Full terms are retained in
  [LICENSE](LICENSE) and must accompany binary distributions as required there.
- Retrieved 2026-10-30; source SHA256:
  `3e4d7d6fa0bee4b279fdd1c095b40db615c694441007995d0a38cd378f64a317`
  (`src/vl53l1x_class.cpp`),
  `bba385fc073ff3ef02a3d382802e35a4ce53b0141811ab099712b8b875934409`
  (`src/vl53l1x_class.h`).

Reference functions are `VL53L1X_SensorInit`, `VL53L1X_SetDistanceMode`,
`VL53L1X_SetTimingBudgetInMs`, `VL53L1X_SetInterMeasurementInMs`,
`VL53L1X_GetInterruptPolarity`, `VL53L1X_CheckForDataReady`,
`VL53L1X_GetSensorId`, `VL53L1X_GetDistance`, and `VL53L1X_GetRangeStatus`.
This adaptation checks every I/O result and bounds the upstream calibration loop.
The unchanged contiguous configuration table is sent in one I2C burst instead of
91 individual register writes. Register addresses and multi-byte values are sent
most-significant byte first, as in ST's platform accessors.

## Register selection and behavior

| Register | Purpose |
| --- | --- |
| `0x00E5` bit 0 | Sensor firmware boot-ready check, 1000 ms wait budget |
| `0x010F..0x0110` | Require 16-bit model/module ID `0xEACC` |
| `0x002D..0x0087` | Full ST default configuration, including new-sample interrupt mode |
| `0x002E`, `0x002F` | Set bit 0 to 1 for AVDD-level I2C/GPIO pads |
| `0x0030` bit 4; `0x0031` bit 0 | Read interrupt polarity and data-ready status together |
| `0x0087` | `0x40` start timed ranging; `0x00` stop |
| `0x0086` | Write `0x01` to acknowledge a consumed result |
| `0x0008`, `0x000B` | ST post-VHV settings `0x09`, `0x00` |
| `0x004B`, `0x0060`, `0x0063`, `0x0069` | Long mode: `0x0A`, `0x0F`, `0x0D`, `0xB8` |
| `0x0078`, `0x007A` | Long mode: `0x0F0D`, `0x0E0E` |
| `0x005E`, `0x0061` | Long-mode 50 ms budget: `0x00AD`, `0x00C6` |
| `0x00DE..0x00DF`, `0x006C..0x006F` | Read oscillator calibration, mask `0x03FF`, write `clock * 100 * 1.075` for 100 ms period |
| `0x0089..0x0097` | One 15-byte result read: original status byte at offset 0; distance in mm at offsets 13/14 |

The ToF4M board schematic shows 3.3 V AVDD and 4.7 kohm pull-ups to 3.3 V.
The pad-voltage overrides follow ST's comments at default table entries
`0x002E/0x002F`; the base table itself is unchanged.
Primary hardware source: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/626/UNIT-ToF4M.pdf

Long mode is deliberate: the application permits thresholds up to 4000 mm;
Short mode would limit usable far-distance recovery samples. Long mode with a
50 ms timing budget and 100 ms period does not guarantee 4 m range on every
surface or under strong ambient light. A 400 mm default reminder threshold is an
application choice. No offset/crosstalk calibration or cover glass compensation
is added here.

## API and failures

- `initialize(device)` waits for boot, checks ID, writes ST defaults, performs one
  VHV calibration (1000 ms data-ready wait), selects Long/50 ms/100 ms and starts
  ranging. It is a blocking task-context operation; do not call under an LVGL
  lock or from an interrupt. Reinitialization first calls `stop()`.
- Every I2C transaction has a **50 ms** timeout. Boot and calibration loops use
  monotonic deadlines and yield at least one FreeRTOS tick between checks. A wait
  can exceed its deadline by its final transaction (at most its 50 ms timeout)
  plus scheduler latency; configuration transactions also take bounded time.
- Null device: `ESP_ERR_INVALID_ARG`. Wrong ID: `ESP_ERR_NOT_SUPPORTED`, with no
  configuration writes to that unidentified device. Zero calibrated oscillator:
  `ESP_ERR_INVALID_RESPONSE`. Boot/calibration timeout: `ESP_ERR_TIMEOUT`.
  I2C errors are returned unchanged; no assert/abort is used.
- Initialization failure after ID verification makes a bounded, best-effort
  stop and returns the original error. The device handle remains caller-owned.
- `poll(reading, ready)` resets both outputs before checking state or doing I/O.
  Before successful initialization / after stop it returns `ESP_ERR_INVALID_STATE`.
  Not ready: `ESP_OK`, `ready=false`, default `Reading`; no old range is returned.
- A ready frame is read and its interrupt cleared before publication. Failure
  reading or acknowledging it returns the I2C error with default outputs. A frame
  is never published and then retried merely because its acknowledge failed.
- A consumed optical/quality failure is **`ESP_OK`, `ready=true`, `valid=false`**;
  the raw status and reported distance remain available for diagnostics. Only
  ST's `(raw_status & 0x1F) == 9` (normalized range status 0) is accepted, and only
  for **40..4000 mm inclusive**. All other statuses and out-of-range values are
  invalid; they must not trigger a near-distance alarm or count as recovery.
- `stop()` is idempotent, makes at most one 50 ms write, clears driver state and
  forgets the borrowed handle even on error. If it fails, hardware ranging may
  still be active; the caller must decide whether/how to retry or reinitialize.

## Integration and verification boundary

For StackChan, use the external red PORT A with 7-bit address `0x29`, 100 kHz,
SDA GPIO2 / SCL GPIO1. The internal board bus already uses controller 1 on
SDA12/SCL11; create the external bus on controller 0. Do not replace the internal
bus or let the driver remove either bus/device. Use a bus with no asynchronous
transaction callbacks; the stack buffers here require synchronous operations.

Verification performed on 2026-10-30:

- All 91 configuration bytes compared programmatically against the pinned ST
  source; identical. The retained license text also matches the pinned license.
- Upstream source SHA256 checked against the values recorded above.
- The driver translation unit passed the existing Xtensa ESP32-S3 GCC 14.2.0
  toolchain with the firmware's generated `compile_commands.json` flags plus
  `-fsyntax-only -Werror`.
- The complete ESP-IDF firmware, including the independent sampler and standalone
  proximity expression, built successfully. Current binary size, SHA256 and build
  log are recorded in the workspace's `environment.json`.
- The actual driver compiled under MSVC Release against test-only I2C/time fakes.
  All 11 scenario groups passed: identification, bounded waits, configuration,
  transaction failure injection, polarity/status/range boundaries, freshness,
  read/acknowledge failures and stop behavior. The additional pure C++ proximity
  policy passed 16 scenario groups, including median warmup, dwell, hysteresis,
  invalid samples, timeouts and uptime rollover. Together with the existing
  motion test, CTest passed 3/3. Run `scripts/Test-Host.ps1` to reproduce.

The sensor had not been connected at the last user confirmation; electrical operation, actual measurement cadence,
calibration completion, optical range/status behavior and repeated-frame handling
on physical hardware remain **unverified**. Firmware integration/build and COM
output are owned by the caller; this directory does not install or launch an app.
