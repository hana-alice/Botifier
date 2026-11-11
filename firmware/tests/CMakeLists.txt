cmake_minimum_required(VERSION 3.16)

project(stackchan_firmware_tests LANGUAGES CXX)

enable_testing()

add_executable(head_touch_filter_test head_touch_filter_test.cpp)
target_include_directories(head_touch_filter_test PRIVATE ../main)
target_compile_features(head_touch_filter_test PRIVATE cxx_std_17)
add_test(NAME head_touch_filter_test COMMAND $<TARGET_FILE:head_touch_filter_test>)

add_executable(si12t_driver_test si12t_driver_test.cpp ../main/hal/drivers/Si12T/Si12T.cpp)
target_include_directories(si12t_driver_test PRIVATE fakes/si12t ../main)
target_compile_features(si12t_driver_test PRIVATE cxx_std_20)
add_test(NAME si12t_driver_test COMMAND $<TARGET_FILE:si12t_driver_test>)

add_executable(motion_math_test
    motion_math_test.cpp
    ../main/stackchan/motion/motion_math.cpp
)

target_include_directories(motion_math_test PRIVATE
    ../main
)

target_compile_features(motion_math_test PRIVATE cxx_std_17)

add_test(NAME motion_math_test COMMAND $<TARGET_FILE:motion_math_test>)

# Compile the actual sensor driver against a deterministic I2C/time model.
# The fakes apply only to this target; motion and firmware builds stay unchanged.
add_executable(vl53l1x_driver_test
    vl53l1x_driver_test.cpp
    ../main/distance_sensor/vl53l1x.cpp
)
target_include_directories(vl53l1x_driver_test PRIVATE fakes ../main)
target_compile_features(vl53l1x_driver_test PRIVATE cxx_std_17)
add_test(NAME vl53l1x_driver_test COMMAND $<TARGET_FILE:vl53l1x_driver_test>)

add_executable(proximity_policy_test proximity_policy_test.cpp)
target_include_directories(proximity_policy_test PRIVATE ../main)
target_compile_features(proximity_policy_test PRIVATE cxx_std_17)
add_test(NAME proximity_policy_test COMMAND $<TARGET_FILE:proximity_policy_test>)

add_executable(distance_snapshot_test distance_snapshot_test.cpp)

target_include_directories(distance_snapshot_test PRIVATE fakes ../main)

target_compile_features(distance_snapshot_test PRIVATE cxx_std_17)
add_test(NAME distance_snapshot_test COMMAND $<TARGET_FILE:distance_snapshot_test>)

add_executable(distance_schedule_test distance_schedule_test.cpp)
target_include_directories(distance_schedule_test PRIVATE ../main)
target_compile_features(distance_schedule_test PRIVATE cxx_std_17)
add_test(NAME distance_schedule_test COMMAND $<TARGET_FILE:distance_schedule_test>)

add_executable(distance_service_test distance_service_test.cpp)
target_include_directories(distance_service_test PRIVATE fakes/distance_service ../main)
target_compile_features(distance_service_test PRIVATE cxx_std_17)
add_test(NAME distance_service_test COMMAND $<TARGET_FILE:distance_service_test>)

# Exercise real avatar ownership; only the element math dependency is replaced.
add_executable(avatar_lifetime_test avatar_lifetime_test.cpp)

target_include_directories(avatar_lifetime_test PRIVATE fakes/avatar_ui ../main)

target_compile_features(avatar_lifetime_test PRIVATE cxx_std_17)
add_test(NAME avatar_lifetime_test COMMAND $<TARGET_FILE:avatar_lifetime_test>)

add_executable(pet_motion_test pet_motion_test.cpp)

target_include_directories(pet_motion_test PRIVATE ../main)

target_compile_features(pet_motion_test PRIVATE cxx_std_17)
add_test(NAME pet_motion_test COMMAND $<TARGET_FILE:pet_motion_test>)

