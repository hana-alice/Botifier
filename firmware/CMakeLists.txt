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

# Component Manager canonicalizes override names to lowercase, while the pinned
# M5 CMake files require CamelCase short names. Register their exact managed
# targets before IDF 5.5 expands requirements; leave upstream sources unchanged.
foreach(sc_m5_component M5UnitUnified M5HAL M5Utility)
    string(TOLOWER "${sc_m5_component}" sc_m5_canonical)
    idf_build_set_property(__COMPONENT_NAMES_RESOLVED "${sc_m5_component}" APPEND)
    idf_build_set_property(__COMPONENT_TARGETS_RESOLVED "___idf_m5stack__${sc_m5_canonical}" APPEND)
endforeach()

project(stack-chan)
