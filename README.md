# Growatt Solar Inverter MQTT Gateway

ESP8266-based gateway that reads data from a Growatt MID 15KTL3 solar inverter via Modbus RS485 and publishes it to MQTT.

## Hardware

### Required Components
- **ESP8266 NodeMCU** (or Wemos D1 Mini)
- **MAX485 TTL to RS485 Module**
- **Growatt MID 15KTL3** solar inverter (or compatible models)
- Jumper wires
- USB cable for programming

### Wiring Diagram

```
ESP8266 NodeMCU    →    MAX485 Module
==========================================
D1 (GPIO 5)        →    DE (Driver Enable)
D2 (GPIO 4)        →    RE (Receiver Enable - active low)
D5 (GPIO 14)       →    RO (Receiver Output)
D6 (GPIO 12)       →    DI (Driver Input)
3.3V               →    VCC
GND                →    GND

MAX485 Module      →    Growatt Inverter
==========================================
A                  →    RS485 A+
B                  →    RS485 B-
```

**Important Notes:**
- Connect DE and RE pins together on the MAX485
- Use shielded twisted pair cable for RS485 connection
- Maximum cable length: 1200m (though typically much shorter)
- Termination resistors may be required for long cables

## Features

- ✅ **Complete inverter monitoring**: Reads all PV input, AC output, and energy data
- ✅ **MQTT publishing**: Sends data every 8 seconds to MQTT broker
- ✅ **System status**: Reports WiFi RSSI, uptime, free heap every 30 seconds
- ✅ **OTA updates**: Update firmware wirelessly
- ✅ **Error handling**: Reports Modbus communication errors
- ✅ **WiFi reconnection**: Automatic WiFi recovery
- ✅ **LED status**: Built-in LED blinks during data reads

## Data Published

### Inverter Data (`solar/growatt/data`)

**PV Input:**
- Solar power (total)
- PV1/2/3: voltage, current, power
- PV1/2/3: daily energy, total energy

**AC Output:**
- Total output power
- Grid frequency
- L1/L2/L3: voltage, current, power
- Phase voltages (RS, ST, TR)

**Energy:**
- Today's energy production
- Total energy production
- Total work time

**Temperature:**
- Inverter temperature
- IPM temperature
- Boost temperature

**Diagnostics:**
- Inverter power factor
- Real output percent
- Derating mode
- Fault codes and warnings

### System Status (`solar/growatt/status`)
```json
{
  "rssi": -45,
  "uptime": 1234,
  "ssid": "YourWiFi",
  "ip": "192.168.1.100",
  "clientid": "growatt_gateway-aabbcc",
  "version": "v1.3.0",
  "freeheap": 25000
}
```

### Connection Status (`solar/growatt/connection`)
- `online` (LWT: Last Will and Testament)
- `offline` (when disconnected)

## Setup Instructions

### 1. Install PlatformIO

Install [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension in VS Code.

### 2. Configure Credentials

```bash
# Copy the example config
cp include/config.example.h include/config.h

# Edit with your credentials
nano include/config.h
```

Required settings:
- WiFi SSID and password
- MQTT broker IP, username, password
- MQTT topic root (e.g., `solar/growatt`)
- OTA password

### 3. Build and Upload

```bash
# Build the project
pio run

# Upload to ESP8266 (via USB)
pio run --target upload

# Monitor serial output
pio device monitor
```

### 4. Future OTA Updates

After the first upload, you can update wirelessly:

```bash
# Uncomment OTA settings in platformio.ini
# Set upload_port to your ESP8266's IP address
# Set upload auth password

pio run --target upload
```

## Configuration

### Timing Settings

Edit in `include/config.h`:
```cpp
#define UPDATE_INTERVAL     8         // Read data every 8 seconds
#define STATUS_INTERVAL     30        // Send status every 30 seconds
```

### Modbus Settings

Default settings work for most Growatt inverters:
```cpp
#define MODBUS_SLAVE_ID     1         // Inverter slave ID
#define MODBUS_RATE         9600      // Baud rate
```

## Troubleshooting

### No Data from Inverter

1. **Check wiring**: Verify A/B connections are correct
2. **Check slave ID**: Default is 1, but verify in inverter settings
3. **Check baud rate**: Should be 9600 (Growatt default)
4. **Check cable**: Use shielded twisted pair, max 1200m
5. **Serial monitor**: Check for Modbus error messages

### MQTT Not Connecting

1. **Verify broker IP**: Ping your MQTT broker
2. **Check credentials**: Username/password must match
3. **Check firewall**: Port 1883 must be open
4. **Serial monitor**: Look for MQTT error codes

### OTA Not Working

1. **Check password**: OTA_PASSWORD in config.h
2. **Same network**: ESP8266 and computer must be on same network
3. **Firewall**: Port 8266 must be accessible
4. **Memory**: OTA requires sufficient free heap

## MQTT Integration

### Node-RED Example

```json
[{
  "id": "mqtt-in",
  "type": "mqtt in",
  "topic": "solar/growatt/data",
  "broker": "your-mqtt-broker",
  "name": "Growatt Data"
}]
```

### Home Assistant Example

```yaml
sensor:
  - platform: mqtt
    name: "Solar Power"
    state_topic: "solar/growatt/data"
    value_template: "{{ value_json.solarpower }}"
    unit_of_measurement: "W"
    device_class: power
```

## Technical Details

### Modbus Protocol
- **Protocol**: Modbus RTU over RS485
- **Function code**: 0x04 (Read Input Registers)
- **Register range**: 0-127 (read in two blocks of 64)
- **Byte order**: Big-endian (MSB first)

### Memory Usage
- **Sketch size**: ~350KB
- **Free heap**: ~25KB (typical)
- **MQTT buffer**: 1100 bytes

## Documentation

- [Growatt Modbus Protocol PDF](Growatt%20PV%20Inverter%20Modbus%20RS485%20RTU%20Protocol%20v120.pdf)
- [Growatt MID15KTL3 User Manual](Growatt-MID15_20KTL3-XL-User-Manual.pdf)

## Version History

- **v1.3.0**: Current version - Stable release with full feature set
- Improved error handling
- Added system status reporting
- OTA support

## Support

For issues or questions:
- Check serial monitor output for errors
- Verify wiring and configuration
- Consult Growatt Modbus protocol documentation
