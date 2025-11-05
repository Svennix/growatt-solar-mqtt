// ESP8266 GROWATT MID 15KTL3 to MQTT Gateway

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <ArduinoOTA.h>

// ========== CONFIGURATION ==========

// Network Settings
const char* ssid = "SSID";
const char* password = "PASSWORD";
const char* mqtt_server = "MQTT IP";
const char* mqtt_user = "MQTT USER";
const char* mqtt_password = "MQTT PASSWORD";
const char* clientID = "CLIENT ID";
const char* topicRoot = "TOPIC";
const char* ota_password = "OTA PASSWORD";

// Hardware Settings
#define SERIAL_RATE     115200
#define MAX485_DE       5         // D1 - RS485 Direction Enable
#define MAX485_RE_NEG   4         // D2 - RS485 Receive Enable
#define MAX485_RX       14        // D5 - RS485 Receive
#define MAX485_TX       12        // D6 - RS485 Transmit
#define STATUS_LED      2         // Built-in LED

// Timing Settings
#define UPDATE_MODBUS   8         // Read inverter data every 8 seconds
#define UPDATE_STATUS   30        // Send status every 30 seconds
#define WIFICHECK       500       // WiFi check interval (ms)

// Modbus Settings
#define SLAVE_ID        1
#define MODBUS_RATE     9600

// ========== GLOBAL VARIABLES ==========
unsigned long uptime = 0;
unsigned long seconds = 0;
unsigned long lastWifiCheck = 0;
char newclientid[80];
char buildversion[12] = "v1.3.0";

// MQTT Topics
char topicData[40];
char topicError[40]; 
char topicStatus[40];
char topicConnection[40];

// ========== OBJECTS ==========
os_timer_t myTimer;
WiFiClient espClient;
PubSubClient mqtt(mqtt_server, 1883, 0, espClient);

// ========== GROWATT INTERFACE CLASS ==========
class growattIF {
private:
    ModbusMaster growattInterface;
    SoftwareSerial *serial;
    int setcounter = 0;
    int overflow;
    
    // Inverter Data Structure
    struct inverter_data {
        // Status
        int status;
        
        // PV Input Data
        float solarpower;
        float pv1voltage, pv1current, pv1power;
        float pv2voltage, pv2current, pv2power;
        float pv3voltage, pv3current, pv3power;
        
        // AC Output Data
        float outputpower, gridfrequency;
        float gridvoltagel1, gridvoltagel2, gridvoltagel3;
        float gridcurrentl1, gridcurrentl2, gridcurrentl3;
        float gridpowerl1, gridpowerl2, gridpowerl3;
        float gridrsvoltage, gridstvoltage, gridtrvoltage;
        
        // Energy Data
        float energytoday, energytotal, totalworktime;
        float pv1energytoday, pv1energytotal;
        float pv2energytoday, pv2energytotal;
        float pv3energytoday, pv3energytotal;
        float opfullpower;
        
        // Temperature Data
        float tempinverter, tempipm, tempboost;
        
        // Diagnostic Data
        int ipf, realoppercent, deratingmode;
        int faultcode, faultbitcode, warningbitcode;
    } data;
    
    void preTransmission() {
        digitalWrite(MAX485_RE_NEG, 1);
        digitalWrite(MAX485_DE, 1);
    }
    
    void postTransmission() {
        digitalWrite(MAX485_RE_NEG, 0);
        digitalWrite(MAX485_DE, 0);
    }

public:
    static const uint8_t Success = 0x00;
    static const uint8_t Continue = 0xFF;
    
    growattIF() {
        pinMode(MAX485_RE_NEG, OUTPUT);
        pinMode(MAX485_DE, OUTPUT);
        digitalWrite(MAX485_RE_NEG, 0);
        digitalWrite(MAX485_DE, 0);
    }
    
    void init() {
        serial = new SoftwareSerial(MAX485_RX, MAX485_TX, false);
        serial->begin(MODBUS_RATE);
        growattInterface.begin(SLAVE_ID, *serial);
        
        static growattIF* obj = this;
        growattInterface.preTransmission([]() { obj->preTransmission(); });
        growattInterface.postTransmission([]() { obj->postTransmission(); });
    }
    
    const char* getErrorMessage(uint8_t result) {
        switch(result) {
            case 0x01: return "Illegal function";
            case 0x02: return "Illegal data address";
            case 0x03: return "Illegal data value";
            case 0x04: return "Slave device failure";
            case 0xE0: return "Invalid slave ID";
            case 0xE1: return "Invalid function";
            case 0xE2: return "Response timed out";
            case 0xE3: return "Invalid CRC";
            default: return "Unknown error";
        }
    }
    
