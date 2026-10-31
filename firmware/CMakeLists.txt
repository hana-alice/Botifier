# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.6.0")
add_definitions(-DFIRMWARE_VERSION=\"${PROJECT_VER}\")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# Auto-load a local sdkconfig overlay (git-ignored) when present. Lets
# self-hosted / custom deployments pin CONFIG_STACKCHAN_SERVER_URL,
# CONFIG_OTA_URL, etc. without touching committed defaults.
# Manual equivalent: SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.local" idf.py build
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local"
        CACHE STRING "ESP-IDF sdkconfig default files (auto-extended with local overlay)" FORCE)
    message(STATUS "StackChan: detected sdkconfig.defaults.local, applying overlay")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stack-chan)

# Apply after all components: ESP-DL appends -O3 in its own CMakeLists.
# SHELL keeps this final -Os distinct from IDF's earlier size option.
idf_component_get_property(espdl_lib espressif__esp-dl COMPONENT_LIB)
target_compile_options(${espdl_lib} PRIVATE "SHELL:-Os")

# Keep the scalar/dispatch convolution hot paths at the vendor's speed setting
# while the remaining ESP-DL code stays small enough for the existing OTA slot.
idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
set_property(SOURCE
    "${espdl_dir}/dl/base/dl_base_conv2d.cpp"
    "${espdl_dir}/dl/base/dl_base_depthwise_conv2d.cpp"
    DIRECTORY "${espdl_dir}" APPEND PROPERTY COMPILE_OPTIONS -O3)
