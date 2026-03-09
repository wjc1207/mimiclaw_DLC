# SKILL: Lua Hardware Scripting via lua_gpio_lib

## Purpose
Use this skill whenever you need to write Lua scripts that control hardware
peripherals on an ESP32 device using the built-in `lua_gpio_lib` bindings.
This skill covers every available module: `gpio`, `i2c`, `spi`, `rgb`, `pwm`,
and `sleep`.

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

---

### `i2c` — I²C Bus

| Function | Signature | Returns | Description |
|---|---|---|---|
| `i2c.write` | `i2c.write(sda, scl, addr, data_table, [freq])` | nothing | Write bytes to an I²C device |
| `i2c.read` | `i2c.read(sda, scl, addr, len, [freq])` | `string` (JSON array) | Read `len` bytes from an I²C device |

**Defaults:** `freq` defaults to `100000` (100 kHz) when omitted.

`data_table` must be a Lua table of integers (byte values 0–255).

```lua
-- Write register 0x00 with value 0xFF to device at address 0x3C
-- SDA=21, SCL=22, 400 kHz
i2c.write(21, 22, 0x3C, {0x00, 0xFF}, 400000)

-- Read 6 bytes from device 0x68 (e.g. MPU-6050)
local raw = i2c.read(21, 22, 0x68, 6)
print("Raw bytes:", raw)
```

---

### `spi` — SPI Bus

| Function | Signature | Returns | Description |
|---|---|---|---|
| `spi.transfer` | `spi.transfer(mosi, miso, sclk, cs, tx_table)` | `string` (JSON result) | Full-duplex SPI transfer |

`tx_table` is a Lua table of bytes to send. The return value contains the
received bytes from the bus.

```lua
-- Send 0x9F (JEDEC ID command) and read 3 response bytes
-- MOSI=23, MISO=19, SCLK=18, CS=5
local rx = spi.transfer(23, 19, 18, 5, {0x9F, 0x00, 0x00, 0x00})
print("SPI RX:", rx)
```

---

### `rgb` — Addressable RGB LEDs (NeoPixel / WS2812)

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
`tool_gpio_execute` call fails. Wrap calls in `pcall` for recoverable error
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

### I²C sensor read-register helper
```lua
-- Write a register address, then read N bytes back
local function i2c_read_reg(sda, scl, addr, reg, len)
    i2c.write(sda, scl, addr, {reg})
    return i2c.read(sda, scl, addr, len)
end

-- Example: read WHO_AM_I register (0x75) of MPU-6050 at 0x68
local id = i2c_read_reg(21, 22, 0x68, 0x75, 1)
print("WHO_AM_I:", id)
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
   hardware.
3. **I²C / SPI `data` arguments must be Lua tables** (not strings). Convert
   strings with `{string.byte(s, 1, -1)}` if needed.
4. **`i2c.read` and `spi.transfer` return a JSON-encoded string**, not a plain
   number. Parse with `require("cjson").decode(result)` if your runtime has
   cjson, or handle as a raw string.
5. **Duty cycle for PWM is 0–1023** (10-bit). Values outside this range may
   produce unexpected behaviour.
6. **`sleep.ms` blocks the FreeRTOS task.** Keep delay values reasonable to
   avoid watchdog timeouts (stay well under the configured WDT period).
7. **No persistent state between calls.** Each `gpio`, `i2c`, `spi`, etc. call
   is stateless; bus/pin configuration is handled inside the tool layer.

---

## Quick-Reference Card

```
gpio.write(pin, 0|1)
gpio.read(pin)                         → 0|1

i2c.write(sda, scl, addr, {bytes}, [freq])
i2c.read(sda, scl, addr, len, [freq]) → "[b0,b1,…]"

spi.transfer(mosi, miso, sclk, cs, {tx_bytes}) → "[rx_bytes]"

rgb.fill(pin, n, r, g, b)
rgb.show(pin, n)

pwm.start(pin, freq_hz, duty_0_1023)

sleep.ms(milliseconds)
```