    uint8_t readData(char* json) {
        uint8_t result;
        
        ESP.wdtDisable();
        result = growattInterface.readInputRegisters(setcounter * 64, 64);
        ESP.wdtEnable(1);
        
        if (result == growattInterface.ku8MBSuccess) {
            if (setcounter == 0) {
                // Read registers 0-63
                readFirstBlock();
                setcounter++;
                return Continue;
            }
            
            if (setcounter == 1) {
                // Read registers 64-127
                readSecondBlock();
                setcounter = 0;
                
                // Generate JSON with all data
                generateJSON(json);
            }
        }
        
        return result;
    }
    
private:
    void readFirstBlock() {
        // Status and PV Input
        data.status = growattInterface.getResponseBuffer(0);
        data.solarpower = ((growattInterface.getResponseBuffer(1) << 16) | growattInterface.getResponseBuffer(2)) * 0.1;
        
        data.pv1voltage = growattInterface.getResponseBuffer(3) * 0.1;
        data.pv1current = growattInterface.getResponseBuffer(4) * 0.1;
        data.pv1power = ((growattInterface.getResponseBuffer(5) << 16) | growattInterface.getResponseBuffer(6)) * 0.1;
        
        data.pv2voltage = growattInterface.getResponseBuffer(7) * 0.1;
        data.pv2current = growattInterface.getResponseBuffer(8) * 0.1;
        data.pv2power = ((growattInterface.getResponseBuffer(9) << 16) | growattInterface.getResponseBuffer(10)) * 0.1;
        
        data.pv3voltage = growattInterface.getResponseBuffer(11) * 0.1;
        data.pv3current = growattInterface.getResponseBuffer(12) * 0.1;
        data.pv3power = ((growattInterface.getResponseBuffer(13) << 16) | growattInterface.getResponseBuffer(14)) * 0.1;
        
        // AC Output
        data.outputpower = ((growattInterface.getResponseBuffer(35) << 16) | growattInterface.getResponseBuffer(36)) * 0.1;
        data.gridfrequency = growattInterface.getResponseBuffer(37) * 0.01;
        
        // 3-Phase Grid Data
        data.gridvoltagel1 = growattInterface.getResponseBuffer(38) * 0.1;
        data.gridcurrentl1 = growattInterface.getResponseBuffer(39) * 0.1;
        data.gridpowerl1 = ((growattInterface.getResponseBuffer(40) << 16) | growattInterface.getResponseBuffer(41)) * 0.1;
        
        data.gridvoltagel2 = growattInterface.getResponseBuffer(42) * 0.1;
        data.gridcurrentl2 = growattInterface.getResponseBuffer(43) * 0.1;
        data.gridpowerl2 = ((growattInterface.getResponseBuffer(44) << 16) | growattInterface.getResponseBuffer(45)) * 0.1;
        
        data.gridvoltagel3 = growattInterface.getResponseBuffer(46) * 0.1;
        data.gridcurrentl3 = growattInterface.getResponseBuffer(47) * 0.1;
        data.gridpowerl3 = ((growattInterface.getResponseBuffer(48) << 16) | growattInterface.getResponseBuffer(49)) * 0.1;
        
        data.gridrsvoltage = growattInterface.getResponseBuffer(50) * 0.1;
        data.gridstvoltage = growattInterface.getResponseBuffer(51) * 0.1;
        data.gridtrvoltage = growattInterface.getResponseBuffer(52) * 0.1;
        
        // Energy Data Part 1
        data.energytoday = ((growattInterface.getResponseBuffer(53) << 16) | growattInterface.getResponseBuffer(54)) * 0.1;
        data.energytotal = ((growattInterface.getResponseBuffer(55) << 16) | growattInterface.getResponseBuffer(56)) * 0.1;
        data.totalworktime = ((growattInterface.getResponseBuffer(57) << 16) | growattInterface.getResponseBuffer(58)) * 0.5;
        
        data.pv1energytoday = ((growattInterface.getResponseBuffer(59) << 16) | growattInterface.getResponseBuffer(60)) * 0.1;
        data.pv1energytotal = ((growattInterface.getResponseBuffer(61) << 16) | growattInterface.getResponseBuffer(62)) * 0.1;
        
        overflow = growattInterface.getResponseBuffer(62);
    }
    
