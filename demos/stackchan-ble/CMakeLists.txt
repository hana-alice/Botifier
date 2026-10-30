cmake_minimum_required(VERSION 3.25)
project(stackchan_demos LANGUAGES CXX)

add_executable(stackchan_demo main.cpp ble_link.cpp)
target_compile_features(stackchan_demo PRIVATE cxx_std_20)
target_compile_definitions(stackchan_demo PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(stackchan_demo PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(stackchan_demo PRIVATE windowsapp)