add_executable(touch_reaction_test touch_reaction_test.cpp)
target_include_directories(touch_reaction_test PRIVATE ../main)
target_compile_features(touch_reaction_test PRIVATE cxx_std_17)
add_test(NAME touch_reaction_test COMMAND $<TARGET_FILE:touch_reaction_test>)

add_executable(alert_head_motion_test alert_head_motion_test.cpp)
target_include_directories(alert_head_motion_test PRIVATE ../main)
target_compile_features(alert_head_motion_test PRIVATE cxx_std_17)
add_test(NAME alert_head_motion_test COMMAND $<TARGET_FILE:alert_head_motion_test>)

# Compile the real modifier headers against isolated avatar/motion hardware
# adapters. COPYONLY preserves their relative includes and reconfigures on edit.
set(legacy_root "${CMAKE_CURRENT_BINARY_DIR}/legacy-interactions")
file(COPY fakes/legacy_interactions/stackchan DESTINATION "${legacy_root}")
foreach(header modifiable.h modifiers/head_pet.h modifiers/imu.h)
    configure_file("../main/stackchan/${header}" "${legacy_root}/stackchan/${header}" COPYONLY)
endforeach()
add_executable(legacy_interaction_test legacy_interaction_test.cpp)
target_include_directories(legacy_interaction_test PRIVATE
    "${legacy_root}" fakes/legacy_interactions ../components/smooth_ui_toolkit/src)
target_compile_features(legacy_interaction_test PRIVATE cxx_std_17)
add_test(NAME legacy_interaction_test COMMAND $<TARGET_FILE:legacy_interaction_test>)

add_executable(snap_detector_test snap_detector_test.cpp)
target_include_directories(snap_detector_test PRIVATE ../main)
target_compile_features(snap_detector_test PRIVATE cxx_std_17)
add_test(NAME snap_detector_test COMMAND $<TARGET_FILE:snap_detector_test>)

add_executable(intro_clip_test intro_clip_test.cpp)
target_include_directories(intro_clip_test PRIVATE ../main)
target_compile_features(intro_clip_test PRIVATE cxx_std_17)
add_test(NAME intro_clip_test COMMAND $<TARGET_FILE:intro_clip_test>
    "${CMAKE_CURRENT_SOURCE_DIR}/../main/apps/app_distance/media/kuro_intro.bin"
    "${CMAKE_CURRENT_SOURCE_DIR}/../main/apps/common/idle_program/media/shorekeeper_intro.bin")

add_executable(camera_rgb_test camera_rgb_test.cpp)
target_include_directories(camera_rgb_test PRIVATE ../main)
target_compile_features(camera_rgb_test PRIVATE cxx_std_17)
add_test(NAME camera_rgb_test COMMAND $<TARGET_FILE:camera_rgb_test>)

add_executable(nfc_gate_test nfc_gate_test.cpp)
target_include_directories(nfc_gate_test PRIVATE ../main)
target_compile_features(nfc_gate_test PRIVATE cxx_std_17)
add_test(NAME nfc_gate_test COMMAND $<TARGET_FILE:nfc_gate_test>)

add_executable(idle_program_test idle_program_test.cpp)
target_include_directories(idle_program_test PRIVATE ../main)
target_compile_features(idle_program_test PRIVATE cxx_std_17)
add_test(NAME idle_program_test COMMAND $<TARGET_FILE:idle_program_test>)

add_executable(idle_wire_test idle_wire_test.cpp)
target_include_directories(idle_wire_test PRIVATE ../main)
target_compile_features(idle_wire_test PRIVATE cxx_std_17)
add_test(NAME idle_wire_test COMMAND $<TARGET_FILE:idle_wire_test>)

add_executable(rest_head_motion_test rest_head_motion_test.cpp)
target_include_directories(rest_head_motion_test PRIVATE ../main)
target_compile_features(rest_head_motion_test PRIVATE cxx_std_17)
add_test(NAME rest_head_motion_test COMMAND $<TARGET_FILE:rest_head_motion_test>)

