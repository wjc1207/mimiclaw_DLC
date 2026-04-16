set(srcs "dummy.c")

if(CONFIG_MIMI_TOOL_CAMERA_ENABLED OR CONFIG_MIMI_CAMERA_SERVER_ENABLED)
    list(APPEND srcs
        "camera_core.c"
        "camera_server.c"
    )
endif()

if(CONFIG_MIMI_TOOL_CAMERA_ENABLED)
    list(APPEND srcs "tool_camera_capture.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "."
    PRIV_INCLUDE_DIRS
        "../../main"
    REQUIRES
        esp_http_server
        esp_timer
        json
        mbedtls
        esp32-camera
)
