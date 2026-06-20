// ESP32-C6 GROWATT MID 15/20KTL3-XL to MQTT Gateway
// Copyright (c) 2025 Svein Arne Dammen
// All rights reserved.
//
// Hardware: ESP32-C6-Mini + TTL485-V2.0 RS485 module (auto direction)
//
// MQTT Topics published:
//   {topicRoot}/data         - Inverter data (every UPDATE_INTERVAL s)
//   {topicRoot}/diagnostics  - Temps, faults, safety, PID (every UPDATE_INTERVAL s)
//   {topicRoot}/status       - System performance stats (every STATUS_INTERVAL s)
//   {topicRoot}/connection   - "online"/"offline" (retained, LWT)

#include <WiFi.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Ticker.h>

// All credentials, pins and timing live in config.h (gitignored).
// Copy include/config.example.h to include/config.h and edit it.
#include "config.h"

// ========== CONFIGURATION ==========
// Network settings (sourced from config.h)
const char* ssid          = WIFI_SSID;
const char* password      = WIFI_PASSWORD;
const char* mqtt_server   = MQTT_SERVER;
const int   mqtt_port     = MQTT_PORT;
const char* mqtt_user     = MQTT_USER;
const char* mqtt_password = MQTT_PASSWORD;
const char* clientID      = MQTT_CLIENT_ID;
const char* topicRoot     = MQTT_TOPIC_ROOT;
const char* ota_password  = OTA_PASSWORD;

// ========== MQTT TOPICS ==========
char topicData[64];
char topicDiag[64];
char topicStatus[64];
char topicConnection[64];

// ========== GLOBAL OBJECTS ==========
WiFiClient espClient;
PubSubClient mqtt(espClient);
ModbusMaster modbus;
HardwareSerial RS485Serial(1);  // UART1
Ticker timerTick;

// ========== STATE VARIABLES ==========
volatile unsigned long seconds = 0;
unsigned long uptimeSeconds = 0;
unsigned long lastUptimeTick = 0;
unsigned long lastMqttAttempt = 0;
char deviceHostname[32];
const char* buildversion = FIRMWARE_VERSION;

// Modbus error tracking
unsigned long modbusReadCount = 0;
unsigned long modbusErrorCount = 0;

// ========== PERFORMANCE MONITORING ==========
#define PERF_SAMPLES (PERF_WINDOW)

struct PerfStats {
    uint32_t heapSamples[PERF_SAMPLES];
    uint8_t  heapIndex;
    uint8_t  heapCount;

    uint32_t cycleSamples[PERF_SAMPLES];
    uint8_t  cycleIndex;
    uint8_t  cycleCount;

    void init() {
        heapIndex = 0; heapCount = 0;
        cycleIndex = 0; cycleCount = 0;
    }

    void addHeapSample(uint32_t v) {
        heapSamples[heapIndex] = v;
        heapIndex = (heapIndex + 1) % PERF_SAMPLES;
        if (heapCount < PERF_SAMPLES) heapCount++;
    }

    void addCycleSample(uint32_t v) {
        cycleSamples[cycleIndex] = v;
        cycleIndex = (cycleIndex + 1) % PERF_SAMPLES;
        if (cycleCount < PERF_SAMPLES) cycleCount++;
    }

    uint32_t avg(uint32_t* arr, uint8_t count) {
        if (count == 0) return 0;
        uint64_t sum = 0;
        for (uint8_t i = 0; i < count; i++) sum += arr[i];
        return sum / count;
    }

    void getMinMax(uint32_t* arr, uint8_t count, uint32_t &mn, uint32_t &mx) {
        mn = UINT32_MAX; mx = 0;
        for (uint8_t i = 0; i < count; i++) {
            if (arr[i] < mn) mn = arr[i];
            if (arr[i] > mx) mx = arr[i];
        }
        if (count == 0) { mn = 0; mx = 0; }
    }
} perf;

