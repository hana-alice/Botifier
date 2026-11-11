// Existing functional suites keep testing their adapter's behavior. RTC storage
// and concurrent diagnostics are exercised separately by runtime_diagnostics_test.
#include <apps/common/runtime_diagnostics.h>
extern "C" void botifier_diag_mark(uint32_t, uint32_t, int32_t) {}
extern "C" void botifier_diag_sample_memory(uint32_t) {}