    void readSecondBlock() {
        // Energy Data Part 2
        data.pv2energytoday = ((growattInterface.getResponseBuffer(63-64) << 16) | growattInterface.getResponseBuffer(64-64)) * 0.1;
        data.pv2energytotal = ((growattInterface.getResponseBuffer(65-64) << 16) | growattInterface.getResponseBuffer(66-64)) * 0.1;
        data.pv3energytoday = ((growattInterface.getResponseBuffer(67-64) << 16) | growattInterface.getResponseBuffer(68-64)) * 0.1;
        data.pv3energytotal = ((growattInterface.getResponseBuffer(69-64) << 16) | growattInterface.getResponseBuffer(70-64)) * 0.1;
        
        // Temperature Data
        data.tempinverter = growattInterface.getResponseBuffer(93-64) * 0.1;
        data.tempipm = growattInterface.getResponseBuffer(94-64) * 0.1;
        data.tempboost = growattInterface.getResponseBuffer(95-64) * 0.1;
        
        // Diagnostic Data
        data.ipf = growattInterface.getResponseBuffer(100-64);
        data.realoppercent = growattInterface.getResponseBuffer(101-64);
        data.opfullpower = ((growattInterface.getResponseBuffer(102-64) << 16) | growattInterface.getResponseBuffer(103-64)) * 0.1;
        data.deratingmode = growattInterface.getResponseBuffer(103-64);
        data.faultcode = growattInterface.getResponseBuffer(105-64);
        data.faultbitcode = ((growattInterface.getResponseBuffer(105-64) << 16) | growattInterface.getResponseBuffer(106-64));
        data.warningbitcode = ((growattInterface.getResponseBuffer(110-64) << 16) | growattInterface.getResponseBuffer(111-64));
    }
    
    void generateJSON(char* json) {
        snprintf(json, 1024,
            "{"
            "\"status\":%d,"
            "\"solarpower\":%.1f,"
            "\"pv1voltage\":%.1f,\"pv1current\":%.1f,\"pv1power\":%.1f,"
            "\"pv2voltage\":%.1f,\"pv2current\":%.1f,\"pv2power\":%.1f,"
            "\"pv3voltage\":%.1f,\"pv3current\":%.1f,\"pv3power\":%.1f,"
            "\"outputpower\":%.1f,\"gridfrequency\":%.2f,"
            "\"gridvoltagel1\":%.1f,\"gridcurrentl1\":%.1f,\"gridpowerl1\":%.1f,"
            "\"gridvoltagel2\":%.1f,\"gridcurrentl2\":%.1f,\"gridpowerl2\":%.1f,"
            "\"gridvoltagel3\":%.1f,\"gridcurrentl3\":%.1f,\"gridpowerl3\":%.1f,"
            "\"gridrsvoltage\":%.1f,\"gridstvoltage\":%.1f,\"gridtrvoltage\":%.1f,"
            "\"energytoday\":%.1f,\"energytotal\":%.1f,\"totalworktime\":%.1f,"
            "\"pv1energytoday\":%.1f,\"pv1energytotal\":%.1f,"
            "\"pv2energytoday\":%.1f,\"pv2energytotal\":%.1f,"
            "\"pv3energytoday\":%.1f,\"pv3energytotal\":%.1f,"
            "\"opfullpower\":%.1f,"
            "\"tempinverter\":%.1f,\"tempipm\":%.1f,\"tempboost\":%.1f,"
            "\"ipf\":%d,\"realoppercent\":%d,\"deratingmode\":%d,"
            "\"faultcode\":%d,\"faultbitcode\":%d,\"warningbitcode\":%d"
            "}",
            data.status, data.solarpower,
            data.pv1voltage, data.pv1current, data.pv1power,
            data.pv2voltage, data.pv2current, data.pv2power,
            data.pv3voltage, data.pv3current, data.pv3power,
            data.outputpower, data.gridfrequency,
            data.gridvoltagel1, data.gridcurrentl1, data.gridpowerl1,
            data.gridvoltagel2, data.gridcurrentl2, data.gridpowerl2,
            data.gridvoltagel3, data.gridcurrentl3, data.gridpowerl3,
            data.gridrsvoltage, data.gridstvoltage, data.gridtrvoltage,
            data.energytoday, data.energytotal, data.totalworktime,
            data.pv1energytoday, data.pv1energytotal,
            data.pv2energytoday, data.pv2energytotal,
            data.pv3energytoday, data.pv3energytotal,
            data.opfullpower,
            data.tempinverter, data.tempipm, data.tempboost,
            data.ipf, data.realoppercent, data.deratingmode,
            data.faultcode, data.faultbitcode, data.warningbitcode);
    }
};

// Create interface instance
growattIF inverter;

// ========== MAIN FUNCTIONS ==========

void readInverterData() {
    char json[1024];
    
    digitalWrite(STATUS_LED, 0);
    uint8_t result = inverter.readData(json);
    
    if (result == inverter.Success) {
        mqtt.publish(topicData, json);
        Serial.println(F("Inverter data published"));
    } else if (result != inverter.Continue) {
        const char* message = inverter.getErrorMessage(result);
        Serial.printf("Read error: %s\n", message);
        mqtt.publish(topicError, message);
    }
    digitalWrite(STATUS_LED, 1);
}

