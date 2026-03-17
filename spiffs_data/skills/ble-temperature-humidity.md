# BLE Temperature Humidity

Read temperature and humidity from BTHome v2 BLE broadcast packets. Use this when the user wants nearby sensor values without establishing a GATT connection.

## Requirements

- You need the sensor BLE MAC address.
- The sensor must broadcast BTHome v2 service data UUID `0xFCD2`.
- Current parser supports BTHome object IDs: temperature `0x02` and humidity `0x03`.

## How to use

1. Ask for the BLE MAC address if the user did not provide it.
2. Call `ble` with `{"action":"connect","addr":"aa:bb:cc:dd:ee:ff"}` to start listening.
3. Call `ble` with `{"action":"read"}` to get the latest parsed broadcast value.
4. Report temperature in C and humidity in %RH.
5. Call `ble` with `{"action":"disconnect"}` when finished.

## Notes

- If connect fails, tell the user to move the sensor closer, check the MAC address, and confirm the sensor is broadcasting BTHome v2.
- Encrypted BTHome payloads are not decoded by this implementation.