#pragma once
#include <cstdint>

class Hal {
public:
    std::uint32_t millis();
};
Hal& GetHAL();