void timerCallback(void *pArg) {
    seconds++;
    
    // Read inverter data
    if (seconds % UPDATE_MODBUS == 0) {
        readInverterData();
    }
    
    // Send system status
    if (seconds % UPDATE_STATUS == 0) {
        sendSystemStatus();
    }
}

void sendSystemStatus() {
    if (mqtt_server != "") {
        char value[300];
        snprintf(value, sizeof(value),
            "{\"rssi\":%d,\"uptime\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"clientid\":\"%s\",\"version\":\"%s\",\"freeheap\":%d}",
            WiFi.RSSI(), uptime, WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
            newclientid, buildversion, ESP.getFreeHeap());
        mqtt.publish(topicStatus, value);
        Serial.printf("Status sent - Free heap: %d bytes\n", ESP.getFreeHeap());
    }
}

void connectMQTT() {
    while (!mqtt.connected()) {
        Serial.print(F("Connecting to MQTT..."));
        
        byte mac[6];
        WiFi.macAddress(mac);
        sprintf(newclientid, "%s-%02x%02x%02x", clientID, mac[2], mac[1], mac[0]);
        
        if (mqtt.connect(newclientid, mqtt_user, mqtt_password, topicConnection, 1, true, "offline")) {
            Serial.println(F(" connected"));
            mqtt.publish(topicConnection, "online", true);
        } else {
            Serial.printf(" failed (rc=%d), retrying in 5s\n", mqtt.state());
            delay(5000);
        }
    }
}

void onMQTTMessage(char* topic, byte* payload, unsigned int length) {
    payload[length] = '\0';
    String message = (char*)payload;
    Serial.printf("MQTT message: %s = %s\n", topic, message.c_str());
    // Future: Add control commands here if needed
}

void setupOTA() {
    byte mac[6];
    WiFi.macAddress(mac);
    char hostname[30];
    sprintf(hostname, "growatt-%02x%02x%02x", mac[2], mac[1], mac[0]);
    
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setPassword(ota_password);
    
    ArduinoOTA.onStart([]() {
        os_timer_disarm(&myTimer);
        Serial.println(F("OTA Update Start"));
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println(F("\nOTA Update Complete"));
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]\n", error);
        os_timer_arm(&myTimer, 1000, true);
    });
    
    ArduinoOTA.begin();
    Serial.printf("OTA Ready - Hostname: %s\n", hostname);
}

// ========== SETUP ==========
void setup() {
    Serial.begin(SERIAL_RATE);
    Serial.println(F("\n=== GROWATT MID 15KTL3 Gateway ==="));
    Serial.println(F("Starting system initialization..."));
    
    // Initialize hardware
    pinMode(STATUS_LED, OUTPUT);
    
    // Build MQTT topics
    sprintf(topicData, "%s/data", topicRoot);
    sprintf(topicError, "%s/error", topicRoot);
    sprintf(topicStatus, "%s/status", topicRoot);
    sprintf(topicConnection, "%s/connection", topicRoot);
    
    // Connect to WiFi
    Serial.print(F("Connecting to WiFi"));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(F("."));
        seconds++;
        if (seconds > 180) {
            Serial.println(F("\nWiFi timeout - restarting"));
            ESP.restart();
        }
    }
    seconds = 0;
    
    Serial.println(F("\nWiFi connected"));
    Serial.printf("IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    
    // Initialize Modbus interface
    inverter.init();
    Serial.println(F("Modbus interface initialized"));
    
    // Setup timer
    os_timer_setfn(&myTimer, timerCallback, NULL);
    os_timer_arm(&myTimer, 1000, true);
    Serial.println(F("Timer started (1 second intervals)"));
    
    // Setup MQTT
    mqtt.setServer(mqtt_server, 1883);
    mqtt.setBufferSize(1100);
    mqtt.setCallback(onMQTTMessage);
    Serial.printf("MQTT configured for server: %s\n", mqtt_server);
    
    // Setup OTA
    setupOTA();
    
    Serial.println(F("=== Initialization Complete ==="));
    Serial.printf("Reading inverter data every %d seconds\n", UPDATE_MODBUS);
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
}

// ========== MAIN LOOP ==========
void loop() {
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Handle MQTT
    if (!mqtt.connected()) {
        connectMQTT();
    }
    mqtt.loop();
    
    // Update uptime counter
    static unsigned long lastTick = 0;
    if (millis() - lastTick >= 60000) {
        lastTick = millis();
        uptime++;
    }
    
    // Check WiFi connection
    if (millis() - lastWifiCheck >= WIFICHECK) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi reconnecting..."));
            WiFi.reconnect();
        }
        lastWifiCheck = millis();
    }
}