// ========== INVERTER DATA STRUCTURE ==========
// Full register map for Growatt MID 15/20KTL3-XL
// Input registers (function 04): Group 1 (0-124), Group 2 (125-249)
struct InverterData {
    // --- Register 0: Status ---
    int status;                // 0=waiting, 1=normal, 3=fault

    // --- Registers 1-14: PV Input ---
    float solarPowerTotal;     // Reg 1-2:   Total input power (0.1W)
    float pv1Voltage;          // Reg 3:     PV1 voltage (0.1V)
    float pv1Current;          // Reg 4:     PV1 current (0.1A)
    float pv1Power;            // Reg 5-6:   PV1 power (0.1W)
    float pv2Voltage;          // Reg 7:     PV2 voltage (0.1V)
    float pv2Current;          // Reg 8:     PV2 current (0.1A)
    float pv2Power;            // Reg 9-10:  PV2 power (0.1W)
    float pv3Voltage;          // Reg 11:    PV3 voltage (0.1V)
    float pv3Current;          // Reg 12:    PV3 current (0.1A)
    float pv3Power;            // Reg 13-14: PV3 power (0.1W)

    // --- Registers 35-52: AC Output ---
    float outputPower;         // Reg 35-36: Total output power (0.1W)
    float gridFrequency;       // Reg 37:    Grid frequency (0.01Hz)
    float gridVoltageL1;       // Reg 38:    L1 voltage (0.1V)
    float gridCurrentL1;       // Reg 39:    L1 current (0.1A)
    float gridPowerL1;         // Reg 40-41: L1 power (0.1VA)
    float gridVoltageL2;       // Reg 42:    L2 voltage (0.1V)
    float gridCurrentL2;       // Reg 43:    L2 current (0.1A)
    float gridPowerL2;         // Reg 44-45: L2 power (0.1VA)
    float gridVoltageL3;       // Reg 46:    L3 voltage (0.1V)
    float gridCurrentL3;       // Reg 47:    L3 current (0.1A)
    float gridPowerL3;         // Reg 48-49: L3 power (0.1VA)
    float gridVoltageRS;       // Reg 50:    Line voltage R-S (0.1V)
    float gridVoltageST;       // Reg 51:    Line voltage S-T (0.1V)
    float gridVoltageTR;       // Reg 52:    Line voltage T-R (0.1V)

    // --- Registers 53-70: Energy Data ---
    float energyToday;         // Reg 53-54: Today's generation (0.1kWh)
    float energyTotal;         // Reg 55-56: Total generation (0.1kWh)
    float totalWorkTime;       // Reg 57-58: Total work time (0.5s)
    float pv1EnergyToday;      // Reg 59-60: PV1 energy today (0.1kWh)
    float pv1EnergyTotal;      // Reg 61-62: PV1 energy total (0.1kWh)
    float pv2EnergyToday;      // Reg 63-64: PV2 energy today (0.1kWh) *crosses block boundary*
    float pv2EnergyTotal;      // Reg 65-66: PV2 energy total (0.1kWh)
    float pv3EnergyToday;      // Reg 67-68: PV3 energy today (0.1kWh)
    float pv3EnergyTotal;      // Reg 69-70: PV3 energy total (0.1kWh)

    // --- Registers 91-92: Total PV Energy ---
    float pvEnergyTotal;       // Reg 91-92: Total PV energy all strings (0.1kWh)

    // --- Registers 93-99: Temperatures & Bus Voltages ---
    float tempInverter;        // Reg 93:    Inverter temperature (0.1C)
    float tempIPM;             // Reg 94:    IPM temperature (0.1C)
    float tempBoost;           // Reg 95:    Boost temperature (0.1C)
    float temp4;               // Reg 96:    Temperature sensor 4 (0.1C)
    float batVoltDSP;          // Reg 97:    Battery/DSP voltage (0.1V)
    float pBusVoltage;         // Reg 98:    Positive DC bus voltage (0.1V)
    float nBusVoltage;         // Reg 99:    Negative DC bus voltage (0.1V)

