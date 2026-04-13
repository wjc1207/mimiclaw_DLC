## Examples

### Example 1: sense node
- Hardware: ESP32-S3 + camera + BLE thermometer
- Software: camera capture tool + BLE listener tool
- Use case: monitor the environment and report to LLMs

### Example 2: smart car
- Hardware: ESP32-S3 + L289N motor driver + 4 DC motors
- Software: lua scripting
- Use case: control the car via LLM commands, e.g. "move forward", "turn left", etc.

### Example 3: team leader
- Hardware: ESP32-S3 * 3
- Software: A2A server + A2A client
- Use case: one ESP32-S3 acts as the team leader, receiving commands from user and distributing to other two ESP32-S3 (team members) via A2A communication. The team members can execute tasks and report back to the leader, which then reports to the user.