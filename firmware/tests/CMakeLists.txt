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
