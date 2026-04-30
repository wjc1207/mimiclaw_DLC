#pragma once

#include "buddy.h"

/**
 * Initialize the contact store (SPIFFS).
 */
esp_err_t buddy_contacts_init(void);

/**
 * Upsert a contact record. Returns ESP_OK.
 */
esp_err_t buddy_contacts_upsert(const buddy_contact_record_t *rec);

/**
 * Get a contact by peer device_id. Returns ESP_ERR_NOT_FOUND if missing.
 */
esp_err_t buddy_contacts_get(const char *peer_id, buddy_contact_record_t *out);

/**
 * List all contacts. Fills buf with up to max records, updates *count.
 */
esp_err_t buddy_contacts_list(buddy_contact_record_t *buf, size_t max, size_t *count);

/**
 * Check contact status: NEW, KNOWN, or RECENT (met within 24h).
 */
buddy_contact_status_t buddy_contacts_check(const char *peer_id);

/**
 * Update the cloud_synced fields of an existing contact record.
 */
esp_err_t buddy_contacts_update_match(const char *peer_id, float score,
                                      const char *icebreaker,
                                      const char *shared_interests);
