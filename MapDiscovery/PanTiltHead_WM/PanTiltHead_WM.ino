/*
 * PanTiltHead_WM — XIAO ESP32-S3 (oder ESP32 classic) + Feetech Servos + SEN0366
 *                  + Laser-Marker (CatFind LaserMarker 6_3)
 *
 * Empfangseinheit des Pan/Tilt-Systems. Verbindet sich mit dem Haus-WLAN,
 * lauscht auf UDP-Steuerpakete vom PanTiltController, positioniert Pan- und
 * Tilt-Servo und steuert den Laser-Entfernungsmesser. Sendet Messergebnisse
 * per UDP zurück an den Controller.
 *
 * NEU (WM = "With Marker"): Dieser Head steuert zusätzlich den Laser-Marker
 * (ESP32-C3, feste IP 192.168.0.182). Der Marker wird AUSSCHLIESSLICH von
 * diesem Head angesteuert – der Controller (PanTiltKarte_WM) schickt nur das
 * Flag markerOn im ControlMsg, das Versenden der Marker-UDP-Pakete passiert
 * hier. Marker "ein" = mainLaser + subLaser + Aux an und Pixel weiss.
 * Protokoll: siehe CatFind/LaserMarker/API_LaserMarker6_3.md (17-Byte-Kommando,
 * Unicast an 192.168.0.182:23456, Little-Endian, gepackt). Es werden KEINE
 * CatFind-Header benötigt – die Pakete werden hier selbst zusammengebaut.
 *
 * Hardware:
 *   Board  : Seeed XIAO ESP32-S3 (oder ESP32 classic, Pins via TurnBaseConfig.h)
 *   FQBN   : esp32:esp32:XIAO_ESP32S3
 *   Pan    : Feetech ST-Servo ST3020 – Drehung (Yaw),  ID=2, Origin=2048
 *   Tilt   : Feetech SC-Servo SC15   – Neigung (Pitch), ID=4, Origin=680
 *   Laser  : DFRobot SEN0366 / GJD-01, UART 9600 Baud
 *
 * Verdrahtung Servo-Bus (Half-Duplex UART):
 *   TX --[1kΩ]--+-- SERVO BUS DATA
 *   RX ----------+
 *
 * Pins, Servo-IDs und Ursprungspositionen: siehe ../TurnBaseConfig.h
 * (ESP32-S3: Servo TX=1, RX=2; Laser RX=7, TX=8)
 * (ESP32:    Servo TX=16, RX=17; Laser RX=26, TX=25)
 *
 * Wichtige Erkenntnisse für künftige Projekte:
 *   - ST-Servo (ST3020, STS-Protokoll, Little-Endian, Vorzeichenbit 15) und
 *     SC-Servo (SC15, SCS-Protokoll, Big-Endian) sind unterschiedliche Klassen
 *     (FeetechST3020 vs FeetechSC15). Verwechslung führt zu falschen Positions-
 *     werten ohne Fehlermeldung, da beide Protokolle ähnlich aussehen.
 *   - SC15 returnDelayUnits muss auf einen Wert >0 gesetzt werden (hier 3),
 *     damit sich Antwortpakete auf dem geteilten Half-Duplex-Bus nicht mit
 *     Paketen des ST3020 überschneiden.
 *   - Die mechanischen Endanschläge des Tilt-SC15 sind im Servo-EEPROM
 *     gespeichert (setAngleLimits) und werden hardwareseitig erzwungen.
 *     Software-Clamping schadet nicht, ist aber nicht zwingend nötig.
 *   - moveTime(position, timeMs, speed): Der Servo beginnt sofort mit der
 *     neuen Bewegung, auch wenn er noch in Fahrt ist. Kein waitMoveDone()
 *     nötig für Echtzeit-Fernsteuerung – ständige kleine Korrekturbefehle
 *     funktionieren besser als vollständiges Abwarten.
 *   - Winkelumrechnung: Pan 4096 ticks/360° (Origin 2048), Tilt 1024 ticks/180°
 *     (Origin 680). Beide Ursprünge in TurnBaseConfig.h definiert.
 *   - SEN0366-Laser: singleMeasurement() blockiert bis zu 1000 ms (Timeout).
 *     Nur aufrufen wenn doMeasure==1, nicht im normalen Servo-Positions-Loop.
 *     Rückgabe in Metern (float), Umrechnung auf mm im Controller.
 *   - UDP-Architektur: Head lauscht auf Port 4210 (Befehle), antwortet direkt
 *     an die Sender-IP des Controllers auf Port 4211. Kein Pairing nötig –
 *     die Controller-IP wird aus dem empfangenen UDP-Paket gelesen.
 *   - WiFi Station (nicht Access Point): Beide Geräte verbinden sich mit
 *     demselben Router. WiFi-Zugangsdaten via #include <Credentials.h> aus
 *     Arduino/libraries/MyLGFXConfigs/.
 *   - TurnBaseConfig.h wählt Servo- und Sensor-Pins automatisch per
 *     CONFIG_IDF_TARGET_ESP32S3 / CONFIG_IDF_TARGET_ESP32.
 *   - Bibliotheken: SerialServo (smily77/SerialServo), LaserRangefinder_SEN0366
 *     (smily77/Laser_Rangefinder_SEN0366_GJD-01), WiFi (ESP32-Kern)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <Credentials.h>   // ssid, password
#include <FeetechBus.h>
#include <FeetechST3020.h>
#include <FeetechSC15.h>
#include <LaserRangefinder_SEN0366.h>

#include "../TurnBaseConfig.h"

// ── WiFi / UDP ───────────────────────────────────────────────────────────────

static const uint16_t UDP_CMD_PORT    = 4210;   // Head lauscht hier
static const uint16_t UDP_RESULT_PORT = 4211;   // Controller lauscht hier

static WiFiUDP udp;

// ── Nachrichtenstrukturen (müssen mit PanTiltController identisch sein) ───────

struct __attribute__((packed)) ControlMsg {
    float   panDeg;
    float   tiltDeg;
    uint8_t laserOn;
    uint8_t doMeasure;
    uint8_t approach;    // 1 = Anti-Backlash-Anfahrt (Pan immer von definierter Seite)
    uint8_t markerOn;    // 1 = Laser-Marker an (mainLaser+subLaser+Aux+Pixel weiss)
};

struct __attribute__((packed)) ResultMsg {
    float   distanceM;      // Messdistanz in Metern (0 wenn ungültig)
    float   panActualDeg;   // tatsächlicher Pan-Winkel laut Servo-Feedback
    uint8_t laserState;     // aktueller Laser-Zustand (0/1)
    uint8_t valid;          // 1 = Messung gültig, 0 = kein Ergebnis
    uint8_t markerState;    // aktueller Laser-Marker-Zustand (0/1)
};

// ── Laser-Marker (CatFind LaserMarker 6_3) ───────────────────────────────────
// Der Marker wird per UDP-Unicast an seine feste IP gesteuert. Wir bauen die
// 17-Byte-Kommandopakete selbst zusammen (kein CatFind-SDK nötig).
// Header (12 B): version=0x63, sender(egal), msgCode=5, payloadLen=5, timeStamp=0
// Payload (5 B): cmd(uint8), info(int32 LE).
static const IPAddress MARKER_IP(192, 168, 0, 182);
static const uint16_t  MARKER_PORT      = 23456;
static const uint8_t   MARKER_VERSION   = 0x63;
static const uint8_t   MARKER_MSG_CMD   = 5;
static const uint8_t   MK_CMD_MAINLASER = 11;
static const uint8_t   MK_CMD_SUBLASER  = 12;
static const uint8_t   MK_CMD_AUX       = 13;
static const uint8_t   MK_CMD_PIXEL     = 14;
static const int32_t   MK_PIXEL_WHITE   = 0xFFFFFF;
static const int32_t   MK_PIXEL_OFF     = 0x000000;

static bool markerState = false;   // zuletzt gesetzter Marker-Zustand

static void markerSendCmd(uint8_t cmd, int32_t info) {
    uint8_t buf[17];
    buf[0] = MARKER_VERSION;
    buf[1] = 0xF0;                 // sender – wird vom Marker ignoriert
    buf[2] = MARKER_MSG_CMD;
    buf[3] = 5;                    // payloadLen
    memset(&buf[4], 0, 8);         // timeStamp = 0
    buf[12] = cmd;
    memcpy(&buf[13], &info, 4);    // info, Little-Endian (ESP32 ist LE)
    udp.beginPacket(MARKER_IP, MARKER_PORT);
    udp.write(buf, sizeof(buf));
    udp.endPacket();
}

// Setzt alle Marker-Ausgänge auf den gewünschten Zustand. Zur Robustheit gegen
// UDP-Paketverlust wird jedes Kommando zweimal gesendet (idempotent).
static void markerApply(bool on) {
    for (int rep = 0; rep < 2; rep++) {
        markerSendCmd(MK_CMD_MAINLASER, on ? 1 : 0);
        //markerSendCmd(MK_CMD_SUBLASER,  on ? 1 : 0);
        markerSendCmd(MK_CMD_AUX,       on ? 1 : 0);
        markerSendCmd(MK_CMD_PIXEL,     on ? MK_PIXEL_WHITE : MK_PIXEL_OFF);
        delay(5);
    }
    markerState = on;
}

// ── Servos ───────────────────────────────────────────────────────────────────

FeetechBus    bus(Serial1);
FeetechST3020 pan(bus, panServoID);
#if defined(TILT_SERVO_SC)
FeetechSC15   tilt(bus, tiltServoID);
#elif defined(TILT_SERVO_ST)
FeetechST3020 tilt(bus, tiltServoID);
#endif

// ── Laser-Sensor ─────────────────────────────────────────────────────────────

LaserRangefinder_SEN0366 laser(&Serial2);

// ── Hilfsfunktionen: Winkel → Servo-Ticks ────────────────────────────────────

// Pan: ST-Servo, 4096 ticks / 360°
static uint16_t panDegToTicks(float deg) {
    int32_t t = panServoOrigin + (int32_t)(deg * (4096.0f / 360.0f));
    return (uint16_t)constrain(t, 0, 4095);
}

// Umkehrung: Ist-Ticks → Grad (Umrechnung bleibt ausschliesslich im Head)
static float panTicksToDeg(uint16_t ticks) {
    return ((int32_t)ticks - panServoOrigin) * (360.0f / 4096.0f);
}

// ── Anti-Backlash-Anfahrt ─────────────────────────────────────────────────────
// Mechanisches Getriebespiel macht die Endposition richtungsabhängig. Für
// reproduzierbare Messungen fahren wir das Ziel immer von der gleichen Seite an:
// erst BACKOFF° darunter (von unten), einschwingen, dann hoch auf das Ziel.
static const float    BACKLASH_BACKOFF_DEG = 5.0f;   // Rückzug hinter das Ziel
static const uint16_t APPROACH_MOVE_MS     = 400;    // Fahrzeit je Teilbewegung
static const uint16_t APPROACH_SETTLE_MS   = 500;    // Einschwingzeit je Teilbewegung

static void panApproach(uint16_t targetTicks, float targetDeg) {
    uint16_t preTicks = panDegToTicks(targetDeg - BACKLASH_BACKOFF_DEG);
    pan.moveTime(preTicks,    APPROACH_MOVE_MS, 600);
    delay(APPROACH_SETTLE_MS);
    pan.moveTime(targetTicks, APPROACH_MOVE_MS, 600);
    delay(APPROACH_SETTLE_MS);
}

#if defined(TILT_SERVO_SC)
static uint16_t tiltDegToTicks(float deg) {
    int32_t t = tiltServoOrigin + (int32_t)(deg * (1024.0f / 180.0f));
    return (uint16_t)constrain(t, 0, 1023);
}
#elif defined(TILT_SERVO_ST)
static uint16_t tiltDegToTicks(float deg) {
    int32_t t = tiltServoOrigin + (int32_t)(deg * (4096.0f / 360.0f));
    return (uint16_t)constrain(t, 0, 4095);
}
#endif

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== PanTiltHead Start ===");

    // Servo-Bus initialisieren
    bus.beginPins(servoBusSpeed, SERVO_RX_PIN, SERVO_TX_PIN);
    delay(100);

    Serial.print("Pan  ping: "); Serial.println(pan.ping()  ? "OK" : "FAIL");
    Serial.print("Tilt ping: "); Serial.println(tilt.ping() ? "OK" : "FAIL");

    Serial.print("Pan  setMode POSITION: "); Serial.println(pan.setMode(ServoMode::POSITION)  ? "OK" : "FAIL");
    Serial.print("Tilt setMode POSITION: "); Serial.println(tilt.setMode(ServoMode::POSITION) ? "OK" : "FAIL");

    FeetechST3020::Profile pPan;
    pPan.statusReturnLevel = 1;
    pPan.returnDelayUnits  = 0;
    pPan.torqueOnAfterInit = true;
    pan.init(pPan);

#if defined(TILT_SERVO_SC)
    FeetechSC15::Profile pTilt;
    pTilt.returnDelayUnits  = 3;
#elif defined(TILT_SERVO_ST)
    FeetechST3020::Profile pTilt;
    pTilt.returnDelayUnits  = 3;
#endif
    pTilt.statusReturnLevel = 1;
    pTilt.torqueOnAfterInit = true;
    tilt.init(pTilt);

    Serial.println("Fahre zu Origin...");
    pan.moveTime(panServoOrigin,  1500, 400);
    tilt.moveTime(tiltServoOrigin, 1500, 400);
    delay(2000);
    Serial.println("Origin erreicht.");

    // Laser-Sensor initialisieren
    Serial2.begin(SEN0366_BAUD_RATE, SERIAL_8N1, sensorRxD, sensorTxD);
    laser.begin();
    laser.controlLaser(SEN0366_LASER_OFF);
    Serial.println("Laser initialisiert (aus).");

    // WLAN verbinden
    Serial.printf("Verbinde mit WLAN \"%s\"...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 30000) {
            Serial.println("FEHLER: WLAN-Verbindung fehlgeschlagen! Neustart...");
            delay(1000);
            ESP.restart();
        }
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nWLAN verbunden  IP: %s\n", WiFi.localIP().toString().c_str());

    udp.begin(UDP_CMD_PORT);
    Serial.printf("UDP lauscht auf Port %u\n", UDP_CMD_PORT);

    // Laser-Marker in definierten Aus-Zustand bringen
    markerApply(false);
    Serial.printf("Laser-Marker initialisiert (aus), IP %s:%u\n",
                  MARKER_IP.toString().c_str(), MARKER_PORT);

    Serial.println("Warte auf Befehle vom Controller...");
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    int n = udp.parsePacket();
    if (n != sizeof(ControlMsg)) {
        delay(5);
        return;
    }

    ControlMsg cmd;
    udp.read((uint8_t *)&cmd, sizeof(cmd));
    IPAddress senderIP = udp.remoteIP();

    // Laser steuern
    laser.controlLaser(cmd.laserOn ? SEN0366_LASER_ON : SEN0366_LASER_OFF);

    // Laser-Marker steuern (nur bei Zustandsänderung; Kommandos sind idempotent)
    if ((bool)cmd.markerOn != markerState) {
        markerApply((bool)cmd.markerOn);
        Serial.printf("Marker %s\n", markerState ? "EIN" : "AUS");
    }

    // Servos positionieren
    uint16_t panTicks  = panDegToTicks(cmd.panDeg);
    uint16_t tiltTicks = tiltDegToTicks(cmd.tiltDeg);
    tilt.moveTime(tiltTicks, 300, 400);

    if (cmd.approach) {
        // Definierte Anfahrt für reproduzierbare Messung (von unten)
        panApproach(panTicks, cmd.panDeg);
    } else {
        pan.moveTime(panTicks, 300, 600);
    }

    // Ist-Pan-Winkel aus Servo-Feedback lesen (auch wenn nicht gemessen wird).
    // Fallback auf den Sollwinkel, falls die Bus-Abfrage fehlschlägt.
    float    panActualDeg = cmd.panDeg;
    uint16_t panActualTicks;
    if (pan.readPresentPosition(panActualTicks)) {
        panActualDeg = panTicksToDeg(panActualTicks);
    }

    Serial.printf("Pan soll=%.1f°(%u) ist=%.1f°%s Tilt=%.1f°(%u) Laser=%s\n",
                  cmd.panDeg, panTicks, panActualDeg,
                  cmd.approach ? " [Anfahrt]" : "", cmd.tiltDeg, tiltTicks,
                  cmd.laserOn ? "ein" : "aus");

    // Ergebnis-Nachricht
    ResultMsg result;
    result.panActualDeg = panActualDeg;
    result.laserState   = cmd.laserOn;
    result.valid        = 0;
    result.distanceM    = 0.0f;
    result.markerState  = markerState ? 1 : 0;

    if (cmd.doMeasure) {
        float dist = 0.0f;
        bool  ok   = false;
        for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
            ok = laser.singleMeasurement(dist);
            Serial.printf("Messversuch %d: %s\n", attempt, ok ? "OK" : "fehlgeschlagen");
            if (!ok && attempt < 3) delay(100);
        }
        if (ok) {
            result.distanceM = dist;
            result.valid     = 1;
            Serial.printf("Messung: %.3f m\n", dist);
        } else {
            Serial.println("Messung nach 3 Versuchen fehlgeschlagen.");
        }
        // singleMeasurement() schaltet den Laser des SEN0366 selbst ab.
        // Befohlenen Zustand wiederherstellen, damit der Laser bei laserOn=1
        // zum Weiterzielen an bleibt (sonst geht er nach jeder Messung aus).
        laser.controlLaser(cmd.laserOn ? SEN0366_LASER_ON : SEN0366_LASER_OFF);
    }

    // Ergebnis an Controller zurücksenden (Port 4211)
    udp.beginPacket(senderIP, UDP_RESULT_PORT);
    udp.write((uint8_t *)&result, sizeof(result));
    udp.endPacket();
}
