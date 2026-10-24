/*
 * Step4_LidarLaser — PowerActor2 nach dem Umbau, Schritt 4: Servo wie Schritt 3
 *                    + LP40B-Lidar misst laufend, Laser EIN unter 0,5 m
 *
 * Gegenstueck: ../Step4_Display (CYD28, Touch + Drehknopf).
 * Basis: Step3_ServoFull (Servo-Teil unveraendert).
 *
 * Servo wie Step1_ServoSweep:
 *   TX (GPIO19) --[1kΩ]--+-- SERVO BUS DATA
 *   RX (GPIO18) ----------+
 *   Feetech ST3020, 1 Mbit/s, Positionen 0..4095 (= -180..+180 Grad um 2048),
 *   links = 0, rechts = 4095, Schnellfahrt mit Geschwindigkeit/Beschleunigung 0.
 *
 * Lidar: LP40B / DFRobot LPB40B (SEN0586) an Serial2, 115200 Baud
 *   LP40B TX -> GPIO16 (RX2), LP40B RX <- GPIO17 (TX2)   (wie PA2i6_3_0)
 *   Protokoll laut SEN0586_Communication_protocol-EN.pdf (ohne Bibliothek):
 *   Rahmen 55 | Key | Value[4] | CRC8 ueber Key+Value (Polynom 0x31) | AA
 *   Messwert: Key 07, Value = Fehlercode, Distanz mm (3 Byte, hoechstes zuerst).
 *   Betrieb: Byte-Format, Modus "Dauermessung auf Startbefehl" (0x02) +
 *   Start (0x05), Sensor-Standard 50 Hz. Kommt LIDAR_STALE_MS nichts, wird
 *   die Einrichtung wiederholt (Sensor spaeter eingeschaltet / angesteckt).
 *
 * Laser: GPIO25 (laserPwr, aktiv HIGH). EIN solange die letzte gueltige
 *   Messung (Fehlercode 0, juenger als LIDAR_STALE_MS) < LASER_ON_MM ist,
 *   sonst AUS. Uebrige Relais-Pins (27 Ventil, 26, 23) bleiben sicher AUS.
 *
 * Protokoll (eigenes Test-Protokoll, NICHT xCom):
 *   Display -> PA2 : Step2Cmd an UDP 4310 (Unicast an 192.168.0.181)
 *   PA2 -> Display : Step4State an die Absender-IP, Port 4311, als Antwort
 *                    auf JEDES Kommando (auch Poll).
 *   Magic 0x54: ein altes Step3-Display wird ignoriert.
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

// ── Lidar / Laser ────────────────────────────────────────────────────────────

#define LIDAR_RX_PIN    16
#define LIDAR_TX_PIN    17
#define LIDAR_BAUD      115200
#define LIDAR_STALE_MS  1000     // so lange ohne Messwert -> neu einrichten
#define LIDAR_NONE      0xFF     // Fehlercode "noch keine Messung"
#define LASER_PIN       25
#define LASER_ON_MM     500

static uint32_t lidarMm   = 0;
static uint8_t  lidarErr  = LIDAR_NONE;   // 0 = ok, 1 schwach, 2 stark, 3 Bereich, 4 System
static uint32_t lidarAt   = 0;            // millis() der letzten Messung
static uint32_t lidarCount = 0;
static bool     laserOn   = false;

// ── Relais (Ventil & Co. nur sicher AUS, nicht bedienbar) ────────────────────

static const uint8_t OFF_PINS[3] = {27, 26, 23};

// ── Protokoll (identisch in Step4_Display) ───────────────────────────────────

#define STEP2_CMD_PORT    4310
#define STEP2_STATE_PORT  4311
#define STEP4_MAGIC       0x54

enum : uint8_t { S2_POLL = 0, S2_SERVO = 1, S2_FAST = 3 };

struct __attribute__((packed)) Step2Cmd {
    uint8_t  magic;
    uint8_t  type;       // S2_POLL / S2_SERVO / S2_FAST
    uint16_t value;      // SERVO/FAST: Zielposition
};

struct __attribute__((packed)) Step4State {
    uint8_t  magic;
    uint8_t  servoOk;    // 0 = Servo antwortet nicht
    uint16_t servoGoal;
    uint16_t servoPos;   // Ist-Position laut Servo
    uint8_t  tempC;
    uint8_t  lidarErr;   // LIDAR_NONE = keine (frische) Messung
    uint32_t lidarMm;
    uint8_t  laserOn;
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

static void setLaser(bool on);

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
            setLaser(false);                                         // sicher aus
            for (uint8_t p : OFF_PINS) digitalWrite(p, LOW);
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
    }
}

static void setLaser(bool on) {
    if (on == laserOn) return;
    laserOn = on;
    digitalWrite(LASER_PIN, on ? HIGH : LOW);
    logf("Laser %s (Lidar %lu mm, Fehler %u)\n", on ? "EIN" : "AUS",
         (unsigned long)lidarMm, lidarErr);
}

static bool lidarFresh() {
    return lidarErr != LIDAR_NONE && millis() - lidarAt < LIDAR_STALE_MS;
}

// ── Kommandos ────────────────────────────────────────────────────────────────

static void sendState(IPAddress to) {
    Step4State s;
    s.magic     = STEP4_MAGIC;
    s.servoGoal = servoGoal;
    uint16_t pos = 0;
    uint8_t  t   = 0;
    s.servoOk   = servoOk && servo.readPresentPosition(pos);
    if (s.servoOk) servo.readCurrentTemperature(t);
    s.servoPos  = pos;
    s.tempC     = t;
    s.lidarErr  = lidarFresh() ? lidarErr : LIDAR_NONE;
    s.lidarMm   = lidarMm;
    s.laserOn   = laserOn;
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
    if (c.magic != STEP4_MAGIC) return;
    switch (c.type) {
        case S2_SERVO:
            servoGoTo(c.value, SERVO_SPEED);
            break;
        case S2_FAST:
            servoGoTo(c.value, SERVO_FAST);
            logf("Schnellfahrt nach %u (%s)\n", servoGoal,
                 servoGoal < SERVO_CENTER ? "links" :
                 servoGoal > SERVO_CENTER ? "rechts" : "Mitte");
            break;
        default:
            break;                           // S2_POLL: nur Zustand melden
    }
    sendState(udp.remoteIP());
}

// ── Arduino ──────────────────────────────────────────────────────────────────

void setup() {
    digitalWrite(LASER_PIN, LOW);            // zuerst: Laser und Relais sicher aus
    pinMode(LASER_PIN, OUTPUT);
    for (uint8_t p : OFF_PINS) {
        digitalWrite(p, LOW);
        pinMode(p, OUTPUT);
    }
    Serial.begin(115200);
    delay(500);
    logf("\nPowerActor2 Step4_LidarLaser\n");
    setUpWifi();
    setUpOTA();
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    udp.begin(STEP2_CMD_PORT);
    initServo();
    initLidar();
}

void loop() {
    ArduinoOTA.handle();
    handleUdp();
    pollLidar();

    // Laser: nur bei frischer, gueltiger Messung unter LASER_ON_MM
    setLaser(lidarFresh() && lidarErr == 0 && lidarMm < LASER_ON_MM);

    static uint32_t tRetry = 0, tLidarRetry = 0, tLidarLog = 0;
    static uint16_t lastLogged = 0;
    static uint32_t tLog = 0;

    if (telnetServer.hasClient()) {
        if (telnet) telnet.stop();
        telnet = telnetServer.available();
        logf("PowerActor2 Step4_LidarLaser - Telnet verbunden, Servo %s, Lidar %s\n",
             servoOk ? "ok" : "FEHLT", lidarFresh() ? "ok" : "FEHLT");
    }
    if (!servoOk && millis() > tRetry) {     // Servo nachtraeglich angesteckt?
        initServo();
        tRetry = millis() + 5000;
    }
    if (!lidarFresh() && millis() > tLidarRetry) {   // Lidar stumm -> neu einrichten
        lidarSetup();
        tLidarRetry = millis() + 2000;
    }
    if (millis() > tLidarLog) {              // alle 5 s ein Lidar-Lebenszeichen
        if (lidarFresh())
            logf("Lidar %lu mm, Fehler %u, %lu Messungen\n",
                 (unsigned long)lidarMm, lidarErr, (unsigned long)lidarCount);
        else
            logf("Lidar: keine Messung (%lu bisher)\n", (unsigned long)lidarCount);
        tLidarLog = millis() + 5000;
    }
    if (servoOk && servoGoal != lastLogged && millis() > tLog) {
        logf("Servo Soll %u\n", servoGoal);
        lastLogged = servoGoal;
        tLog = millis() + 300;
    }
}
