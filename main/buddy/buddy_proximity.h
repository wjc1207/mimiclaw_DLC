#pragma once

#include "buddy.h"
#include <stdint.h>

/**
 * Feed a new RSSI sample. Updates rolling median.
 */
void buddy_proximity_feed(int8_t rssi);

/**
 * Get current smoothed RSSI.
 */
int8_t buddy_proximity_rssi(void);

/**
 * Classify current proximity based on rolling median.
 */
buddy_proximity_t buddy_proximity_classify(void);

/**
 * Convert proximity enum to human-readable string.
 */
const char *buddy_proximity_str(buddy_proximity_t p);

/**
 * Reset the rolling window (e.g., when switching peers).
 */
void buddy_proximity_reset(void);
