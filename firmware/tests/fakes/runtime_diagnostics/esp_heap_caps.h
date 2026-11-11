#pragma once
#include <cstddef>
inline constexpr unsigned MALLOC_CAP_INTERNAL = 1u << 0, MALLOC_CAP_8BIT = 1u << 1;
std::size_t heap_caps_get_free_size(unsigned capabilities);
std::size_t heap_caps_get_largest_free_block(unsigned capabilities);
std::size_t heap_caps_get_minimum_free_size(unsigned capabilities);
