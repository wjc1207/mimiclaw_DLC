#include "lua_modulo_ble.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lauxlib.h"
#include "mimi_config.h"

#if CONFIG_MIMI_TOOL_BLE_ENABLED && defined(CONFIG_BT_NIMBLE_ENABLED)
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

typedef struct {
    float temperature_c;
    float humidity_percent;
    int battery_percent;
    bool temperature_valid;
    bool humidity_valid;
    bool battery_valid;
    bool encrypted;
    char source_addr[18];
    uint32_t last_seen_ms;
} thome_reading_t;

static const char *TAG = "lua_ble";
static const uint16_t BTHOME_UUID_16 = 0xFCD2;
static const uint8_t AD_TYPE_SERVICE_DATA_16 = 0x16;

static char s_target_addr[18] = "a4:c1:38:a0:0d:98";
static bool s_filter_target_addr = true;
static bool s_started = false;
static uint8_t s_own_addr_type;

static thome_reading_t s_latest = {0};
static bool s_has_data = false;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static int bthome_gap_event(struct ble_gap_event *event, void *arg);
static void bthome_start_scan(void);

static int bthome_obj_len(uint8_t object_id)
{
    switch (object_id) {
    case 0x00:
        return 1;
    case 0x01:
        return 1;
    case 0x02:
        return 2;
    case 0x03:
        return 2;
    default:
        return -1;
    }
}

static void ble_addr_to_str(const ble_addr_t *addr, char *out, size_t out_len)
{
    snprintf(out, out_len, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

static bool bthome_parse_service_data(const uint8_t *svc, uint8_t svc_len,
                                      thome_reading_t *out)
{
    if (svc == NULL || out == NULL || svc_len < 3) {
        return false;
    }

    uint16_t uuid16 = (uint16_t)svc[0] | ((uint16_t)svc[1] << 8);
    if (uuid16 != BTHOME_UUID_16) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    uint8_t dev_info = svc[2];
    out->encrypted = (dev_info & 0x01U) != 0;

    uint8_t i = 3;
    bool any = false;

    while (i < svc_len) {
        uint8_t object_id = svc[i++];
        int len = bthome_obj_len(object_id);
        if (len < 0 || (uint8_t)(i + len) > svc_len) {
            break;
        }

        if (object_id == 0x02 && len == 2) {
            int16_t raw_temp = (int16_t)(((uint16_t)svc[i + 1] << 8) | svc[i]);
            out->temperature_c = (float)raw_temp / 100.0f;
            out->temperature_valid = true;
            any = true;
        } else if (object_id == 0x03 && len == 2) {
            uint16_t raw_hum = (uint16_t)(((uint16_t)svc[i + 1] << 8) | svc[i]);
            out->humidity_percent = (float)raw_hum / 100.0f;
            out->humidity_valid = true;
            any = true;
        } else if (object_id == 0x01 && len == 1) {
            out->battery_percent = svc[i];
            out->battery_valid = true;
            any = true;
        }

        i = (uint8_t)(i + len);
    }

    return any;
}

static void bthome_start_scan(void)
{
    const struct ble_gap_disc_params scan_params = {
        .itvl = 0x50,
        .window = 0x30,
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 0,
    };

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &scan_params, bthome_gap_event, NULL);
    if (rc == BLE_HS_EALREADY) {
        return;
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "Start BLE scan failed rc=%d", rc);
    }
}

static int bthome_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        char addr_str[18] = {0};
        ble_addr_to_str(&event->disc.addr, addr_str, sizeof(addr_str));

        if (s_filter_target_addr && strcasecmp(addr_str, s_target_addr) != 0) {
            return 0;
        }

        const uint8_t *data = event->disc.data;
        uint8_t data_len = event->disc.length_data;
        uint8_t idx = 0;

        while (idx < data_len) {
            uint8_t field_len = data[idx];
            if (field_len == 0) {
                break;
            }
            if ((uint8_t)(idx + 1 + field_len) > data_len) {
                break;
            }

            uint8_t ad_type = data[idx + 1];
            const uint8_t *payload = &data[idx + 2];
            uint8_t payload_len = (uint8_t)(field_len - 1);

            if (ad_type == AD_TYPE_SERVICE_DATA_16 && payload_len >= 3) {
                thome_reading_t parsed = {0};
                if (bthome_parse_service_data(payload, payload_len, &parsed)) {
                    parsed.last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
                    snprintf(parsed.source_addr, sizeof(parsed.source_addr), "%s", addr_str);

                    portENTER_CRITICAL(&s_lock);
                    s_latest = parsed;
                    s_has_data = true;
                    portEXIT_CRITICAL(&s_lock);
                }
            }

            idx = (uint8_t)(idx + 1 + field_len);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        bthome_start_scan();
        return 0;
    default:
        return 0;
    }
}

static void bthome_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE reset reason=%d", reason);
}

static void bthome_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        return;
    }

    bthome_start_scan();
}

