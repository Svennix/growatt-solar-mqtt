# Growatt Solar Inverter MQTT Gateway

ESP32-C6 based gateway that reads data from a Growatt **MID 15/20KTL3-XL** solar inverter via Modbus RS485 and publishes it to MQTT.

## Hardware

### Required Components
- **ESP32-C6** dev board (e.g. ESP32-C6-DevKitC-1 / ESP32-C6-Mini)
- **TTL485-V2.0 RS485 module** (auto-direction — no DE/RE control pins needed)
- **Growatt MID 15/20KTL3-XL** solar inverter (or compatible Growatt models)
- Jumper wires
- USB-C cable for the first flash

### Wiring Diagram

```
ESP32-C6              →    TTL485-V2.0 (RS485, auto-direction)
==================================================================
GPIO4  (UART1 RX)    →    TTL485 RX
GPIO5  (UART1 TX)    →    TTL485 TX
3.3V                 →    VCC
GND                  →    GND

TTL485-V2.0          →    Growatt Inverter (RS485)
==================================================================
A                    →    RS485 A+
B                    →    RS485 B-
```

Pin assignments are defined in `include/config.h` (`RS485_RX`, `RS485_TX`, `STATUS_LED`).

**Important Notes:**
- The TTL485-V2.0 handles transmit/receive switching automatically — there are **no DE/RE pins** to wire.
- `GPIO8` drives the onboard status LED (blinks LOW during each inverter read) — no external wiring required.
- Use shielded twisted pair cable for the RS485 connection.
- Maximum cable length: 1200 m (though typically much shorter).
- Termination resistors may be required for long cable runs.

## Features

- ✅ **Complete inverter monitoring**: Full MID 15/20KTL3-XL register map — PV input, AC output, energy, temperatures, safety and PID diagnostics
- ✅ **MQTT publishing**: Inverter data + diagnostics every `UPDATE_INTERVAL` seconds (default 2 s)
- ✅ **System status**: WiFi RSSI, uptime, free heap, Modbus error rate and cycle timing every `STATUS_INTERVAL` seconds (default 30 s)
- ✅ **OTA updates**: Update firmware wirelessly over WiFi (ArduinoOTA)
- ✅ **mDNS**: Device is reachable as `<hostname>.local`
- ✅ **Error handling**: Tracks and reports Modbus communication errors
- ✅ **WiFi reconnection**: Automatic WiFi recovery via event handler
- ✅ **LED status**: Onboard LED blinks during data reads

## Data Published

The topic root defaults to `growatt` (set via `MQTT_TOPIC_ROOT` in `config.h`), giving four topics:

| Topic | Contents | Rate |
|-------|----------|------|
| `growatt/data` | Live PV / grid / energy readings | every `UPDATE_INTERVAL` s |
| `growatt/diagnostics` | Temperatures, faults, safety, strings, PID | every `UPDATE_INTERVAL` s |
| `growatt/status` | Gateway health & performance stats | every `STATUS_INTERVAL` s |
| `growatt/connection` | `online` / `offline` (retained, LWT) | on connect / disconnect |

### Inverter Data (`growatt/data`)

Nested JSON. Power in W, voltage in V, current in A, frequency in Hz, energy in kWh.

```json
{
  "status": 1,
  "solarpower": 5234.0,
  "pv1": { "voltage": 412.5, "current": 8.2, "power": 3382.0 },
  "pv2": { "voltage": 405.1, "current": 4.5, "power": 1822.0 },
  "pv3": { "voltage": 0.0,   "current": 0.0, "power": 0.0 },
  "grid": {
    "outputpower": 5100.0,
    "frequency": 50.0,
    "l1": { "voltage": 230.5, "current": 7.4, "power": 1705.0 },
    "l2": { "voltage": 231.0, "current": 7.3, "power": 1686.0 },
    "l3": { "voltage": 229.8, "current": 7.5, "power": 1723.0 },
    "voltage_rs": 399.5, "voltage_st": 400.1, "voltage_tr": 398.9,
    "apparent_power": 5200.0,
    "reactive_power": 120.0,
    "reactive_power_max": 6000.0,
    "reactive_power_total": 45.0
  },
  "energy": {
    "today": 12.5, "total": 3456.7, "pv_total": 3460.0, "worktime": 1234567.0,
    "pv1_today": 7.2, "pv1_total": 2000.0,
    "pv2_today": 5.3, "pv2_total": 1456.0,
    "pv3_today": 0.0, "pv3_total": 0.0
  }
}
```

