#include "buddy.h"
#include "buddy_profile.h"
#include "buddy_contacts.h"
#include "buddy_espnow.h"
#include "buddy_agent.h"

#include "esp_log.h"

static const char *TAG = "buddy";

esp_err_t buddy_init(void)
{
    esp_err_t err;

    /* 1. Load/generate device identity + user profile */
    err = buddy_profile_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Profile init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 2. Mount contact store */
    err = buddy_contacts_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Contacts init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 3. Initialize ESP-NOW */
    err = buddy_espnow_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 4. Initialize agent (LED, etc.) */
    err = buddy_agent_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Agent init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Buddy subsystem initialized");
    return ESP_OK;
}

esp_err_t buddy_start(void)
{
    esp_err_t err;

    /* Start ESP-NOW beacon + receive */
    err = buddy_espnow_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW start failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Start contact processing task */
    err = buddy_agent_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Agent start failed: %s", esp_err_to_name(err));
        buddy_espnow_stop();
        return err;
    }

    ESP_LOGI(TAG, "Buddy subsystem started — discovering peers");
    return ESP_OK;
}

esp_err_t buddy_stop(void)
{
    buddy_espnow_stop();
    buddy_led_set(BUDDY_LED_PATTERN_OFF);
    ESP_LOGI(TAG, "Buddy subsystem stopped");
    return ESP_OK;
}
