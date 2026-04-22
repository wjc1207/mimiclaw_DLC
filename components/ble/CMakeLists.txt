set(srcs "dummy.c")

if(CONFIG_MIMI_TOOL_BLE_ENABLED)
    list(APPEND srcs
        "lua_modulo_ble.c"
    )
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "."
    PRIV_INCLUDE_DIRS
        "../../main"
    REQUIRES
        bt
        esp_timer
        lua
)
