[In Japanese](README.md)

# M5BLEKeyCast

`M5BLEKeyCast` turns multiple M5Stack devices (ATOM Matrix/Lite) into BLE keyboards, allowing all M5Stack devices running `M5BLEKeyCast` to send the same key input simultaneously.

Originally, `M5BLEKeyCast` was created to synchronize and operate RoBoHoN. For example, you can set up each RoBoHoN to wait for key input via Roblick and run a dance script. By connecting an M5Stack to each RoBoHoN as a BLE keyboard, pressing a button can make all RoBoHoNs dance in sync.

## How to Use

Pair the M5Stack device running `M5BLEKeyCast` with the target device as a BLE keyboard.

Once paired, the LED on the M5Stack device lights up blue. When you press the button on the M5Stack device, it broadcasts the stored key via ESP-NOW, causing all M5Stack devices to send the same key simultaneously.

The LED colors indicate the following states:

| Color          | State                            |
|----------------|----------------------------------|
| Off            | BLE not connected                |
| Blue (solid)   | BLE connected                    |
| Green (blink)  | ESP-NOW received -> key sent     |
| Yellow (solid) | Configuration mode               |

### Configuration Mode

Boot the M5Stack device while holding the button to enter configuration mode, indicated by the LED lighting up yellow.

The serial console will display the following menu:

```
=== Config Mode ===
[1] Set key  (current: '1')
[2] Set name (current: 'KC_Lite')
[0] Exit
Enter choice:
```

| Choice | Description |
|--------|-------------|
| `1`    | Change the key to send. Enter a single printable ASCII character |
| `2`    | Change the BLE device name. Enter a name up to 20 characters and press Enter |
| `0`    | Exit configuration mode and restart |

Settings are saved to NVS immediately. You can make multiple changes before exiting — press `0` to restart with the new settings. If you change the device name, you may need to re-pair with previously connected devices.
