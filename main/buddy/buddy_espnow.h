#pragma once

#include "buddy.h"

/**
 * Initialize ESP-NOW: register callback, set PMK, set channel.
 * Does NOT start broadcasting — call buddy_espnow_start().
 */
esp_err_t buddy_espnow_init(void);

/**
 * Start beacon broadcast and enable receive.
 */
esp_err_t buddy_espnow_start(void);

/**
 * Stop beacon broadcast and ESP-NOW.
 */
esp_err_t buddy_espnow_stop(void);

/**
 * Send a beacon frame immediately (for manual trigger / testing).
 */
esp_err_t buddy_espnow_send_beacon(void);

/**
 * Get the event queue for contact processing.
 */
QueueHandle_t buddy_espnow_get_event_queue(void);
