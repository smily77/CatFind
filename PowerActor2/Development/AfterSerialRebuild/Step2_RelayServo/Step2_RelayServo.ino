/*
 * Step2_RelayServo — PowerActor2 nach dem Umbau, Schritt 2: Servo auf Position
 *                    + 4 Relais einzeln schalten, ferngesteuert vom Display
 *
 * Gegenstueck: ../Step2_Display (CYD28 mit Drehknopf).
 *
 * Servo wie Step1_ServoSweep:
 *   TX (GPIO19) --[1kΩ]--+-- SERVO BUS DATA
 *   RX (GPIO18) ----------+
 *   Feetech ST3020, 1 Mbit/s, Lib FeetechBusServos, Positionen 760..3400
 *   (alte Anschlaege leftStopp/rightStopp)
 * Relais: RELAY_PINS (alte PA2-Belegung valve/laserPwr/LD06Pwr/hlkPwr),
 *   beim Start alle AUS.
 *
 * Protokoll (eigenes Test-Protokoll, NICHT xCom - kommt beim Bus-Deploy):
 *   Display -> PA2 : Step2Cmd an UDP 4310 (Unicast an 192.168.0.181)
 *   PA2 -> Display : Step2State an die Absender-IP, Port 4311, als Antwort
 *                    auf JEDES Kommando (auch Poll), so sieht das Display
 *                    laufend die Ist-Position.
 *
 * Ausgabe: Serial (115200) UND Telnet Port 23.
 * Board  : ESP32 classic, FQBN esp32:esp32:esp32
 * OTA    : arduino-cli upload --fqbn esp32:esp32:esp32 --port 192.168.0.181 --upload-field password= .
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Credentials.h>   // ssid, password
#include <FeetechBus.h>
#include <FeetechST3020.h>

// ── Servo-Bus ────────────────────────────────────────────────────────────────

#define SERVO_RX_PIN   18
#define SERVO_TX_PIN   19
#define SERVO_BAUD     1000000
#define SERVO_ID       1
#define SERVO_MIN      760
#define SERVO_MAX      3400
#define SERVO_CENTER   2048
#define SERVO_SPEED    800

FeetechBus    bus(Serial1);
FeetechST3020 servo(bus, SERVO_ID);
static bool     servoOk   = false;
static uint16_t servoGoal = SERVO_CENTER;

// ── Relais ───────────────────────────────────────────────────────────────────

static const uint8_t RELAY_PINS[4] = {27, 25, 26, 23};
#define RELAY_ON   HIGH
#define RELAY_OFF  LOW
static uint8_t relayMask = 0;              // Bit i = Relais i+1 ein

// ── Protokoll (identisch in Step2_Display) ───────────────────────────────────

#define STEP2_CMD_PORT    4310
#define STEP2_STATE_PORT  4311
#define STEP2_MAGIC       0x52

enum : uint8_t { S2_POLL = 0, S2_SERVO = 1, S2_RELAY = 2 };

struct __attribute__((packed)) Step2Cmd {
    uint8_t  magic;
    uint8_t  type;       // S2_POLL / S2_SERVO / S2_RELAY
    uint16_t value;      // SERVO: Zielposition; RELAY: (Index 0..3 << 1) | ein
};

struct __attribute__((packed)) Step2State {
    uint8_t  magic;
    uint8_t  relays;     // Bit i = Relais i+1 ein
    uint16_t servoGoal;
    uint16_t servoPos;   // Ist-Position laut Servo
    uint8_t  servoOk;    // 0 = Servo antwortet nicht
    uint8_t  tempC;
};

static WiFiUDP udp;

// ── Ausgabe auf Serial + Telnet ──────────────────────────────────────────────

static WiFiServer telnetServer(23);
static WiFiClient telnet;

static void logf(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    if (telnet && telnet.connected()) telnet.print(buf);
}

// ── WLAN / OTA ───────────────────────────────────────────────────────────────

#define HOST_NAME   "PowerActor2"
#define LAST_OCTET  181

static void setUpWifi() {
    WiFi.mode(WIFI_STA);
    IPAddress localIP(192, 168, 0, LAST_OCTET);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(192, 168, 0, 1);
    WiFi.config(localIP, gateway, subnet, dns);
    WiFi.setHostname(HOST_NAME);
    WiFi.begin(ssid, password);
    logf("Verbinde mit WLAN \"%s\"", ssid);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 30000) {
            logf("\nWLAN fehlgeschlagen - Neustart\n");
            delay(1000);
            ESP.restart();
        }
        logf(".");
        delay(250);
    }
    logf("\nWLAN ok, IP %s, RSSI %d dBm\n",
         WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

static void setUpOTA() {
    ArduinoOTA.setHostname(HOST_NAME);
    ArduinoOTA
        .onStart([]() {
            for (uint8_t p : RELAY_PINS) digitalWrite(p, RELAY_OFF);   // sicher aus
            if (servoOk) servo.setTorque(false);
            logf("OTA Start\n");
        })
        .onEnd([]() { logf("\nOTA fertig\n"); })
        .onProgress([](unsigned int p, unsigned int t) {
            Serial.printf("OTA %u%%\r", p * 100 / t);
        })
        .onError([](ota_error_t e) { logf("OTA Fehler %u\n", e); });
    ArduinoOTA.begin();
}

// ── Servo / Relais ───────────────────────────────────────────────────────────

static void initServo() {
    bus.beginPins(SERVO_BAUD, SERVO_RX_PIN, SERVO_TX_PIN);
    delay(50);
    servoOk = servo.ping();
    if (!servoOk) {
        logf("Servo ID %u antwortet NICHT\n", SERVO_ID);
        return;
    }
    servo.setMode(ServoMode::POSITION);
    FeetechST3020::Profile p;
    p.statusReturnLevel = 1;
    p.returnDelayUnits  = 0;
    p.torqueOnAfterInit = true;
    servo.init(p);
    servo.moveTime(servoGoal, 1500, SERVO_SPEED);
    logf("Servo ID %u ok, faehrt auf %u\n", SERVO_ID, servoGoal);
}

static void servoGoTo(uint16_t pos) {
    servoGoal = constrain(pos, SERVO_MIN, SERVO_MAX);
    if (servoOk) servo.moveTime(servoGoal, 0, SERVO_SPEED);
}

static void setRelay(uint8_t idx, bool on) {
    if (idx > 3) return;
    digitalWrite(RELAY_PINS[idx], on ? RELAY_ON : RELAY_OFF);
    if (on) relayMask |= (1 << idx);
    else    relayMask &= ~(1 << idx);
    logf("Relais %u (GPIO%u) %s\n", idx + 1, RELAY_PINS[idx], on ? "EIN" : "AUS");
}

// ── Kommandos ────────────────────────────────────────────────────────────────

static void sendState(IPAddress to) {
    Step2State s;
    s.magic     = STEP2_MAGIC;
    s.relays    = relayMask;
    s.servoGoal = servoGoal;
    uint16_t pos = 0;
    uint8_t  t   = 0;
    s.servoOk   = servoOk && servo.readPresentPosition(pos);
    if (s.servoOk) servo.readCurrentTemperature(t);
    s.servoPos  = pos;
    s.tempC     = t;
    udp.beginPacket(to, STEP2_STATE_PORT);
    udp.write((const uint8_t *)&s, sizeof(s));
    udp.endPacket();
}

static void handleUdp() {
    int len = udp.parsePacket();
    if (len <= 0) return;
    Step2Cmd c;
    if (len != sizeof(c)) { udp.flush(); return; }
    udp.read((uint8_t *)&c, sizeof(c));
    if (c.magic != STEP2_MAGIC) return;
    switch (c.type) {
        case S2_SERVO:
            servoGoTo(c.value);
            break;
        case S2_RELAY:
            setRelay(c.value >> 1, c.value & 1);
            break;
        default:
            break;                           // S2_POLL: nur Zustand melden
    }
    sendState(udp.remoteIP());
}

// ── Arduino ──────────────────────────────────────────────────────────────────

void setup() {
    for (uint8_t p : RELAY_PINS) {           // zuerst: alle Relais sicher aus
        digitalWrite(p, RELAY_OFF);
        pinMode(p, OUTPUT);
    }
    Serial.begin(115200);
    delay(500);
    logf("\nPowerActor2 Step2_RelayServo\n");
    setUpWifi();
    setUpOTA();
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    udp.begin(STEP2_CMD_PORT);
    initServo();
}

void loop() {
    ArduinoOTA.handle();
    handleUdp();

    static uint32_t tRetry = 0;
    static uint16_t lastLogged = 0;
    static uint32_t tLog = 0;

    if (telnetServer.hasClient()) {
        if (telnet) telnet.stop();
        telnet = telnetServer.available();
        logf("PowerActor2 Step2_RelayServo - Telnet verbunden, Servo %s, Relais 0x%X\n",
             servoOk ? "ok" : "FEHLT", relayMask);
    }
    if (!servoOk && millis() > tRetry) {     // Servo nachtraeglich angesteckt?
        initServo();
        tRetry = millis() + 5000;
    }
    if (servoOk && servoGoal != lastLogged && millis() > tLog) {
        logf("Servo Soll %u\n", servoGoal);
        lastLogged = servoGoal;
        tLog = millis() + 300;
    }
}
