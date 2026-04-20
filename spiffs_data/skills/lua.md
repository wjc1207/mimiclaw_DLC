# SKILL: Lua Hardware Scripting via lua_gpio_lib

## Purpose
Use this skill whenever you need to write Lua scripts that control hardware
peripherals on an ESP32 device using the built-in `lua_gpio_lib` bindings.
This skill covers the available modules: `gpio`, `rgb` (when enabled in
Kconfig), `pwm`, and `sleep`.

---

## Module Reference

### `gpio` — Digital I/O

| Function | Signature | Returns | Description |
|---|---|---|---|
| `gpio.write` | `gpio.write(pin, value)` | nothing | Set a digital output pin HIGH (1) or LOW (0) |
| `gpio.read` | `gpio.read(pin)` | `integer` | Read the digital level of a pin (0 or 1) |

```lua
-- Turn on an LED on pin 2, then read pin 4
gpio.write(2, 1)
local level = gpio.read(4)
print("Pin 4 level:", level)
```

### `rgb` — Addressable RGB LEDs (NeoPixel / WS2812)

> This module is only available when `CONFIG_MIMI_TOOL_RGB_ENABLED` is enabled.

| Function | Signature | Returns | Description |
|---|---|---|---|
| `rgb.fill` | `rgb.fill(pin, num_pixels, r, g, b)` | nothing | Set all pixels to a single RGB colour (0–255 each) |
| `rgb.show` | `rgb.show(pin, num_pixels)` | nothing | Latch/push the pixel data to the strip |

`rgb.fill` only buffers the colour. You **must** call `rgb.show` to update the
physical LEDs.

```lua
-- Set 8 NeoPixels on pin 16 to solid red, then display
rgb.fill(16, 8, 255, 0, 0)
rgb.show(16, 8)

-- Simple colour sweep
local colours = {
    {255, 0,   0},   -- red
    {0,   255, 0},   -- green
    {0,   0,   255}, -- blue
}
for _, c in ipairs(colours) do
    rgb.fill(16, 8, c[1], c[2], c[3])
    rgb.show(16, 8)
    sleep.ms(500)
end
```

---

### `pwm` — PWM Output

| Function | Signature | Returns | Description |
|---|---|---|---|
| `pwm.start` | `pwm.start(pin, freq, duty)` | nothing | Start PWM on a pin. `duty` is 0–1023 (10-bit) |

```lua
-- 50% duty cycle at 1 kHz on pin 13
pwm.start(13, 1000, 512)

-- Servo pulse: 50 Hz, duty ~77 ≈ 1.5 ms centre position (10-bit scale)
pwm.start(15, 50, 77)
```

> **Duty scale:** 0 = 0%, 1023 = 100%. Calculate with:
> `duty = math.floor(percentage / 100 * 1023)`

---

### `sleep` — Delays

| Function | Signature | Returns | Description |
|---|---|---|---|
| `sleep.ms` | `sleep.ms(ms)` | nothing | Block for `ms` milliseconds (uses FreeRTOS `vTaskDelay`) |

```lua
sleep.ms(1000)   -- wait 1 second
sleep.ms(50)     -- wait 50 ms
```

---

## Error Handling

All library functions raise a Lua error (via `luaL_error`) if the underlying
hardware operation fails. Wrap calls in `pcall` for recoverable error
handling:

```lua
local ok, err = pcall(function()
    gpio.write(99, 1)   -- invalid pin
end)
if not ok then
    print("GPIO error:", err)
end
```

---

## Common Patterns

### Blink an LED
```lua
local LED_PIN = 2
while true do
    gpio.write(LED_PIN, 1)
    sleep.ms(500)
    gpio.write(LED_PIN, 0)
    sleep.ms(500)
end
```

### Read a button with debounce
```lua
local BTN_PIN = 0
local last = gpio.read(BTN_PIN)
while true do
    sleep.ms(20)
    local cur = gpio.read(BTN_PIN)
    if cur ~= last then
        print("Button changed to:", cur)
        last = cur
    end
end
```

### PWM LED fade
```lua
local PIN = 13
-- Fade in
for duty = 0, 1023, 8 do
    pwm.start(PIN, 1000, duty)
    sleep.ms(10)
end
-- Fade out
for duty = 1023, 0, -8 do
    pwm.start(PIN, 1000, duty)
    sleep.ms(10)
end
```

### NeoPixel rainbow cycle
```lua
local PIN, N = 16, 12

local function hsv_to_rgb(h)   -- h: 0-255
    local s, v = 255, 255
    local i = math.floor(h / 43)
    local f = (h - i * 43) * 6
    local q = math.floor((255 - f) * v / 255)
    local t = math.floor(f * v / 255)
    local r, g, b
    if     i == 0 then r,g,b = v, t, 0
    elseif i == 1 then r,g,b = q, v, 0
    elseif i == 2 then r,g,b = 0, v, t
    elseif i == 3 then r,g,b = 0, q, v
    elseif i == 4 then r,g,b = t, 0, v
    else               r,g,b = v, 0, q
    end
    return r, g, b
end

for hue = 0, 254 do
    local r, g, b = hsv_to_rgb(hue)
    rgb.fill(PIN, N, r, g, b)
    rgb.show(PIN, N)
    sleep.ms(20)
end
```

---

## Rules & Constraints

1. **Always use integer pin numbers.** Passing a float or nil will raise a Lua
   error.
2. **`rgb.fill` must be followed by `rgb.show`** before colours appear on the
    hardware, and only if the RGB module is enabled in Kconfig.
3. **Duty cycle for PWM is 0–1023** (10-bit). Values outside this range may
   produce unexpected behaviour.
4. **`sleep.ms` blocks the FreeRTOS task.** Keep delay values reasonable to
   avoid watchdog timeouts (stay well under the configured WDT period).
5. **No persistent state between calls.** Each `gpio`, `rgb`, `pwm`, or `sleep`
   is stateless; bus/pin configuration is handled inside the tool layer.

---

## Quick-Reference Card

```
gpio.write(pin, 0|1)
gpio.read(pin)                         → 0|1
rgb.fill(pin, n, r, g, b)              (when enabled)
rgb.show(pin, n)                       (when enabled)
pwm.start(pin, freq_hz, duty_0_1023)
sleep.ms(milliseconds)
```