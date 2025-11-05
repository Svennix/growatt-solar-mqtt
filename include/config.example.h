// Configuration Template for Growatt Solar MQTT Gateway
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
#define MQTT_SERVER         "192.168.x.x"      // Your MQTT broker IP
#define MQTT_PORT           1883
#define MQTT_USER           "mqtt_username"
#define MQTT_PASSWORD       "mqtt_password"
#define MQTT_CLIENT_ID      "growatt_gateway"
#define MQTT_TOPIC_ROOT     "solar/growatt"

// OTA (Over-The-Air) Update Settings
#define OTA_HOSTNAME        "growatt-gateway"
#define OTA_PASSWORD        "ota_password"

// ========== HARDWARE CONFIGURATION ==========

// RS485 Pin Mappings (NodeMCU)
#define MAX485_DE           5         // D1 - RS485 Direction Enable
#define MAX485_RE_NEG       4         // D2 - RS485 Receive Enable
#define MAX485_RX           14        // D5 - RS485 Receive
#define MAX485_TX           12        // D6 - RS485 Transmit
#define STATUS_LED          2         // Built-in LED

// Serial Settings
#define SERIAL_RATE         115200
#define MODBUS_RATE         9600

// ========== MODBUS CONFIGURATION ==========

// Growatt Inverter Settings
#define MODBUS_SLAVE_ID     1
#define UPDATE_INTERVAL     8         // Read data every 8 seconds
#define STATUS_INTERVAL     30        // Send status every 30 seconds

// ========== OPTIONAL SETTINGS ==========

// Enable debug output
// #define DEBUG_ENABLED

// Firmware Version
#define FIRMWARE_VERSION    "v1.3.0"

#endif // CONFIG_H