`status`: `0` = waiting, `1` = normal, `3` = fault.

### Diagnostics (`growatt/diagnostics`)

```json
{
  "temperature": { "inverter": 42.5, "ipm": 38.1, "boost": 40.2, "temp4": 0.0 },
  "bus": { "p_voltage": 380.0, "n_voltage": 380.0, "bat_volt_dsp": 0.0 },
  "ipf": 1000,
  "output_percent": 85,
  "max_power_limit": 20000.0,
  "derating_mode": 0,
  "fault": { "maincode": 0, "subcode": 0 },
  "warning": { "bit_h": 0, "subcode": 0, "maincode": 0 },
  "remote_ctrl_en": 0, "remote_ctrl_power": 0,
  "safety": { "pv_iso": 2000, "gfci": 5, "dci_r": 0.0, "dci_s": 0.0, "dci_t": 0.0 },
  "pid": {
    "status": 0,
    "pv1_voltage": 0.0, "pv1_current": 0.0,
    "pv2_voltage": 0.0, "pv2_current": 0.0,
    "pv3_voltage": 0.0, "pv3_current": 0.0,
    "fault_code": 0
  },
  "strings": { "unmatch": 0, "unbalance": 0, "disconnect": 0, "prompt": 0 },
  "fan_fault": 0
}
```

### System Status (`growatt/status`)

```json
{
  "rssi": -55,
  "uptime": 86400,
  "ssid": "YourWiFi",
  "ip": "10.0.20.7",
  "hostname": "growatt-aabbcc",
  "version": "v2.1.0",
  "modbus": { "reads": 50000, "errors": 12, "error_rate": 0.0 },
  "heap": { "current": 210000, "min": 205000, "max": 215000, "avg": 209000, "min_ever": 200000 },
  "cycle_ms": { "min": 180, "max": 320, "avg": 210, "last": 205 }
}
```

### Connection Status (`growatt/connection`)
- `online` — published retained on connect
- `offline` — Last Will and Testament, published by the broker when the device drops

## Setup Instructions

### 1. Install PlatformIO

Install the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension in VS Code.

This project uses the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform fork, because the ESP32-C6 requires arduino-esp32 3.x (see `platformio.ini`). PlatformIO downloads it automatically on first build.

### 2. Configure Credentials

```bash
# Copy the example config
cp include/config.example.h include/config.h

# Edit with your credentials
nano include/config.h
```

Required settings:
- WiFi SSID and password
- MQTT broker IP, port, username, password
- MQTT topic root (default `growatt`)
- OTA password (`OTA_PASSWORD`) — also used automatically for OTA uploads

`config.h` is gitignored and will never be committed.

### 3. Build and Upload (first flash over USB)

```bash
# Build
pio run

# Upload via USB (default environment)
pio run -e esp32-c6-devkitc-1 -t upload

# Monitor serial output
pio device monitor
```

The partition table (`min_spiffs.csv`, set in `platformio.ini`) can only be written over USB, so the **first** flash must be wired.

### 4. Future OTA Updates

After the first USB flash, update wirelessly:

```bash
pio run -e ota -t upload
```

- Set the device IP in `platformio.ini` under `[env:ota]` → `upload_port`.
- The `--auth` password is read automatically from `OTA_PASSWORD` in `config.h` by `scripts/ota_auth.py`, so there is nothing else to configure.

## Configuration

All settings live in `include/config.h` (copied from `config.example.h`).