add_executable(edge_swipe_test edge_swipe_test.cpp)
target_include_directories(edge_swipe_test PRIVATE ../main)
target_compile_features(edge_swipe_test PRIVATE cxx_std_17)
add_test(NAME edge_swipe_test COMMAND $<TARGET_FILE:edge_swipe_test>)

# Production Servo/Motion and actual UITK springs; only the clock and hardware
# position writer are simulated. These include paths apply to this target only.
set(servo_uitk_root "${CMAKE_CURRENT_SOURCE_DIR}/../components/smooth_ui_toolkit/src")
add_executable(servo_timed_motion_test
    servo_timed_motion_test.cpp
    ../main/stackchan/motion/servo.cpp
    ../main/stackchan/motion/motion.cpp
    ../main/stackchan/motion/motion_math.cpp
    "${servo_uitk_root}/core/animation/animate_value/animate_value.cpp"
    "${servo_uitk_root}/core/animation/animate/animate.cpp"
    "${servo_uitk_root}/core/animation/generators/spring/spring.cpp"
    "${servo_uitk_root}/core/animation/generators/easing/easing.cpp"
    "${servo_uitk_root}/core/easing/ease.cpp"
    "${servo_uitk_root}/core/hal/hal.cpp")
target_include_directories(servo_timed_motion_test PRIVATE fakes/servo_motion ../main "${servo_uitk_root}")
target_compile_definitions(servo_timed_motion_test PRIVATE _USE_MATH_DEFINES SMOOTH_UI_TOOLKIT_ENABLE_DEFAULT_HAL=0)
target_compile_features(servo_timed_motion_test PRIVATE cxx_std_17)
add_test(NAME servo_timed_motion_test COMMAND $<TARGET_FILE:servo_timed_motion_test>)

add_executable(content_timing_test content_timing_test.cpp)
target_include_directories(content_timing_test PRIVATE ../main)
target_compile_features(content_timing_test PRIVATE cxx_std_17)
add_test(NAME content_timing_test COMMAND $<TARGET_FILE:content_timing_test>)

# Extract the contiguous six I/O definitions; no ESP driver headers or Python
# dependency are needed. Reconfigure whenever the production source changes.
set(scserial_root "${CMAKE_CURRENT_SOURCE_DIR}/../main/hal/drivers/FTServo_Arduino/src")
set(scserial_source "${scserial_root}/SCSerial.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${scserial_source}")
file(READ "${scserial_source}" scserial_text)
string(FIND "${scserial_text}" "int SCSerial::readSCS(" scserial_start)
if(scserial_start LESS 0)
    message(FATAL_ERROR "SCSerial I/O definitions were not found")
endif()
string(SUBSTRING "${scserial_text}" ${scserial_start} -1 scserial_io)
string(REGEX MATCHALL "SCSerial::[a-zA-Z_]+\\(" scserial_methods "${scserial_io}")
list(LENGTH scserial_methods scserial_method_count)
if(NOT scserial_method_count EQUAL 6)
    message(FATAL_ERROR "SCSerial I/O extraction expects exactly six definitions; review its source layout")