    // --- Registers 100-112: Diagnostics ---
    int   ipf;                 // Reg 100:   Inverter power factor
    int   realOutputPercent;   // Reg 101:   Real output power percent (1%)
    float opFullPower;         // Reg 102-103: Max power limit (0.1W)
    int   deratingMode;        // Reg 104:   Derating mode
    int   faultMaincode;       // Reg 105:   Fault main code
    int   faultSubcode;        // Reg 107:   Fault sub code
    int   remoteCtrlEn;        // Reg 108:   Remote control enable
    int   remoteCtrlPower;     // Reg 109:   Remote control power
    int   warningBitH;         // Reg 110:   Warning bit high
    int   warnSubcode;         // Reg 111:   Warning sub code
    int   warnMaincode;        // Reg 112:   Warning main code

    // --- Second Group: Registers 125+ ---
    float pidPV1Voltage;       // Reg 125:   PID PV1 voltage (0.1V)
    float pidPV1Current;       // Reg 126:   PID PV1 current (0.1mA)
    float pidPV2Voltage;       // Reg 127:   PID PV2 voltage (0.1V)
    float pidPV2Current;       // Reg 128:   PID PV2 current (0.1mA)
    float pidPV3Voltage;       // Reg 129:   PID PV3 voltage (0.1V)
    float pidPV3Current;       // Reg 130:   PID PV3 current (0.1mA)
    int   pidStatus;           // Reg 141:   PID status (0=none, 1=wait, 2=normal, 3=fault)

    int   strUnmatch;          // Reg 174:   String unmatch bits
    int   strCurrentUnbalance; // Reg 175:   String current unbalance bits
    int   strDisconnect;       // Reg 176:   String disconnect bits
    int   pidFaultCode;        // Reg 177:   PID fault code bits
    int   stringPrompt;        // Reg 178:   String prompt bits

    int   pvISO;               // Reg 200:   PV isolation resistance (kOhm)
    float dciR;                // Reg 201:   R-phase DCI (0.1mA)
    float dciS;                // Reg 202:   S-phase DCI (0.1mA)
    float dciT;                // Reg 203:   T-phase DCI (0.1mA)
    int   gfci;                // Reg 205:   GFCI current (mA)

    int   fanFaultBit;         // Reg 229:   Fan fault bits (bit0-3 = fan1-4)
    float apparentPower;       // Reg 230-231: Output apparent power (0.1VA)
    float reactivePower;       // Reg 232-233: Output reactive power (0.1Var)
    float reactivePowerMax;    // Reg 234-235: Nominal reactive power (0.1Var)
    float reactivePowerTotal;  // Reg 236-237: Total reactive energy (0.1VarH)

} inverterData;

// ========== MODBUS READ HELPERS ==========
float read32(uint8_t highReg, uint8_t lowReg, float scale) {
    uint32_t val = ((uint32_t)modbus.getResponseBuffer(highReg) << 16) |
                   modbus.getResponseBuffer(lowReg);
    return val * scale;
}

float read16(uint8_t reg, float scale) {
    return modbus.getResponseBuffer(reg) * scale;
}

int read16i(uint8_t reg) {
    return modbus.getResponseBuffer(reg);
}

bool readBlock(uint16_t startReg, uint16_t count) {
    uint8_t result = modbus.readInputRegisters(startReg, count);
    modbusReadCount++;
    if (result != modbus.ku8MBSuccess) {
        modbusErrorCount++;
        return false;
    }
    return true;
}

// ========== MODBUS DATA READING ==========
// Reads input registers in 4 blocks with 50ms settling between each.
// Block boundary note: Register 63 (pv2EnergyToday high word) is in block 1,
// register 64 (low word) is in block 2. We save reg 63 before reading block 2.

