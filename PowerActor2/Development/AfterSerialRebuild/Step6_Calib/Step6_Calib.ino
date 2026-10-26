/*
 * Step6_Calib — PowerActor2 nach dem Umbau, Schritt 6: Winkel-Kalibrierung
 *               von LD06 und Compact Dome gegen Servo + LP40B
 *
 * Gegenstueck: ../Step6_CalibTool/pa2_calib.py (PC, Weboberflaeche).
 * Basis: Step5_RelaysIR (Servo-, Lidar- und Relais-Teil), IR-Kamera und
 * Display-Protokoll entfallen.
 *
 * Ablauf: Der PC schaltet die Speisung von LD06 und Compact Dome ein und
 * laesst den Servo zwischen zwei Positionen pendeln (Sweep). Jede LP40B-
 * Messung geht zusammen mit der dabei gelesenen Servo-Position an den PC.
 * Der PC sucht darin die Person und vergleicht ihre Richtung mit LD06 und
 * Compact Dome.
 *
 * Servo wie Step1_ServoSweep:
 *   TX (GPIO19) --[1kΩ]--+-- SERVO BUS DATA
 *   RX (GPIO18) ----------+
 *   Feetech ST3020, 1 Mbit/s, Positionen 0..4095 (= -180..+180 Grad um 2048),
 *   links = 0, rechts = 4095.
 *
 * Lidar: LP40B an Serial2 RX16/TX17, 115200 Baud, Dauermessung wie Step4.
 *
 * Laser GPIO25: in diesem Schritt IMMER AUS (es laeuft eine Person vor dem
 *   Turm herum).
 *
 * Relais (aktiv HIGH): 1 = GPIO27 Ventil (hier nie schaltbar), 2 = GPIO26
 *   Speisung LD06, 3 = GPIO23 Speisung Radar (Compact Dome), 4 = GPIO32 Spot.
 *   Kommt CAL_TIMEOUT_MS kein Kommando vom PC, gehen alle Relais AUS und der
 *   Sweep stoppt.
 *
 * Protokoll (eigenes Test-Protokoll, NICHT xCom), Magic 0x56:
 *   PC -> PA2 : CalCmd an UDP 4310 (Unicast an 192.168.0.181)
 *   PA2 -> PC : an die Absender-IP des letzten Kommandos, Port 4311
 *               CalState  (kind 1) als Antwort auf jedes Kommando + alle 250 ms
 *               CalSamples(kind 2) gesammelte LP40B-Messungen, alle 100 ms
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
#define SERVO_SPEED    800      // Standard fuer C_GOTO ohne Geschwindigkeit
#define SWEEP_TURN     30       // so nah am Ende -> Richtung wechseln (Servo bleibt oft
                                // 10..20 Schritte vor dem Ziel stehen)
#define SWEEP_STALL_MS 250      // so lange keine Bewegung nahe am Ende -> wechseln
#define SWEEP_STUCK_MS 6000     // Ende nie erreicht -> trotzdem wechseln

FeetechBus    bus(Serial1);
FeetechST3020 servo(bus, SERVO_ID);
static bool     servoOk   = false;
static uint16_t servoGoal = SERVO_CENTER;
static uint16_t servoPos  = SERVO_CENTER;    // zuletzt gelesene Ist-Position
static uint8_t  servoTemp = 0;

static bool     sweeping   = false;
static uint16_t sweepLo    = 1700, sweepHi = 2400, sweepSpeed = 600;
static uint32_t sweepTurnAt = 0;              // millis() des letzten Richtungswechsels
static uint16_t stallPos = 0;                 // Position beim letzten Bewegungsnachweis
static uint32_t stallAt  = 0;

// ── Lidar / Laser ────────────────────────────────────────────────────────────

#define LIDAR_RX_PIN    16
#define LIDAR_TX_PIN    17
#define LIDAR_BAUD      115200
#define LIDAR_STALE_MS  1000     // so lange ohne Messwert -> neu einrichten
#define LIDAR_NONE      0xFF     // Fehlercode "noch keine Messung"
#define LASER_PIN       25

static uint32_t lidarMm    = 0;
static uint8_t  lidarErr   = LIDAR_NONE;  // 0 = ok, 1 schwach, 2 stark, 3 Bereich, 4 System
static uint32_t lidarAt    = 0;           // millis() der letzten Messung
static uint32_t lidarCount = 0;

// ── Relais ───────────────────────────────────────────────────────────────────

#define RELAY_COUNT     4
#define RELAY_ALLOWED   0x0E       // Bit 0 (Ventil) ist in diesem Schritt gesperrt
#define CAL_TIMEOUT_MS  3000       // so lange kein PC-Kommando -> alles AUS
static const uint8_t RELAY_PINS[RELAY_COUNT] = {27, 26, 23, 32};
static const char   *RELAY_NAMES[RELAY_COUNT] = {"Ventil", "LD06", "Radar", "Spot"};
static uint8_t  relayMask = 0;             // Bit i = Relais i+1 ein
static uint32_t lastCmdAt = 0;             // millis() des letzten PC-Kommandos

// ── Protokoll (identisch in pa2_calib.py) ────────────────────────────────────

#define CAL_CMD_PORT    4310
#define CAL_STATE_PORT  4311
#define CAL_MAGIC       0x56
#define CAL_MAX_SAMPLES 40

enum : uint8_t { C_POLL = 0, C_RELAYS = 1, C_GOTO = 2, C_SWEEP = 3, C_STOP = 4 };
enum : uint8_t { K_STATE = 1, K_SAMPLES = 2 };

struct __attribute__((packed)) CalCmd {
    uint8_t  magic;
    uint8_t  type;       // C_*
    uint16_t a;          // RELAYS: Maske | GOTO: Position | SWEEP: lo
    uint16_t b;          // GOTO: Geschwindigkeit (0 = Standard) | SWEEP: hi
    uint16_t c;          // SWEEP: Geschwindigkeit
};

struct __attribute__((packed)) CalState {
    uint8_t  magic;
    uint8_t  kind;       // K_STATE
    uint8_t  servoOk;
    uint8_t  tempC;
    uint16_t servoGoal;
    uint16_t servoPos;
    uint8_t  relays;
    uint8_t  sweeping;
    uint16_t sweepLo, sweepHi, sweepSpeed;
    uint8_t  lidarErr;   // LIDAR_NONE = keine (frische) Messung
    uint32_t lidarMm;
    uint32_t lidarCount;
    uint32_t uptimeMs;
};

struct __attribute__((packed)) CalSample {
    uint32_t tMs;        // millis() beim Empfang der Messung
    uint16_t pos;        // Servo-Ist-Position direkt danach gelesen
    uint16_t mm;         // Distanz (gekappt auf 65535)
    uint8_t  err;
};

struct __attribute__((packed)) CalSamples {
    uint8_t   magic;
    uint8_t   kind;      // K_SAMPLES
    uint8_t   count;
    CalSample s[CAL_MAX_SAMPLES];
};

static WiFiUDP    udp;
static IPAddress  pcIp;                   // Absender des letzten Kommandos
static bool       pcKnown = false;
static CalSamples batch;

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

static void allRelaysOff();

static void setUpWifi() {
    WiFi.mode(WIFI_STA);
    IPAddress localIP(192, 168, 0, LAST_OCTET);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(192, 168, 0, 1);
    WiFi.config(localIP, gateway, subnet, dns);
    WiFi.setHostname(HOST_NAME);
    WiFi.setSleep(false);                    // gleichmaessiger Datenstrom zum PC
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
            sweeping = false;
            allRelaysOff();
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

// Sweep: am jeweiligen Ende umkehren (Ist-Position kommt aus readServoPos)
static void serviceSweep() {
    if (!sweeping || !servoOk) return;
    int  toGo  = abs((int)servoPos - (int)servoGoal);
    if (abs((int)servoPos - (int)stallPos) > 3) { stallPos = servoPos; stallAt = millis(); }
    bool stalled = toGo < 120 && millis() - stallAt > SWEEP_STALL_MS;
    if (toGo <= SWEEP_TURN || stalled || millis() - sweepTurnAt > SWEEP_STUCK_MS) {
        servoGoTo(servoGoal == sweepHi ? sweepLo : sweepHi, sweepSpeed);
        sweepTurnAt = stallAt = millis();
    }
}

static void readServoPos() {
    uint16_t p;
    if (servoOk && servo.readPresentPosition(p)) servoPos = p;
}

// ── Lidar ────────────────────────────────────────────────────────────────────

static uint8_t lidarCrc(const uint8_t *p, uint8_t len) {    // CRC-8, Polynom 0x31
    uint8_t crc = 0;
    while (len--) {
        crc ^= *p++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

static void lidarSend(uint8_t key, uint8_t value) {
    uint8_t f[8] = {0x55, key, 0, 0, 0, value, 0, 0xAA};
    f[6] = lidarCrc(f + 1, 5);
    Serial2.write(f, sizeof(f));
}

static void lidarSetup() {
    lidarSend(0x06, 0);                      // eine evtl. laufende Messung stoppen
    delay(20);
    lidarSend(0x04, 0x01);                   // Byte-Format
    delay(20);
    lidarSend(0x0D, 0x02);                   // Dauermessung auf Startbefehl
    delay(20);
    lidarSend(0x05, 0);                      // Start
}

static void initLidar() {
    Serial2.begin(LIDAR_BAUD, SERIAL_8N1, LIDAR_RX_PIN, LIDAR_TX_PIN);
    lidarSetup();
    logf("Lidar eingerichtet (Serial2 RX%u/TX%u, %u Baud)\n",
         LIDAR_RX_PIN, LIDAR_TX_PIN, LIDAR_BAUD);
}

static void sendBatch();

// Messung zusammen mit der Servo-Position in den Sammelpuffer
static void noteSample() {
    readServoPos();
    serviceSweep();
    if (!pcKnown) return;
    CalSample &s = batch.s[batch.count++];
    s.tMs = lidarAt;
    s.pos = servoPos;
    s.mm  = lidarMm > 65535 ? 65535 : lidarMm;
    s.err = lidarErr;
    if (batch.count >= CAL_MAX_SAMPLES) sendBatch();
}

// Bytes einsammeln, Rahmen 55 .. AA mit gueltiger CRC auswerten
static void pollLidar() {
    static uint8_t f[8];
    static uint8_t n = 0;
    while (Serial2.available()) {
        uint8_t b = Serial2.read();
        if (n == 0 && b != 0x55) continue;   // auf Rahmenanfang warten
        f[n++] = b;
        if (n < 8) continue;
        n = 0;
        if (f[7] != 0xAA || lidarCrc(f + 1, 5) != f[6]) {
            // Versatz: ab dem naechsten 0x55 im Puffer neu aufsetzen
            for (uint8_t i = 1; i < 8; i++)
                if (f[i] == 0x55) { memmove(f, f + i, 8 - i); n = 8 - i; break; }
            continue;
        }
        if (f[1] != 0x07) continue;          // nur Messwerte
        lidarErr = f[2];
        lidarMm  = ((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 8) | f[5];
        lidarAt  = millis();
        lidarCount++;
        noteSample();
    }
}

static bool lidarFresh() {
    return lidarErr != LIDAR_NONE && millis() - lidarAt < LIDAR_STALE_MS;
}

// ── Relais ───────────────────────────────────────────────────────────────────

static void setRelays(uint8_t mask) {
    mask &= RELAY_ALLOWED;
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        bool on = mask & (1 << i);
        if (on == bool(relayMask & (1 << i))) continue;
        digitalWrite(RELAY_PINS[i], on ? HIGH : LOW);
        logf("Relais %u %s (GPIO%u) %s\n", i + 1, RELAY_NAMES[i], RELAY_PINS[i],
             on ? "EIN" : "AUS");
    }
    relayMask = mask;
}

static void allRelaysOff() {
    for (uint8_t p : RELAY_PINS) digitalWrite(p, LOW);
    relayMask = 0;
}

// ── PC-Verbindung ────────────────────────────────────────────────────────────

static void sendBatch() {
    if (batch.count && pcKnown) {
        batch.magic = CAL_MAGIC;
        batch.kind  = K_SAMPLES;
        udp.beginPacket(pcIp, CAL_STATE_PORT);
        udp.write((const uint8_t *)&batch, 3 + batch.count * sizeof(CalSample));
        udp.endPacket();
    }
    batch.count = 0;
}

static void sendState() {
    if (!pcKnown) return;
    CalState s;
    s.magic      = CAL_MAGIC;
    s.kind       = K_STATE;
    s.servoOk    = servoOk;
    s.tempC      = servoTemp;
    s.servoGoal  = servoGoal;
    s.servoPos   = servoPos;
    s.relays     = relayMask;
    s.sweeping   = sweeping;
    s.sweepLo    = sweepLo;
    s.sweepHi    = sweepHi;
    s.sweepSpeed = sweepSpeed;
    s.lidarErr   = lidarFresh() ? lidarErr : LIDAR_NONE;
    s.lidarMm    = lidarMm;
    s.lidarCount = lidarCount;
    s.uptimeMs   = millis();
    udp.beginPacket(pcIp, CAL_STATE_PORT);
    udp.write((const uint8_t *)&s, sizeof(s));
    udp.endPacket();
}

static void handleUdp() {
    int len = udp.parsePacket();
    if (len <= 0) return;
    CalCmd c;
    if (len != sizeof(c)) { udp.flush(); return; }
    udp.read((uint8_t *)&c, sizeof(c));
    if (c.magic != CAL_MAGIC) return;
    if (!pcKnown || udp.remoteIP() != pcIp)
        logf("PC %s verbunden\n", udp.remoteIP().toString().c_str());
    pcIp = udp.remoteIP();
    pcKnown = true;
    lastCmdAt = millis();
    switch (c.type) {
        case C_RELAYS:
            setRelays(c.a);
            break;
        case C_GOTO:
            sweeping = false;
            servoGoTo(c.a, c.b ? c.b : SERVO_SPEED);
            break;
        case C_SWEEP:
            sweepLo    = constrain(min(c.a, c.b), SERVO_MIN, SERVO_MAX);
            sweepHi    = constrain(max(c.a, c.b), SERVO_MIN, SERVO_MAX);
            sweepSpeed = c.c ? c.c : SERVO_SPEED;
            if (!sweeping) logf("Sweep %u..%u, Geschwindigkeit %u\n", sweepLo, sweepHi, sweepSpeed);
            // weiter in die aktuelle Richtung, aber auf die neuen Enden
            servoGoTo(servoGoal == sweepLo ? sweepLo : sweepHi, sweepSpeed);
            sweeping = true;
            sweepTurnAt = millis();
            break;
        case C_STOP:
            if (sweeping) logf("Sweep gestoppt bei %u\n", servoPos);
            sweeping = false;
            servoGoTo(servoPos, SERVO_SPEED);
            break;
        default:
            break;                           // C_POLL: nur Zustand melden
    }
    sendState();
}

// ── Arduino ──────────────────────────────────────────────────────────────────

void setup() {
    digitalWrite(LASER_PIN, LOW);            // zuerst: Laser und Relais sicher aus
    pinMode(LASER_PIN, OUTPUT);
    for (uint8_t p : RELAY_PINS) {
        digitalWrite(p, LOW);
        pinMode(p, OUTPUT);
    }
    Serial.begin(115200);
    delay(500);
    logf("\nPowerActor2 Step6_Calib\n");
    setUpWifi();
    setUpOTA();
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    udp.begin(CAL_CMD_PORT);
    initServo();
    initLidar();
}

void loop() {
    ArduinoOTA.handle();
    handleUdp();
    pollLidar();

    static uint32_t tRetry = 0, tLidarRetry = 0, tLog = 0, tBatch = 0, tState = 0, tTemp = 0;

    // PC weg -> Relais aus, Sweep stoppen
    if ((relayMask || sweeping) && millis() - lastCmdAt > CAL_TIMEOUT_MS) {
        allRelaysOff();
        if (sweeping) servoGoTo(servoPos, SERVO_SPEED);
        sweeping = false;
        logf("Kein PC-Kommando seit %u ms - Relais AUS, Sweep gestoppt\n", CAL_TIMEOUT_MS);
    }
    if (millis() - tBatch >= 100) { sendBatch(); tBatch = millis(); }
    if (millis() - tState >= 250) {
        if (!lidarFresh()) { readServoPos(); serviceSweep(); }   // Sweep auch ohne Lidar
        sendState();
        tState = millis();
    }
    if (servoOk && millis() - tTemp >= 2000) {
        servo.readCurrentTemperature(servoTemp);
        tTemp = millis();
    }
    if (telnetServer.hasClient()) {
        if (telnet) telnet.stop();
        telnet = telnetServer.available();
        logf("PowerActor2 Step6_Calib - Telnet verbunden, Servo %s, Lidar %s, Relais 0x%X\n",
             servoOk ? "ok" : "FEHLT", lidarFresh() ? "ok" : "FEHLT", relayMask);
    }
    if (!servoOk && millis() > tRetry) {     // Servo nachtraeglich angesteckt?
        initServo();
        tRetry = millis() + 5000;
    }
    if (!lidarFresh() && millis() > tLidarRetry) {   // Lidar stumm -> neu einrichten
        lidarSetup();
        tLidarRetry = millis() + 2000;
    }
    if (millis() > tLog) {                   // alle 5 s ein Lebenszeichen
        logf("Servo %u%s, Lidar %s %lu mm (%lu Messungen), Relais 0x%X, PC %s\n",
             servoPos, sweeping ? " (Sweep)" : "",
             lidarFresh() ? "ok" : "FEHLT", (unsigned long)lidarMm,
             (unsigned long)lidarCount, relayMask,
             pcKnown ? pcIp.toString().c_str() : "-");
        tLog = millis() + 5000;
    }
}
