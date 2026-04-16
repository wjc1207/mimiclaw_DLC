set(srcs "dummy.c")

if(CONFIG_MIMI_TOOL_RGB_ENABLED)
    list(APPEND srcs "tool_rgb_control.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "."
    REQUIRES
        led_strip
        json
)