bool readInverterData() {
    digitalWrite(STATUS_LED, LOW);

    // Block 1: Registers 0-63 (64 registers)
    if (!readBlock(0, 64)) { digitalWrite(STATUS_LED, HIGH); return false; }

    inverterData.status          = read16i(0);
    inverterData.solarPowerTotal = read32(1, 2, 0.1);
    inverterData.pv1Voltage      = read16(3, 0.1);
    inverterData.pv1Current      = read16(4, 0.1);
    inverterData.pv1Power        = read32(5, 6, 0.1);
    inverterData.pv2Voltage      = read16(7, 0.1);
    inverterData.pv2Current      = read16(8, 0.1);
    inverterData.pv2Power        = read32(9, 10, 0.1);
    inverterData.pv3Voltage      = read16(11, 0.1);
    inverterData.pv3Current      = read16(12, 0.1);
    inverterData.pv3Power        = read32(13, 14, 0.1);

    inverterData.outputPower     = read32(35, 36, 0.1);
    inverterData.gridFrequency   = read16(37, 0.01);
    inverterData.gridVoltageL1   = read16(38, 0.1);
    inverterData.gridCurrentL1   = read16(39, 0.1);
    inverterData.gridPowerL1     = read32(40, 41, 0.1);
    inverterData.gridVoltageL2   = read16(42, 0.1);
    inverterData.gridCurrentL2   = read16(43, 0.1);
    inverterData.gridPowerL2     = read32(44, 45, 0.1);
    inverterData.gridVoltageL3   = read16(46, 0.1);
    inverterData.gridCurrentL3   = read16(47, 0.1);
    inverterData.gridPowerL3     = read32(48, 49, 0.1);
    inverterData.gridVoltageRS   = read16(50, 0.1);
    inverterData.gridVoltageST   = read16(51, 0.1);
    inverterData.gridVoltageTR   = read16(52, 0.1);

    inverterData.energyToday     = read32(53, 54, 0.1);
    inverterData.energyTotal     = read32(55, 56, 0.1);
    inverterData.totalWorkTime   = read32(57, 58, 0.5);
    inverterData.pv1EnergyToday  = read32(59, 60, 0.1);
    inverterData.pv1EnergyTotal  = read32(61, 62, 0.1);

    // Save register 63 (pv2EnergyToday high word) before block 2 overwrites buffer
    uint16_t pv2EnergyTodayH = modbus.getResponseBuffer(63);

    delay(50);

    // Block 2: Registers 64-124 (61 registers)
    // Buffer index: 0=reg64, 1=reg65, ... 27=reg91, 29=reg93, etc.
    if (!readBlock(64, 61)) { digitalWrite(STATUS_LED, HIGH); return false; }

    // Combine saved reg 63 (high) with reg 64 (buffer index 0, low)
    inverterData.pv2EnergyToday  = ((uint32_t)pv2EnergyTodayH << 16 | modbus.getResponseBuffer(0)) * 0.1;
    inverterData.pv2EnergyTotal  = read32(1, 2, 0.1);     // reg 65-66
    inverterData.pv3EnergyToday  = read32(3, 4, 0.1);     // reg 67-68
    inverterData.pv3EnergyTotal  = read32(5, 6, 0.1);     // reg 69-70

    inverterData.pvEnergyTotal   = read32(27, 28, 0.1);   // reg 91-92

    inverterData.tempInverter    = read16(29, 0.1);        // reg 93
    inverterData.tempIPM         = read16(30, 0.1);        // reg 94
    inverterData.tempBoost       = read16(31, 0.1);        // reg 95
    inverterData.temp4           = read16(32, 0.1);        // reg 96
    inverterData.batVoltDSP      = read16(33, 0.1);        // reg 97
    inverterData.pBusVoltage     = read16(34, 0.1);        // reg 98
    inverterData.nBusVoltage     = read16(35, 0.1);        // reg 99

    inverterData.ipf             = read16i(36);            // reg 100
    inverterData.realOutputPercent = read16i(37);          // reg 101
    inverterData.opFullPower     = read32(38, 39, 0.1);   // reg 102-103
    inverterData.deratingMode    = read16i(40);            // reg 104
    inverterData.faultMaincode   = read16i(41);            // reg 105
    inverterData.faultSubcode    = read16i(43);            // reg 107
    inverterData.remoteCtrlEn    = read16i(44);            // reg 108
    inverterData.remoteCtrlPower = read16i(45);            // reg 109
    inverterData.warningBitH     = read16i(46);            // reg 110
    inverterData.warnSubcode     = read16i(47);            // reg 111
    inverterData.warnMaincode    = read16i(48);            // reg 112

    delay(50);

    // Block 3: Registers 125-189 (65 registers)
    // Buffer index: 0=reg125, 16=reg141, 49=reg174, etc.
    if (!readBlock(125, 65)) { digitalWrite(STATUS_LED, HIGH); return false; }

    inverterData.pidPV1Voltage   = read16(0, 0.1);        // reg 125
    inverterData.pidPV1Current   = read16(1, 0.1);        // reg 126
    inverterData.pidPV2Voltage   = read16(2, 0.1);        // reg 127
    inverterData.pidPV2Current   = read16(3, 0.1);        // reg 128
    inverterData.pidPV3Voltage   = read16(4, 0.1);        // reg 129
    inverterData.pidPV3Current   = read16(5, 0.1);        // reg 130

    inverterData.pidStatus       = read16i(16);            // reg 141

    inverterData.strUnmatch          = read16i(49);        // reg 174
    inverterData.strCurrentUnbalance = read16i(50);        // reg 175
    inverterData.strDisconnect       = read16i(51);        // reg 176
    inverterData.pidFaultCode        = read16i(52);        // reg 177
    inverterData.stringPrompt        = read16i(53);        // reg 178

    delay(50);

    // Block 4: Registers 190-237 (48 registers)
    // Buffer index: 10=reg200, 11=reg201, 39=reg229, 40=reg230, etc.
    if (!readBlock(190, 48)) { digitalWrite(STATUS_LED, HIGH); return false; }

    inverterData.pvISO           = read16i(10);            // reg 200
    inverterData.dciR            = read16(11, 0.1);        // reg 201
    inverterData.dciS            = read16(12, 0.1);        // reg 202
    inverterData.dciT            = read16(13, 0.1);        // reg 203
    inverterData.gfci            = read16i(15);            // reg 205

    inverterData.fanFaultBit     = read16i(39);            // reg 229
    inverterData.apparentPower   = read32(40, 41, 0.1);   // reg 230-231
    inverterData.reactivePower   = read32(42, 43, 0.1);   // reg 232-233
    inverterData.reactivePowerMax   = read32(44, 45, 0.1); // reg 234-235
    inverterData.reactivePowerTotal = read32(46, 47, 0.1); // reg 236-237

    digitalWrite(STATUS_LED, HIGH);
    return true;
}

