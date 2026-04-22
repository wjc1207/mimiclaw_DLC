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

### `modulo_ble` — BLE Home Sensor Listener

> This module is only available when `CONFIG_MIMI_TOOL_BLE_ENABLED` and `CONFIG_BT_NIMBLE_ENABLED` are enabled.

| Function | Signature | Returns | Description |
|---|---|---|---|
| `modulo_ble.config` | `modulo_ble.config(target_addr)` or `modulo_ble.config({target_addr = "addr"})` | `boolean` | Configure BLE listener to filter by target MAC address (or accept all devices if address is empty) |
| `modulo_ble.start` | `modulo_ble.start()` or `modulo_ble.start(target_addr)` | `boolean` | Start BLE scanning for BTHome sensors |
| `modulo_ble.stop` | `modulo_ble.stop()` | `boolean` | Stop BLE scanning |
| `modulo_ble.latest` | `modulo_ble.latest()` | `table` or `nil` | Get latest sensor reading from BTHome sensor |

The `latest()` method returns a table with these fields:
- `temperature`: Temperature in Celsius (number or `nil` if invalid)
- `humidity`: Humidity in percent (number or `nil` if invalid)
- `battery`: Battery percentage (0-100 or `nil` if invalid)
- `source_addr`: MAC address of the sensor
- `age_ms`: Time since last reading (in milliseconds)
- `encrypted`: Whether the reading was encrypted (boolean)

```lua
-- Initialize and start BLE listener
modulo_ble.start("a4:c1:38:a0:0d:98") -- Target specific sensor

-- Get and print latest reading
local data = modulo_ble.latest()
if data then
    print("Sensor data from", data.source_addr)
    if data.temperature_valid then
        print("Temperature:", data.temperature, "°C")
    end
    if data.humidity_valid then
        print("Humidity:", data.humidity, "%")
    end
    if data.battery_valid then
        print("Battery:", data.battery, "%")
    end
end

-- Stop listener after 30 seconds
sleep.ms(30000)
modulo_ble.stop()
```

### `modulo_camera` — Camera Capture and Server

> This module is only available when `CONFIG_MIMI_TOOL_CAMERA_ENABLED` is enabled.

| Function | Signature | Returns | Description |
|---|---|---|---|
| `modulo_camera.init` | `modulo_camera.init()` | `boolean` | Initialize the camera with default configuration |
| `modulo_camera.configure` | `modulo_camera.configure(config)` | `boolean` | Configure camera parameters (frame size, quality, pins) |
| `modulo_camera.capture` | `modulo_camera.capture(encode_base64, save_path)` | `table` or `string` | Capture a photo and optionally encode to base64 or save to file |
| `modulo_camera.server.start` | `modulo_camera.server.start()` | `boolean` | Start HTTP server (port 18787) for camera capture endpoint |
| `modulo_camera.server.stop` | `modulo_camera.server.stop()` | `boolean` | Stop the HTTP server |

Camera configuration options:
- `frame_size`: Index of pre-defined frame size (0-10)
- `jpeg_quality`: JPEG compression quality (1-63, 1 = best quality)
- `xclk_freq_hz`: XCLK frequency in Hz (for external oscillator configuration)
- `pin_pwdn`, `pin_reset`, `pin_xclk`, etc.: Camera GPIO pin assignments

```lua
-- Configure camera
modulo_camera.configure({
    frame_size = 6, -- 640x480 (VGA)
    jpeg_quality = 10, -- High quality
    xclk_freq_hz = 12000000 -- External oscillator
})

-- Initialize and capture a photo (save to /spiffs/photo.jpg)
local capture = modulo_camera.capture(true, "/spiffs/photo.jpg")
if capture then
    print("Captured photo:", capture.file_path)
    print("Resolution:", capture.width, "x", capture.height)
    print("JPEG size:", capture.jpeg_bytes, "bytes")
end

-- Start HTTP server for remote capture (/capture endpoint)
modulo_camera.server.start()
sleep.ms(60000) -- Run for 60 seconds
modulo_camera.server.stop()
```

### `modulo_rgb` — RGB LED Control

> This module is only available when `CONFIG_MIMI_TOOL_RGB_ENABLED` is enabled.

| Function | Signature | Returns | Description |
|---|---|---|---|
| `modulo_rgb.set` | `modulo_rgb.set(r, g, b, brightness)` | nothing | Set RGB LED color (0-255 each) with optional brightness (0-255) |
| `modulo_rgb.set_hex` | `modulo_rgb.set_hex(hex, brightness)` | nothing | Set RGB LED color using hex string (#RRGGBB or RRGGBB) |
| `modulo_rgb.off` | `modulo_rgb.off()` | nothing | Turn off RGB LED |

```lua
-- Set red LED at full brightness
modulo_rgb.set(255, 0, 0)

-- Set blue LED at 50% brightness
modulo_rgb.set(0, 0, 255, 128)

-- Set green LED using hex color
modulo_rgb.set_hex("#00FF00")

-- Blink purple LED
while true do
    modulo_rgb.set(128, 0, 128)
    sleep.ms(500)
    modulo_rgb.off()
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