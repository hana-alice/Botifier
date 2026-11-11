#pragma once
#include <cstdint>
// The host substitutes a real allocated arena for the S3 internal-DRAM window.
extern std::uintptr_t botifier_test_dram_low, botifier_test_dram_high;
#define SOC_DRAM_LOW botifier_test_dram_low
#define SOC_DRAM_HIGH botifier_test_dram_high
