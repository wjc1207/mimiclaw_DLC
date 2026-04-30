#pragma once

#include "buddy.h"

/**
 * Initialize profile subsystem: load or generate device identity,
 * load user profile from NVS, compute beacon hash.
 */
esp_err_t buddy_profile_init(void);

/**
 * Compute SHA-256 hash of profile JSON, store first 8 bytes in hash_out.
 */
void buddy_profile_compute_hash(const buddy_profile_t *profile, uint8_t hash_out[8]);