// ========== MQTT PUBLISHING ==========

void publishData() {
    JsonDocument doc;

    doc["status"] = inverterData.status;
    doc["solarpower"] = inverterData.solarPowerTotal;

    JsonObject pv1 = doc["pv1"].to<JsonObject>();
    pv1["voltage"] = inverterData.pv1Voltage;
    pv1["current"] = inverterData.pv1Current;
    pv1["power"]   = inverterData.pv1Power;

    JsonObject pv2 = doc["pv2"].to<JsonObject>();
    pv2["voltage"] = inverterData.pv2Voltage;
    pv2["current"] = inverterData.pv2Current;
    pv2["power"]   = inverterData.pv2Power;

    JsonObject pv3 = doc["pv3"].to<JsonObject>();
    pv3["voltage"] = inverterData.pv3Voltage;
    pv3["current"] = inverterData.pv3Current;
    pv3["power"]   = inverterData.pv3Power;

    JsonObject grid = doc["grid"].to<JsonObject>();
    grid["outputpower"] = inverterData.outputPower;
    grid["frequency"]   = inverterData.gridFrequency;

    JsonObject l1 = grid["l1"].to<JsonObject>();
    l1["voltage"] = inverterData.gridVoltageL1;
    l1["current"] = inverterData.gridCurrentL1;
    l1["power"]   = inverterData.gridPowerL1;

    JsonObject l2 = grid["l2"].to<JsonObject>();
    l2["voltage"] = inverterData.gridVoltageL2;
    l2["current"] = inverterData.gridCurrentL2;
    l2["power"]   = inverterData.gridPowerL2;

    JsonObject l3 = grid["l3"].to<JsonObject>();
    l3["voltage"] = inverterData.gridVoltageL3;
    l3["current"] = inverterData.gridCurrentL3;
    l3["power"]   = inverterData.gridPowerL3;

    grid["voltage_rs"]           = inverterData.gridVoltageRS;
    grid["voltage_st"]           = inverterData.gridVoltageST;
    grid["voltage_tr"]           = inverterData.gridVoltageTR;
    grid["apparent_power"]       = inverterData.apparentPower;
    grid["reactive_power"]       = inverterData.reactivePower;
    grid["reactive_power_max"]   = inverterData.reactivePowerMax;
    grid["reactive_power_total"] = inverterData.reactivePowerTotal;

    JsonObject energy = doc["energy"].to<JsonObject>();
    energy["today"]     = inverterData.energyToday;
    energy["total"]     = inverterData.energyTotal;
    energy["pv_total"]  = inverterData.pvEnergyTotal;
    energy["worktime"]  = inverterData.totalWorkTime;
    energy["pv1_today"] = inverterData.pv1EnergyToday;
    energy["pv1_total"] = inverterData.pv1EnergyTotal;
    energy["pv2_today"] = inverterData.pv2EnergyToday;
    energy["pv2_total"] = inverterData.pv2EnergyTotal;
    energy["pv3_today"] = inverterData.pv3EnergyToday;
    energy["pv3_total"] = inverterData.pv3EnergyTotal;

    char buffer[2048];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    mqtt.publish(topicData, buffer, len);
}

