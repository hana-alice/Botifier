#pragma once
#include <cstddef>
#include <cstdlib>
#include <cassert>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
namespace fake {
inline bool fail_media_allocation = false;
inline int media_allocation_attempts = 0;
}
inline void* heap_caps_malloc(std::size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(size == 6144);
    ++fake::media_allocation_attempts;
    return fake::fail_media_allocation ? nullptr : std::malloc(size);
}