static void bthome_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_listener_set_target_addr(const char *target_addr)
{
    if (target_addr != NULL && target_addr[0] != '\0') {
        snprintf(s_target_addr, sizeof(s_target_addr), "%s", target_addr);
        s_filter_target_addr = true;
    } else {
        s_filter_target_addr = false;
    }
}

static esp_err_t ble_listener_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ble_hs_cfg.reset_cb = bthome_on_reset;
    ble_hs_cfg.sync_cb = bthome_on_sync;

    nimble_port_freertos_init(bthome_host_task);
    s_started = true;

    return ESP_OK;
}

static bool ble_listener_get_latest(thome_reading_t *out)
{
    if (out == NULL) {
        return false;
    }

    bool has;
    portENTER_CRITICAL(&s_lock);
    has = s_has_data;
    if (has) {
        *out = s_latest;
    }
    portEXIT_CRITICAL(&s_lock);

    return has;
}

static esp_err_t ble_listener_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    nimble_port_stop();
    nimble_port_deinit();
    s_started = false;

    portENTER_CRITICAL(&s_lock);
    s_has_data = false;
    memset(&s_latest, 0, sizeof(s_latest));
    portEXIT_CRITICAL(&s_lock);

    return ESP_OK;
}
#else
typedef struct {
    float temperature_c;
    float humidity_percent;
    int battery_percent;
    bool temperature_valid;
    bool humidity_valid;
    bool battery_valid;
    bool encrypted;
    char source_addr[18];
    uint32_t last_seen_ms;
} thome_reading_t;

static void ble_listener_set_target_addr(const char *target_addr)
{
    (void)target_addr;
}

static esp_err_t ble_listener_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static bool ble_listener_get_latest(thome_reading_t *out)
{
    (void)out;
    return false;
}

static esp_err_t ble_listener_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

static int lua_ble_config(lua_State *L)
{
    const char *addr = NULL;

    if (!lua_isnoneornil(L, 1)) {
        if (lua_type(L, 1) == LUA_TTABLE) {
            lua_getfield(L, 1, "target_addr");
            if (!lua_isnil(L, -1)) {
                addr = luaL_checkstring(L, -1);
            }
            lua_pop(L, 1);
        } else {
            addr = luaL_checkstring(L, 1);
        }
    }

    ble_listener_set_target_addr(addr);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_ble_start(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        lua_settop(L, 1);
        lua_ble_config(L);
        lua_pop(L, 1);
    }

    esp_err_t err = ble_listener_start();
    if (err != ESP_OK) {
        return luaL_error(L, "ble_listener.start failed: %s", esp_err_to_name(err));
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_ble_latest(lua_State *L)
{
    thome_reading_t reading = {0};
    bool has = ble_listener_get_latest(&reading);
    if (!has) {
        lua_pushnil(L);
        return 1;
    }

    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    uint64_t age_ms = now_ms >= reading.last_seen_ms ? (now_ms - reading.last_seen_ms) : 0;

    lua_newtable(L);
    lua_pushstring(L, reading.source_addr);
    lua_setfield(L, -2, "source_addr");

    lua_pushnumber(L, (lua_Number)age_ms);
    lua_setfield(L, -2, "age_ms");

    lua_pushnumber(L, reading.temperature_c);
    lua_setfield(L, -2, "temperature");
    lua_pushboolean(L, reading.temperature_valid);
    lua_setfield(L, -2, "temperature_valid");

    lua_pushnumber(L, reading.humidity_percent);
    lua_setfield(L, -2, "humidity");
    lua_pushboolean(L, reading.humidity_valid);
    lua_setfield(L, -2, "humidity_valid");

    lua_pushinteger(L, reading.battery_percent);
    lua_setfield(L, -2, "battery");
    lua_pushboolean(L, reading.battery_valid);
    lua_setfield(L, -2, "battery_valid");

    lua_pushboolean(L, reading.encrypted);
    lua_setfield(L, -2, "encrypted");

    return 1;
}

static int lua_ble_stop(lua_State *L)
{
    esp_err_t err = ble_listener_stop();
    if (err != ESP_OK) {
        return luaL_error(L, "ble_listener.stop failed: %s", esp_err_to_name(err));
    }

    lua_pushboolean(L, 1);
    return 1;
}

int luaopen_ble(lua_State *L)
{
    static const luaL_Reg funcs[] = {
        {"config", lua_ble_config},
        {"start", lua_ble_start},
        {"stop", lua_ble_stop},
        {"latest", lua_ble_latest},
        {NULL, NULL},
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}

int luaopen_modulo_ble(lua_State *L)
{
    return luaopen_ble(L);
}

void lua_register_modulo_ble_lib(lua_State *L)
{
    luaL_requiref(L, "modulo_ble", luaopen_modulo_ble, 1);
    lua_setglobal(L, "modulo_ble");
    luaL_requiref(L, "ble", luaopen_ble, 1);
    lua_setglobal(L, "ble");
}