void publishDiagnostics() {
    JsonDocument doc;

    JsonObject temp = doc["temperature"].to<JsonObject>();
    temp["inverter"] = inverterData.tempInverter;
    temp["ipm"]      = inverterData.tempIPM;
    temp["boost"]    = inverterData.tempBoost;
    temp["temp4"]    = inverterData.temp4;

    JsonObject bus = doc["bus"].to<JsonObject>();
    bus["p_voltage"]    = inverterData.pBusVoltage;
    bus["n_voltage"]    = inverterData.nBusVoltage;
    bus["bat_volt_dsp"] = inverterData.batVoltDSP;

    doc["ipf"]              = inverterData.ipf;
    doc["output_percent"]   = inverterData.realOutputPercent;
    doc["max_power_limit"]  = inverterData.opFullPower;
    doc["derating_mode"]    = inverterData.deratingMode;

    JsonObject fault = doc["fault"].to<JsonObject>();
    fault["maincode"] = inverterData.faultMaincode;
    fault["subcode"]  = inverterData.faultSubcode;

    JsonObject warn = doc["warning"].to<JsonObject>();
    warn["bit_h"]    = inverterData.warningBitH;
    warn["subcode"]  = inverterData.warnSubcode;
    warn["maincode"] = inverterData.warnMaincode;

    doc["remote_ctrl_en"]    = inverterData.remoteCtrlEn;
    doc["remote_ctrl_power"] = inverterData.remoteCtrlPower;

    JsonObject safety = doc["safety"].to<JsonObject>();
    safety["pv_iso"] = inverterData.pvISO;
    safety["gfci"]   = inverterData.gfci;
    safety["dci_r"]  = inverterData.dciR;
    safety["dci_s"]  = inverterData.dciS;
    safety["dci_t"]  = inverterData.dciT;

    JsonObject pid = doc["pid"].to<JsonObject>();
    pid["status"]      = inverterData.pidStatus;
    pid["pv1_voltage"] = inverterData.pidPV1Voltage;
    pid["pv1_current"] = inverterData.pidPV1Current;
    pid["pv2_voltage"] = inverterData.pidPV2Voltage;
    pid["pv2_current"] = inverterData.pidPV2Current;
    pid["pv3_voltage"] = inverterData.pidPV3Voltage;
    pid["pv3_current"] = inverterData.pidPV3Current;
    pid["fault_code"]  = inverterData.pidFaultCode;

    JsonObject strings = doc["strings"].to<JsonObject>();
    strings["unmatch"]    = inverterData.strUnmatch;
    strings["unbalance"]  = inverterData.strCurrentUnbalance;
    strings["disconnect"] = inverterData.strDisconnect;
    strings["prompt"]     = inverterData.stringPrompt;

    doc["fan_fault"] = inverterData.fanFaultBit;

    char buffer[1024];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    mqtt.publish(topicDiag, buffer, len);
}