endif()
string(REPLACE "SCSerial::" "HostSerial::" scserial_io "${scserial_io}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/scserial_read_impl.inc" "${scserial_io}")
add_executable(scserial_read_test scserial_read_test.cpp "${scserial_root}/SCS.cpp")
target_include_directories(scserial_read_test PRIVATE "${scserial_root}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(scserial_read_test PRIVATE cxx_std_17)
add_test(NAME scserial_read_test COMMAND $<TARGET_FILE:scserial_read_test>)

add_executable(codex_weekly_protocol_test
    codex_weekly_protocol_test.cpp
    ../main/apps/common/codex_weekly/protocol.cpp)
target_include_directories(codex_weekly_protocol_test PRIVATE ../main ../components/ArduinoJson/src)
target_compile_features(codex_weekly_protocol_test PRIVATE cxx_std_17)
add_test(NAME codex_weekly_protocol_test COMMAND $<TARGET_FILE:codex_weekly_protocol_test>)

add_executable(codex_service_core_test codex_service_core_test.cpp)
target_include_directories(codex_service_core_test PRIVATE ../main)
target_compile_features(codex_service_core_test PRIVATE cxx_std_17)
add_test(NAME codex_service_core_test COMMAND $<TARGET_FILE:codex_service_core_test>)

# Exercise production OTA handlers. Platform fakes are isolated to this target;
# ArduinoJson parsing/serialization and the HTTP/OTA control flow remain real.
add_executable(firmware_update_test
    firmware_update_test.cpp
    ../main/apps/common/firmware_update/service.cpp)

target_include_directories(firmware_update_test PRIVATE
    fakes/firmware_update ../main ../components/ArduinoJson/src)
target_compile_definitions(firmware_update_test PRIVATE CONFIG_IDF_FIRMWARE_CHIP_ID=9)
target_compile_features(firmware_update_test PRIVATE cxx_std_17)
add_test(NAME firmware_update_test COMMAND $<TARGET_FILE:firmware_update_test>)

add_executable(lyrics_protocol_test
    lyrics_protocol_test.cpp
    ../main/apps/common/lyrics/protocol.cpp)
target_include_directories(lyrics_protocol_test PRIVATE
    fakes/lyrics ../main ../components/ArduinoJson/src)
target_compile_features(lyrics_protocol_test PRIVATE cxx_std_17)
add_test(NAME lyrics_protocol_test COMMAND $<TARGET_FILE:lyrics_protocol_test>)

add_executable(lyrics_presentation_test lyrics_presentation_test.cpp)
target_include_directories(lyrics_presentation_test PRIVATE ../main)
target_compile_features(lyrics_presentation_test PRIVATE cxx_std_17)
add_test(NAME lyrics_presentation_test COMMAND $<TARGET_FILE:lyrics_presentation_test>)

# Compile the complete generated font for both the explicit PSRAM allocation
# path and the static host-preview path. Only LVGL/ROM/heap boundaries are spies.
foreach(font_target lyrics_font_workspace_test lyrics_font_host_test)
    add_executable(${font_target} lyrics_font_workspace_test.cpp)
    target_include_directories(${font_target} PRIVATE fakes/lyrics_font ../main)
    target_compile_features(${font_target} PRIVATE cxx_std_20)
    add_test(NAME ${font_target} COMMAND $<TARGET_FILE:${font_target}>)
endforeach()
target_compile_definitions(lyrics_font_workspace_test PRIVATE ESP_PLATFORM=1)

# The adapters are isolated to these targets; execute the production callback
# and worker code rather than a parallel model of its behavior.
add_executable(lyrics_ble_test lyrics_ble_test.cpp)
target_include_directories(lyrics_ble_test PRIVATE fakes/lyrics_ble ../main)
target_compile_features(lyrics_ble_test PRIVATE cxx_std_17)
add_test(NAME lyrics_ble_test COMMAND $<TARGET_FILE:lyrics_ble_test>)

add_executable(lyrics_service_test
    lyrics_service_test.cpp
    ../main/apps/common/lyrics/protocol.cpp)
target_include_directories(lyrics_service_test PRIVATE
    fakes/lyrics_service ../main ../components/ArduinoJson/src)
target_compile_features(lyrics_service_test PRIVATE cxx_std_17)
add_test(NAME lyrics_service_test COMMAND $<TARGET_FILE:lyrics_service_test>)

# Keep shared-host compatibility checks tied to the actual small C functions.
# Markers fail closed when their production layout changes.
function(lyrics_extract input start_marker end_marker output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    file(READ "${input}" contents)
    string(FIND "${contents}" "${start_marker}" begin)
    if(begin LESS 0)
        message(FATAL_ERROR "Lyrics compatibility source start marker missing: ${input}")
    endif()
    string(SUBSTRING "${contents}" ${begin} -1 tail)
    string(FIND "${tail}" "${end_marker}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Lyrics compatibility source end marker missing: ${input}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} extracted)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${extracted}")
endfunction()
lyrics_extract("${CMAKE_CURRENT_SOURCE_DIR}/../main/hal/utils/bleprph/gatt_svr.c"
    "int gatt_svr_init(bool use_alt_uuid)\n{" "/**\n * Public API implementations" "lyrics_gatt_init.inc")
lyrics_extract("${CMAKE_CURRENT_SOURCE_DIR}/../main/hal/utils/bleprph/bleprph.c"
    "static void bleprph_legacy_request(struct ble_npl_event *event)\n{" "static int bleprph_store_status(" "lyrics_legacy_request.inc")
add_executable(lyrics_ble_compat_test lyrics_ble_compat_test.cpp)
target_include_directories(lyrics_ble_compat_test PRIVATE fakes/lyrics_ble ../main "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(lyrics_ble_compat_test PRIVATE cxx_std_17)
add_test(NAME lyrics_ble_compat_test COMMAND $<TARGET_FILE:lyrics_ble_compat_test>)

# Existing functional suites use inert breadcrumbs. The actual retained-record
# implementation is compiled separately with controlled reset/heap/time inputs.
foreach(diag_target firmware_update_test lyrics_ble_test lyrics_service_test lyrics_ble_compat_test)
    target_sources(${diag_target} PRIVATE runtime_diagnostics_stub.cpp)
endforeach()
add_executable(runtime_diagnostics_test runtime_diagnostics_test.cpp)
target_include_directories(runtime_diagnostics_test PRIVATE fakes/runtime_diagnostics ../main)
target_compile_features(runtime_diagnostics_test PRIVATE cxx_std_20)
add_test(NAME runtime_diagnostics_test COMMAND $<TARGET_FILE:runtime_diagnostics_test>)

lyrics_extract("${CMAKE_CURRENT_SOURCE_DIR}/../main/apps/common/codex_weekly/network_debug.cpp"
    "// MUSIC_COMMAND_BEGIN:" "// MUSIC_COMMAND_END" "music_command_handler.inc")
lyrics_extract("${CMAKE_CURRENT_SOURCE_DIR}/../main/apps/common/codex_weekly/network_debug.cpp"
    "void diagnostic_boot(" "// PANIC_STATUS_BEGIN:" "diagnostic_status.inc")
add_executable(music_command_test music_command_test.cpp)
target_include_directories(music_command_test PRIVATE
    fakes/music_command fakes/firmware_update ../main ../components/ArduinoJson/src "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(music_command_test PRIVATE cxx_std_17)
add_test(NAME music_command_test COMMAND $<TARGET_FILE:music_command_test>)

add_executable(ble_init_diagnostics_test ble_init_diagnostics_test.cpp)
target_include_directories(ble_init_diagnostics_test PRIVATE
    fakes/ble_init_diagnostics fakes/firmware_update ../main)
target_compile_features(ble_init_diagnostics_test PRIVATE cxx_std_20)
add_test(NAME ble_init_diagnostics_test COMMAND $<TARGET_FILE:ble_init_diagnostics_test>)

lyrics_extract("${CMAKE_CURRENT_SOURCE_DIR}/../main/apps/common/codex_weekly/network_debug.cpp"
    "// PANIC_STATUS_BEGIN:" "// PANIC_STATUS_END" "panic_status.inc")
add_executable(panic_diagnostics_test panic_diagnostics_test.cpp)
target_include_directories(panic_diagnostics_test PRIVATE
    fakes/panic_diagnostics fakes/runtime_diagnostics ../main ../components/ArduinoJson/src "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(panic_diagnostics_test PRIVATE cxx_std_20)
add_test(NAME panic_diagnostics_test COMMAND $<TARGET_FILE:panic_diagnostics_test>)
