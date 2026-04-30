## Examples

### Example 1: sense node
- Hardware: ESP32-S3 + camera + BLE thermometer
- Software: camera capture tool + BLE listener tool
- Use case: monitor the environment and report to LLMs

### Example 2: smart car
- Hardware: ESP32-S3 + L289N motor driver + 4 DC motors
- Software: lua scripting
- Use case: control the car via LLM commands, e.g. "move forward", "turn left", etc.

### Example 3: social buddy
- Hardware: ESP32-S3 * 2
- Software: social buddy (ESP-NOW + PSK)
- Use case: two wearable devices discover each other via ESP-NOW beacons, exchange encrypted profiles, and get an LLM-powered match score with icebreaker.