void publishSystemStatus() {
    uint32_t heapMin, heapMax, cycleMin, cycleMax;
    perf.getMinMax(perf.heapSamples, perf.heapCount, heapMin, heapMax);
    perf.getMinMax(perf.cycleSamples, perf.cycleCount, cycleMin, cycleMax);

    JsonDocument doc;

    doc["rssi"]    = WiFi.RSSI();
    doc["uptime"]  = uptimeSeconds;
    doc["ssid"]    = WiFi.SSID();
    doc["ip"]      = WiFi.localIP().toString();
    doc["hostname"] = deviceHostname;
    doc["version"] = buildversion;

    JsonObject mb = doc["modbus"].to<JsonObject>();
    mb["reads"]  = modbusReadCount;
    mb["errors"] = modbusErrorCount;
    float errorRate = (modbusReadCount > 0)
        ? (float)modbusErrorCount / modbusReadCount * 100.0 : 0.0;
    mb["error_rate"] = serialized(String(errorRate, 1));

    JsonObject heap = doc["heap"].to<JsonObject>();
    heap["current"]  = ESP.getFreeHeap();
    heap["min"]      = heapMin;
    heap["max"]      = heapMax;
    heap["avg"]      = perf.avg(perf.heapSamples, perf.heapCount);
    heap["min_ever"] = ESP.getMinFreeHeap();

    JsonObject cycle = doc["cycle_ms"].to<JsonObject>();
    cycle["min"] = cycleMin;
    cycle["max"] = cycleMax;
    cycle["avg"] = perf.avg(perf.cycleSamples, perf.cycleCount);
    if (perf.cycleCount > 0)
        cycle["last"] = perf.cycleSamples[(perf.cycleIndex > 0 ? perf.cycleIndex - 1 : perf.cycleCount - 1)];
    else
        cycle["last"] = 0;

    char buffer[768];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    mqtt.publish(topicStatus, buffer, len);
}

// ========== TIMER / WIFI / MQTT ==========
void onTimer() { seconds = seconds + 1; }

void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("[WiFi] Connected to AP");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[WiFi] Disconnected - reconnecting...");
            WiFi.reconnect();
            break;
        default: break;
    }
}

bool connectMQTT() {
    if (mqtt.connected()) return true;
    unsigned long now = millis();
    if (now - lastMqttAttempt < MQTT_RECONNECT_INTERVAL) return false;
    lastMqttAttempt = now;

    Serial.print("[MQTT] Connecting...");
    byte mac[6];
    WiFi.macAddress(mac);
    char uid[64];
    snprintf(uid, sizeof(uid), "%s-%02x%02x%02x", clientID, mac[2], mac[1], mac[0]);

    if (mqtt.connect(uid, mqtt_user, mqtt_password, topicConnection, 1, true, "offline")) {
        Serial.println(" connected");
        mqtt.publish(topicConnection, "online", true);
        return true;
    }
    Serial.printf(" failed (rc=%d)\n", mqtt.state());
    return false;
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    payload[length] = '\0';
    Serial.printf("[MQTT] %s = %s\n", topic, (char*)payload);
}

