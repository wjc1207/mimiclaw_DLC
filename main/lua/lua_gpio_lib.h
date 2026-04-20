#pragma once

#include "lua.h"

/**
 * Open hardware-binding libraries into a Lua state.
 *
 * Registers the following globals:
 *   gpio.write(pin, val)       gpio.read(pin)
 *   rgb.fill(pin,n,r,g,b)     rgb.show(pin,n)  (when CONFIG_MIMI_TOOL_RGB_ENABLED)
 *   pwm.start(pin,freq,duty)
 *   sleep.ms(ms)
 */
void lua_open_gpio_libs(lua_State *L);
