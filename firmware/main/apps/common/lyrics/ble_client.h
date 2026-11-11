#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ble_gap_event;

// Runs from the media worker, outside the display lock. Reuses the sole NimBLE
// host and posts client work onto its event queue rather than reinitializing it.
int botifier_ams_set_enabled(bool enabled);
void botifier_ams_forget_phone(void);

// Narrow hooks into the existing peripheral host; implementations own no UI.
void botifier_ams_gap_event(struct ble_gap_event* event);
void botifier_ams_host_reset(int reason);
void botifier_ams_host_synced(void);
bool botifier_ams_advertising_enabled(void);
bool botifier_ams_owns_connection(unsigned short handle);
void botifier_ams_legacy_requested(void);

#ifdef __cplusplus
}
#endif
