/*
 * Step3_ServoFull — PowerActor2 nach dem Umbau, Schritt 3: Servo ueber den
 *                   vollen Bereich (+/-180 Grad, keine Anschlaege) und
 *                   "so schnell wie moeglich" an den linken/rechten Rand
 *
 * Gegenstueck: ../Step3_Display (CYD28 mit Drehknopf).
 * Basis: Step2_RelayServo, Relais-Bedienung entfernt (vorerst nicht noetig) -
 * die Relais-Pins werden beim Start nur sicher AUS gelegt.
 *
 * Servo wie Step1_ServoSweep:
 *   TX (GPIO19) --[1kΩ]--+-- SERVO BUS DATA
 *   RX (GPIO18) ----------+
 *   Feetech ST3020, 1 Mbit/s, Lib FeetechBusServos, Positionen 0..4095
 *   (= -180..+180 Grad um 2048; die alten Anschlaege 760/3400 gelten nicht).
 *   Links = 0, rechts = 4095 (wie frueher leftStopp < rightStopp).
 *   Schnellfahrt: Geschwindigkeit 0 und Beschleunigung 0 = Servo-Maximum.
 *
 * Protokoll (eigenes Test-Protokoll, NICHT xCom - kommt beim Bus-Deploy):
 *   Display -> PA2 : Step2Cmd an UDP 4310 (Unicast an 192.168.0.181)
 *   PA2 -> Display : Step2State an die Absender-IP, Port 4311, als Antwort
 *                    auf JEDES Kommando (auch Poll), so sieht das Display
 *                    laufend die Ist-Position.
 *   Eigenes Magic 0x53: ein altes Step2-Display wird ignoriert.
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
#define SERVO_MIN      0
#define SERVO_MAX      4095
#define SERVO_CENTER   2048
#define SERVO_SPEED    800      // Drehknopf
#define SERVO_FAST     0        // 0 = Maximum (STS)

FeetechBus    bus(Serial1);
FeetechST3020 servo(bus, SERVO_ID);
static bool     servoOk   = false;
static uint16_t servoGoal = SERVO_CENTER;

// ── Relais (nur sicher AUS, nicht bedienbar) ─────────────────────────────────

static const uint8_t RELAY_PINS[4] = {27, 25, 26, 23};
#define RELAY_OFF  LOW

// ── Protokoll (identisch in Step3_Display) ───────────────────────────────────

#define STEP2_CMD_PORT    4310
#define STEP2_STATE_PORT  4311
#define STEP2_MAGIC       0x53

enum : uint8_t { S2_POLL = 0, S2_SERVO = 1, S2_FAST = 3 };

struct __attribute__((packed)) Step2Cmd {
    uint8_t  magic;
    uint8_t  type;       // S2_POLL / S2_SERVO / S2_FAST
    uint16_t value;      // SERVO/FAST: Zielposition
};

struct __attribute__((packed)) Step2State {
    uint8_t  magic;
    uint8_t  relays;     // immer 0 (Relais in Schritt 3 nicht bedienbar)
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

// ── Servo ────────────────────────────────────────────────────────────────────

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
    // Winkelgrenzen im Servo-EEPROM auf den vollen Bereich (nur wenn noetig,
    // das EEPROM soll nicht bei jedem Start beschrieben werden)
    uint16_t lo = 0, hi = 0;
    if (servo.readAngleLimits(lo, hi) && (lo != SERVO_MIN || hi != SERVO_MAX)) {
        logf("Servo-Winkelgrenzen %u..%u -> %u..%u (EEPROM)\n", lo, hi, SERVO_MIN, SERVO_MAX);
        servo.setAngleLimits(SERVO_MIN, SERVO_MAX);
    }
    servo.setAcceleration(0);                // 0 = ohne Rampe, volle Beschleunigung
    servo.moveTime(servoGoal, 1500, SERVO_SPEED);
    logf("Servo ID %u ok, faehrt auf %u\n", SERVO_ID, servoGoal);
}

static void servoGoTo(uint16_t pos, uint16_t speed) {
    servoGoal = constrain(pos, SERVO_MIN, SERVO_MAX);
    if (servoOk) servo.moveTime(servoGoal, 0, speed);
}

// ── Kommandos ────────────────────────────────────────────────────────────────

static void sendState(IPAddress to) {
    Step2State s;
    s.magic     = STEP2_MAGIC;
    s.relays    = 0;
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
            servoGoTo(c.value, SERVO_SPEED);
            break;
        case S2_FAST:
            servoGoTo(c.value, SERVO_FAST);
            logf("Schnellfahrt nach %u (%s)\n", servoGoal,
                 servoGoal <= SERVO_CENTER ? "links" : "rechts");
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
    logf("\nPowerActor2 Step3_ServoFull\n");
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
        logf("PowerActor2 Step3_ServoFull - Telnet verbunden, Servo %s\n",
             servoOk ? "ok" : "FEHLT");
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
