#pragma once

#include "buddy.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ── BLE timing ────────────────────────────────────────────────── */
#define BUDDY_BLE_ADV_PERIOD_MS      250
#define BUDDY_BLE_ADV_JITTER_MS      50
#define BUDDY_BLE_SCAN_INTERVAL_MS   1000
#define BUDDY_BLE_SCAN_WINDOW_MS     800

/* ── BLE GATT service / characteristic UUIDs (128-bit) ─────────── */
#define BUDDY_SVC_UUID \
    0x4A,0x7B,0x80,0x01,0x9C,0x3D,0x4E,0x5F, \
    0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0xA7,0xB8

#define BUDDY_CHR_PROFILE_UUID \
    0x4A,0x7B,0x80,0x02,0x9C,0x3D,0x4E,0x5F, \
    0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0xA7,0xB8

#define BUDDY_CHR_PROFILE_WRITE_UUID \
    0x4A,0x7B,0x80,0x03,0x9C,0x3D,0x4E,0x5F, \
    0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0xA7,0xB8

/* ── Manufacturer data identifiers ─────────────────────────────── */
#define BUDDY_MFG_COMPANY_ID  0x02E5   /* Espressif */
#define BUDDY_PROTO_VERSION   1

/**
 * Initialize BLE transport: NimBLE stack, GATT services, event queue.
 */
esp_err_t buddy_ble_init(void);

/**
 * Start advertising + scanning.
 */
esp_err_t buddy_ble_start(void);

/**
 * Stop advertising + scanning, disconnect any active connection.
 */
esp_err_t buddy_ble_stop(void);

/**
 * Get the event queue for contact processing.
 */
QueueHandle_t buddy_ble_get_event_queue(void);
