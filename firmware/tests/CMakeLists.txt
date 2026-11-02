cmake_minimum_required(VERSION 3.16)

project(stackchan_firmware_tests LANGUAGES CXX)

enable_testing()

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

# Exercise real avatar ownership; only the element math dependency is replaced.
add_executable(avatar_lifetime_test avatar_lifetime_test.cpp)

target_include_directories(avatar_lifetime_test PRIVATE fakes/avatar_ui ../main)

target_compile_features(avatar_lifetime_test PRIVATE cxx_std_17)
add_test(NAME avatar_lifetime_test COMMAND $<TARGET_FILE:avatar_lifetime_test>)

add_executable(pet_motion_test pet_motion_test.cpp)

target_include_directories(pet_motion_test PRIVATE ../main)

target_compile_features(pet_motion_test PRIVATE cxx_std_17)
add_test(NAME pet_motion_test COMMAND $<TARGET_FILE:pet_motion_test>)

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