void setupOTA() {
    ArduinoOTA.setHostname(deviceHostname);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setPassword(ota_password);

    ArduinoOTA.onStart([]() {
        timerTick.detach();
        Serial.println("[OTA] Update starting...");
    });
    ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Update complete"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]\n", error);
        timerTick.attach(1.0, onTimer);
    });

    ArduinoOTA.begin();
    Serial.printf("[OTA] Ready - hostname: %s\n", deviceHostname);
}

// ========== SETUP ==========
void setup() {
    Serial.begin(SERIAL_RATE);
    delay(500);
    Serial.println("\n=== GROWATT MID 15KTL3-XL Gateway (ESP32-C6) ===");
    Serial.printf("Firmware: %s\n", buildversion);

    byte mac[6];
    WiFi.macAddress(mac);
    snprintf(deviceHostname, sizeof(deviceHostname), "growatt-%02x%02x%02x", mac[2], mac[1], mac[0]);

    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH);

    snprintf(topicData, sizeof(topicData), "%s/data", topicRoot);
    snprintf(topicDiag, sizeof(topicDiag), "%s/diagnostics", topicRoot);
    snprintf(topicStatus, sizeof(topicStatus), "%s/status", topicRoot);
    snprintf(topicConnection, sizeof(topicConnection), "%s/connection", topicRoot);

    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(deviceHostname);
    WiFi.begin(ssid, password);

    Serial.print("[WiFi] Connecting");
    int wifiTimeout = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print("."); wifiTimeout++;
        if (wifiTimeout > 60) { Serial.println("\n[WiFi] Timeout"); ESP.restart(); }
    }
    Serial.printf("\n[WiFi] Connected | IP: %s | RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    if (MDNS.begin(deviceHostname))
        Serial.printf("[mDNS] Responder started: %s.local\n", deviceHostname);

    RS485Serial.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    modbus.begin(MODBUS_SLAVE_ID, RS485Serial);
    Serial.println("[Modbus] Hardware UART1 initialized (TTL485-V2.0 auto-direction)");

    mqtt.setServer(mqtt_server, mqtt_port);
    mqtt.setBufferSize(2048);
    mqtt.setCallback(onMqttMessage);
    Serial.printf("[MQTT] Server: %s:%d\n", mqtt_server, mqtt_port);

    setupOTA();
    perf.init();
    timerTick.attach(1.0, onTimer);

    Serial.println("=== Initialization Complete ===");
    Serial.printf("[Config] Modbus poll: %ds | Status: %ds\n", UPDATE_INTERVAL, STATUS_INTERVAL);
    Serial.printf("[Memory] Free heap: %d bytes\n", ESP.getFreeHeap());
}

// ========== MAIN LOOP ==========
void loop() {
    ArduinoOTA.handle();

    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();

    unsigned long now = millis();
    if (now - lastUptimeTick >= 1000) {
        lastUptimeTick = now;
        uptimeSeconds++;
    }

    static unsigned long lastSeconds = 0;
    if (seconds != lastSeconds) {
        lastSeconds = seconds;

        perf.addHeapSample(ESP.getFreeHeap());

        if (seconds % UPDATE_INTERVAL == 0) {
            if (mqtt.connected() && WiFi.status() == WL_CONNECTED) {
                unsigned long cycleStart = millis();
                if (readInverterData()) {
                    publishData();
                    publishDiagnostics();
                    unsigned long cycleTime = millis() - cycleStart;
                    perf.addCycleSample(cycleTime);
                    Serial.printf("[Data] Published in %lums | Power: %.1fW | Today: %.1fkWh\n",
                                  cycleTime, inverterData.outputPower, inverterData.energyToday);
                }
            }
        }

        if (seconds % STATUS_INTERVAL == 0) {
            if (mqtt.connected()) publishSystemStatus();
        }
    }
}