### Timing Settings
```cpp
#define UPDATE_INTERVAL     2         // Read inverter & publish every N seconds
#define STATUS_INTERVAL     30        // Publish system status every N seconds
```

### Modbus Settings
Defaults work for most Growatt inverters:
```cpp
#define MODBUS_SLAVE_ID     1         // Inverter slave ID
#define MODBUS_BAUD         9600      // Growatt default baud rate
```

## Troubleshooting

### No Data from Inverter
1. **Check wiring**: Verify A/B connections (try swapping A and B if no response).
2. **Check slave ID**: Default is 1; verify in inverter settings.
3. **Check baud rate**: Should be 9600 (Growatt default).
4. **Check cable**: Use shielded twisted pair, max 1200 m.
5. **Serial monitor / status topic**: Watch the Modbus error rate in `growatt/status`.

### MQTT Not Connecting
1. **Verify broker IP**: Ping your MQTT broker.
2. **Check credentials**: Username/password must match.
3. **Check firewall**: Port 1883 must be open.
4. **Serial monitor**: Connection failures print an `rc=` code.

### OTA Not Working
1. **Check password**: `OTA_PASSWORD` in `config.h` must match the running firmware.
2. **Reachability**: Set `upload_port` to the device IP (or `<hostname>.local`); device and computer must be on the same network.
3. **Port**: ArduinoOTA uses port `3232` (`OTA_PORT`) — ensure it is reachable.
4. **First flash**: OTA only works after the initial USB flash.

## MQTT Integration

### Node-RED Example
```json
[{
  "id": "mqtt-in",
  "type": "mqtt in",
  "topic": "growatt/data",
  "broker": "your-mqtt-broker",
  "name": "Growatt Data"
}]
```

### Home Assistant Example
```yaml
mqtt:
  sensor:
    - name: "Solar Power"
      state_topic: "growatt/data"
      value_template: "{{ value_json.solarpower }}"
      unit_of_measurement: "W"
      device_class: power
    - name: "Solar Energy Today"
      state_topic: "growatt/data"
      value_template: "{{ value_json.energy.today }}"
      unit_of_measurement: "kWh"
      device_class: energy
```

## Technical Details

### Modbus Protocol
- **Protocol**: Modbus RTU over RS485
- **Function code**: 0x04 (Read Input Registers)
- **Read strategy**: registers 0–237 fetched in 4 blocks (0–63, 64–124, 125–189, 190–237) with a 50 ms settle between blocks
- **Byte order**: Big-endian (MSB first); 32-bit values are high-word/low-word pairs

### Resource Notes
- **MQTT buffer**: 2048 bytes (`mqtt.setBufferSize`)
- **Flash**: 4 MB, `min_spiffs` partition layout → two ~1.875 MB OTA app slots
- **Performance window**: 30-sample rolling average (`PERF_WINDOW`) for heap and cycle time
- Live heap, cycle timing and Modbus error rate are reported in the `growatt/status` topic

## Documentation

- [Growatt Modbus Protocol PDF](docs/Growatt%20PV%20Inverter%20Modbus%20RS485%20RTU%20Protocol%20v120.pdf)
- [Growatt MID15KTL3 User Manual](docs/Growatt-MID15_20KTL3-XL-User-Manual.pdf)

## Version History

- **v2.1.0** (current): ESP32-C6 platform (pioarduino / arduino-esp32 3.x); TTL485-V2.0 auto-direction RS485; full MID 15/20KTL3-XL register map; separate `diagnostics` topic; performance/heap monitoring in `status`; OTA via dedicated `ota` environment with password sourced from `config.h`.
- **v1.3.0**: Original ESP8266 release — core inverter monitoring, system status reporting and OTA support.

## Support

For issues or questions:
- Check serial monitor output for errors
- Watch the `growatt/status` topic for Modbus error rate and gateway health
- Verify wiring and configuration
- Consult the Growatt Modbus protocol documentation in [docs/](docs/)
