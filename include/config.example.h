// Configuration Template for Growatt Solar MQTT Gateway (ESP32-C6)
//
// INSTRUCTIONS:
// 1. Copy this file to config.h: cp include/config.example.h include/config.h
// 2. Edit config.h with your actual credentials
// 3. config.h is in .gitignore and will NOT be committed to Git
//
// IMPORTANT: Never commit config.h with real credentials!

#ifndef CONFIG_H
#define CONFIG_H

// ========== NETWORK CONFIGURATION ==========

// WiFi Credentials
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"

// MQTT Broker Settings
#define MQTT_SERVER         "192.168.x.x"      // Broker IP or hostname
#define MQTT_PORT           1883
#define MQTT_USER           "mqtt_username"
#define MQTT_PASSWORD       "mqtt_password"
#define MQTT_CLIENT_ID      "Growatt"
#define MQTT_TOPIC_ROOT     "growatt"

// OTA (Over-The-Air) Update Settings
#define OTA_PASSWORD        "ota_password"
#define OTA_PORT            3232

// ========== HARDWARE CONFIGURATION ==========
// ESP32-C6-Mini + TTL485-V2.0 RS485 module (auto direction, no DE/RE pins)
#define RS485_RX            4         // GPIO4 - UART1 RX (from TTL485 RX)
#define RS485_TX            5         // GPIO5 - UART1 TX (to TTL485 TX)
#define STATUS_LED          8         // GPIO8 - Onboard LED

// Serial Settings
#define SERIAL_RATE         115200

// ========== MODBUS CONFIGURATION ==========
#define MODBUS_SLAVE_ID     1         // Inverter slave ID
#define MODBUS_BAUD         9600      // Growatt default baud rate
#define UPDATE_INTERVAL     2         // Read inverter every N seconds
#define STATUS_INTERVAL     30        // Send system status every N seconds

// ========== ADVANCED SETTINGS ==========
#define MQTT_RECONNECT_INTERVAL 5000  // ms between MQTT reconnect attempts
#define PERF_WINDOW         30        // Performance stats rolling window (samples)

// Firmware Version
#define FIRMWARE_VERSION    "v2.1.0"

#endif // CONFIG_H
