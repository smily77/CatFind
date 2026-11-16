/*
 * PanTiltHead_WM_V2 — Schwenkkopf von MapDiscoveryV2
 *
 * Neu geschrieben (Nachfolger von MapDiscovery/PanTiltHead_WM, gleiche
 * Hardware, gleiche Aufgabe): fährt Pan/Tilt-Servos, misst mit dem Laser-
 * Entfernungsmesser (SEN0366 oder X40, automatisch erkannt) und schaltet den
 * CatFind-LaserMarker. Bedient wird er von PanTiltKarte_WM_V2 über WLAN-UDP,
 * Protokoll siehe ../Common/MDV2Protocol.h.
 *
 * Aufbau
 *   - Netz:      UDP 4210, jede Nachricht hat eine Auftragsnummer (seq).
 *   - Messung:   eigener Ablauf (Zustandsautomat): [Anti-Backlash-Vorfahrt] ->
 *                Zielfahrt -> warten bis Pan am Ziel -> ausschwingen -> Ist-
 *                Winkel lesen -> messen (bis 3 Versuche) -> Endantwort.
 *                Während der Messung bleibt die Position stehen; neue Soll-
 *                positionen werden gemerkt und danach angefahren.
 *   - Wiederholte Aufträge (gleiche seq) lösen keine zweite Messung aus.
 *
 * Board: ESP32 classic (Labor-Turm, FQBN esp32:esp32:esp32) oder
 *        XIAO ESP32-S3 (esp32:esp32:XIAO_ESP32S3). Pins in ../Common/MDV2Config.h.
 * Libraries: FeetechBusServos, Laser_RangeFinder_X40 (LaserRangefinderAuto,
 *        braucht LaserRangefinder_SEN0366), MyLGFXConfigs (Credentials.h).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Credentials.h>            // ssid, password
#include <FeetechBus.h>
#include <FeetechST3020.h>
#include <FeetechSC15.h>
#include <LaserRangefinderAuto.h>

#include "../Common/MDV2Config.h"
#include "../Common/MDV2Protocol.h"

// ── Parameter ───────────────────────────────────────────────────────────────
static const float    PAN_LIMIT_DEG     = 170.0f;
static const float    TILT_LIMIT_DEG    = 30.0f;
static const float    BACKLASH_DEG      = 5.0f;    // Vorfahrt unter das Ziel
static const uint16_t MOVE_TIME_MS      = 300;     // moveTime: Fahrzeit
static const uint16_t MOVE_SPEED        = 600;     // moveTime: Geschwindigkeitsgrenze
static const int      ARRIVE_TOL_TICKS  = 6;       // ~0,5°
static const uint32_t ARRIVE_TIMEOUT_MS = 3000;
static const uint32_t SETTLE_MS         = 150;     // nach Ankunft ausschwingen
static const uint32_t PRE_SETTLE_MS     = 250;     // nach Vorfahrt
static const int      MEASURE_TRIES     = 3;

// ── Hardware-Objekte ────────────────────────────────────────────────────────
static FeetechBus    bus(Serial1);
static FeetechST3020 pan(bus, MDV2_PAN_ID);
#if defined(MDV2_TILT_SC)
static FeetechSC15   tilt(bus, MDV2_TILT_ID);
#else
static FeetechST3020 tilt(bus, MDV2_TILT_ID);
#endif

static HardwareSerial       rangerSerial(2);
static LaserRangefinderAuto ranger(&rangerSerial);

static WiFiUDP udp;

// ── Zustand ─────────────────────────────────────────────────────────────────
static bool     tiltPresent   = false;
static uint32_t tiltPingT     = 0;
static bool     laserState    = false;   // tatsächlich gesetzt
static bool     laserWanted   = false;   // von der Karte gewünscht
static bool     markerState   = false;
static float    panCmdDeg     = 0.0f;    // zuletzt angefahrene Sollposition
static float    tiltCmdDeg    = 0.0f;
static uint16_t panCmdTicks   = MDV2_PAN_ORIGIN;
static uint16_t tiltCmdTicks  = MDV2_TILT_ORIGIN;
static bool     pendingPos    = false;   // Soll während Messung eingetroffen
static float    pendingPan    = 0.0f;
static float    pendingTilt   = 0.0f;

// Messauftrag
enum MeasPhase : uint8_t { MP_IDLE, MP_PRE, MP_MOVE, MP_SETTLE };
static MeasPhase  mPhase = MP_IDLE;
static uint16_t   mSeq;
static uint8_t    mFlags;
static float      mPan, mTilt;
static IPAddress  mIp;
static uint16_t   mPort;
static uint32_t   mT0;               // Beginn der aktuellen Phase
static uint8_t    mArriveHits;
static uint32_t   mStartMs;          // Auftragsbeginn (für Zeitangaben im Log)

// Letzte Endantwort (für wiederholte Aufträge)
static bool         lastValid = false;
static MDV2ReplyMsg lastReply;

// ── Hilfsfunktionen Winkel ──────────────────────────────────────────────────
static uint16_t panTicks(float deg) {
    long t = lroundf(MDV2_PAN_ORIGIN + deg * MDV2_PAN_TICKS_PER_DEG);
    return (uint16_t)constrain(t, 0L, 4095L);
}
static float panDeg(uint16_t ticks) {
    return ((int)ticks - MDV2_PAN_ORIGIN) / MDV2_PAN_TICKS_PER_DEG;
}
static uint16_t tiltTicks(float deg) {
    long t = lroundf(MDV2_TILT_ORIGIN + deg * MDV2_TILT_TICKS_PER_DEG);
    return (uint16_t)constrain(t, 0L, (long)MDV2_TILT_TICKS_MAX);
}
static float tiltDeg(uint16_t ticks) {
    return ((int)ticks - MDV2_TILT_ORIGIN) / MDV2_TILT_TICKS_PER_DEG;
}

static bool readPan(float &deg) {
    uint16_t t;
    if (!pan.readPresentPosition(t)) return false;
    deg = panDeg(t);
    return true;
}
static bool readTilt(float &deg) {
    if (!tiltPresent) return false;
    uint16_t t;
    if (!tilt.readPresentPosition(t)) return false;
    deg = tiltDeg(t);
    return true;
}

// ── Servos ──────────────────────────────────────────────────────────────────
static void movePan(float deg) {
    deg = constrain(deg, -PAN_LIMIT_DEG, PAN_LIMIT_DEG);
    uint16_t t = panTicks(deg);
    panCmdDeg = deg;
    if (t == panCmdTicks) return;
    panCmdTicks = t;
    pan.moveTime(t, MOVE_TIME_MS, MOVE_SPEED);
}
static void moveTilt(float deg) {
    deg = constrain(deg, -TILT_LIMIT_DEG, TILT_LIMIT_DEG);
    uint16_t t = tiltTicks(deg);
    tiltCmdDeg = deg;
    if (t == tiltCmdTicks) return;
    tiltCmdTicks = t;
    if (tiltPresent) tilt.moveTime(t, MOVE_TIME_MS, 400);
}
// Fährt Pan auf beliebigen Winkel, ohne ihn als Soll zu merken (Vorfahrt)
static void movePanRaw(float deg) {
    uint16_t t = panTicks(constrain(deg, -PAN_LIMIT_DEG, PAN_LIMIT_DEG));
    panCmdTicks = t;
    pan.moveTime(t, MOVE_TIME_MS, MOVE_SPEED);
}

static void initServos() {
    bus.beginPins(MDV2_SERVO_BAUD, MDV2_SERVO_RX, MDV2_SERVO_TX);
    delay(100);

    FeetechST3020::Profile pp;
    pp.statusReturnLevel = 1;
    pp.returnDelayUnits  = 0;
    pp.torqueOnAfterInit = true;
    bool panOk = pan.init(pp) && pan.setMode(ServoMode::POSITION);

#if defined(MDV2_TILT_SC)
    FeetechSC15::Profile tp;
#else
    FeetechST3020::Profile tp;
#endif
    tp.statusReturnLevel = 1;
    tp.returnDelayUnits  = 3;    // Antworten auf dem geteilten Bus versetzen
    tp.torqueOnAfterInit = true;
    tiltPresent = tilt.init(tp);
    if (tiltPresent) tilt.setMode(ServoMode::POSITION);

    Serial.printf("Pan  ID %d: %s\n", MDV2_PAN_ID,  panOk ? "OK" : "FEHLT");
    Serial.printf("Tilt ID %d: %s\n", MDV2_TILT_ID, tiltPresent ? "OK" : "FEHLT (Sollwert wird gemeldet)");

    pan.moveTime(MDV2_PAN_ORIGIN, 1500, 400);
    if (tiltPresent) tilt.moveTime(MDV2_TILT_ORIGIN, 1500, 400);
    panCmdTicks  = MDV2_PAN_ORIGIN;
    tiltCmdTicks = MDV2_TILT_ORIGIN;
    delay(1600);
}

// ── Entfernungsmesser ───────────────────────────────────────────────────────
static uint8_t sensorType() {
    switch (ranger.sensorType()) {
        case RANGEFINDER_SEN0366: return MDV2_SENSOR_SEN0366;
        case RANGEFINDER_X40:     return MDV2_SENSOR_X40;
        default:                  return MDV2_SENSOR_NONE;
    }
}

static void applyLaser(bool on) {
    if (ranger.isConnected()) ranger.controlLaser(on ? SEN0366_LASER_ON : SEN0366_LASER_OFF);
    laserState = on;
}

// Misst bis zu MEASURE_TRIES mal. true + Distanz (m) bei Erfolg.
static bool rangerMeasure(float &dist) {
    if (!ranger.isConnected()) {
        if (ranger.redetect()) Serial.printf("Entfernungsmesser erkannt: %s\n", ranger.sensorName());
        else return false;
    }
    for (int i = 1; i <= MEASURE_TRIES; i++) {
        float d = 0.0f;
        if (ranger.singleMeasurement(d) && d > 0.0f) { dist = d; return true; }
        Serial.printf("  Messversuch %d fehlgeschlagen\n", i);
        delay(100);
    }
    return false;
}

// ── LaserMarker ─────────────────────────────────────────────────────────────
// 17-Byte-Kommando (CatFind xCom 6.3): Header 12 B + cmd(1) + info(int32 LE)
static const IPAddress MARKER_IP(MDV2_MARKER_IP);

static void markerSend(uint8_t cmd, int32_t info) {
    uint8_t b[17] = {0};
    b[0] = 0x63;      // Protokollversion
    b[1] = 0xF0;      // Absender (vom Marker ignoriert)
    b[2] = 5;         // msgCode Kommando
    b[3] = 5;         // Nutzlast-Länge
    b[12] = cmd;
    memcpy(&b[13], &info, 4);
    udp.beginPacket(MARKER_IP, MDV2_MARKER_PORT);
    udp.write(b, sizeof(b));
    udp.endPacket();
}

static void applyMarker(bool on) {
    for (int rep = 0; rep < 2; rep++) {          // UDP: doppelt, Kommandos idempotent
        markerSend(11, on ? 1 : 0);              // Hauptlaser
        markerSend(13, on ? 1 : 0);              // Aux
        markerSend(14, on ? 0xFFFFFF : 0);       // Pixel weiss / aus
        delay(5);
    }
    markerState = on;
    Serial.printf("Marker %s\n", on ? "EIN" : "AUS");
}

// ── Antworten ───────────────────────────────────────────────────────────────
static MDV2ReplyMsg makeReply(uint16_t seq, uint8_t cmd, uint8_t status) {
    MDV2ReplyMsg r = {};
    r.magic  = MDV2_MAGIC;
    r.seq    = seq;
    r.cmd    = cmd;
    r.status = status;
    r.flags  = (laserState ? MDV2_F_LASER : 0) | (markerState ? MDV2_F_MARKER : 0);
    r.sensor = sensorType();
    r.panActDeg  = panCmdDeg;
    r.tiltActDeg = tiltCmdDeg;
    return r;
}

static void sendReply(const MDV2ReplyMsg &r, const IPAddress &ip, uint16_t port) {
    udp.beginPacket(ip, port);
    udp.write((const uint8_t *)&r, sizeof(r));
    udp.endPacket();
}

// ── Messablauf ──────────────────────────────────────────────────────────────
static void startMeasure(const MDV2CmdMsg &c, const IPAddress &ip, uint16_t port) {
    mSeq   = c.seq;
    mFlags = c.flags;
    mPan   = constrain(c.panDeg,  -PAN_LIMIT_DEG,  PAN_LIMIT_DEG);
    mTilt  = constrain(c.tiltDeg, -TILT_LIMIT_DEG, TILT_LIMIT_DEG);
    mIp    = ip;
    mPort  = port;
    pendingPos = false;

    sendReply(makeReply(mSeq, MDV2_CMD_MEASURE, MDV2_ST_ACCEPTED), ip, port);

    moveTilt(mTilt);
    if (mFlags & MDV2_F_APPROACH) {
        movePanRaw(mPan - BACKLASH_DEG);
        mPhase = MP_PRE;
    } else {
        movePan(mPan);
        mPhase = MP_MOVE;
    }
    mT0 = mStartMs = millis();
    mArriveHits = 0;
    Serial.printf("[%u] Messung Pan %.2f Tilt %.2f%s\n", mSeq, mPan, mTilt,
                  (mFlags & MDV2_F_APPROACH) ? " (Anfahrt)" : "");
}

// true, sobald Pan zweimal hintereinander innerhalb der Toleranz steht
static bool panArrived() {
    uint16_t t;
    if (pan.readPresentPosition(t) && abs((int)t - (int)panCmdTicks) <= ARRIVE_TOL_TICKS) {
        if (++mArriveHits >= 2) return true;
    } else {
        mArriveHits = 0;
    }
    return false;
}

static void finishMeasure() {
    float panAct = mPan, tiltAct = mTilt;
    bool  panOk  = false, tiltOk = false;
    for (int i = 0; i < 3 && !panOk; i++) panOk = readPan(panAct);
    tiltOk = readTilt(tiltAct);
    if (!panOk) panAct = mPan;
    if (!tiltOk) tiltAct = mTilt;

    uint32_t tMove = millis() - mStartMs;
    float dist = 0.0f;
    bool  ok   = rangerMeasure(dist);
    uint32_t tMeas = millis() - mStartMs - tMove;
    applyLaser(laserWanted);   // Messung kann den Laser abschalten -> Wunsch wiederherstellen

    uint8_t st = ok ? MDV2_ST_DONE : (ranger.isConnected() ? MDV2_ST_FAILED : MDV2_ST_NOSENSOR);
    MDV2ReplyMsg r = makeReply(mSeq, MDV2_CMD_MEASURE, st);
    r.distanceM  = ok ? dist : 0.0f;
    r.panActDeg  = panAct;
    r.tiltActDeg = tiltAct;
    r.tiltOk     = tiltOk ? 1 : 0;
    sendReply(r, mIp, mPort);
    lastReply = r;
    lastValid = true;
    mPhase = MP_IDLE;

    if (ok) Serial.printf("[%u] %s %.3f m  Pan ist %.2f%s  Tilt %.2f%s  (Fahrt %lu ms, Messung %lu ms)\n",
                          mSeq, ranger.sensorName(), dist, panAct, panOk ? "" : "(Soll)",
                          tiltAct, tiltOk ? "" : "(Soll)", (unsigned long)tMove, (unsigned long)tMeas);
    else    Serial.printf("[%u] Messung fehlgeschlagen (%s, %lu ms)\n", mSeq,
                          ranger.isConnected() ? ranger.sensorName() : "kein Sensor", (unsigned long)tMeas);

    if (pendingPos) {          // während der Messung gewünschte Position nachholen
        pendingPos = false;
        movePan(pendingPan);
        moveTilt(pendingTilt);
    }
}

static void stepMeasure() {
    if (mPhase == MP_IDLE) return;
    uint32_t dt = millis() - mT0;
    switch (mPhase) {
        case MP_PRE:
            if (panArrived() || dt > ARRIVE_TIMEOUT_MS) {
                if (dt > ARRIVE_TIMEOUT_MS) Serial.println("  Vorfahrt: Timeout");
                delay(PRE_SETTLE_MS);
                panCmdTicks = 0xFFFF;          // erzwingt das Senden der Zielfahrt
                movePan(mPan);
                mPhase = MP_MOVE; mT0 = millis(); mArriveHits = 0;
            }
            break;
        case MP_MOVE:
            if (panArrived() || dt > ARRIVE_TIMEOUT_MS) {
                if (dt > ARRIVE_TIMEOUT_MS) Serial.println("  Zielfahrt: Timeout, messe trotzdem");
                mPhase = MP_SETTLE; mT0 = millis();
            }
            break;
        case MP_SETTLE:
            if (dt >= SETTLE_MS) finishMeasure();
            break;
        default: break;
    }
}

// ── Empfang ─────────────────────────────────────────────────────────────────
static void handleSet(const MDV2CmdMsg &c, const IPAddress &ip, uint16_t port) {
    laserWanted = c.flags & MDV2_F_LASER;
    bool mk     = c.flags & MDV2_F_MARKER;
    if (mk != markerState) applyMarker(mk);

    if (mPhase != MP_IDLE) {
        pendingPos  = true;
        pendingPan  = c.panDeg;
        pendingTilt = c.tiltDeg;
        sendReply(makeReply(c.seq, MDV2_CMD_SET, MDV2_ST_BUSY), ip, port);
        return;
    }
    if (laserWanted != laserState) applyLaser(laserWanted);
    movePan(c.panDeg);
    moveTilt(c.tiltDeg);
    sendReply(makeReply(c.seq, MDV2_CMD_SET, MDV2_ST_DONE), ip, port);
}

static void handleMeasure(const MDV2CmdMsg &c, const IPAddress &ip, uint16_t port) {
    laserWanted = c.flags & MDV2_F_LASER;
    bool mk     = c.flags & MDV2_F_MARKER;
    if (mk != markerState) applyMarker(mk);

    if (mPhase != MP_IDLE) {
        // läuft: derselbe Auftrag -> BUSY; ein anderer wird abgewiesen (BUSY mit seiner seq)
        sendReply(makeReply(c.seq, MDV2_CMD_MEASURE, MDV2_ST_BUSY), ip, port);
        return;
    }
    if (lastValid && lastReply.seq == c.seq) {      // Wiederholung eines fertigen Auftrags
        sendReply(lastReply, ip, port);
        return;
    }
    startMeasure(c, ip, port);
}

static void pollUdp() {
    for (int guard = 0; guard < 8; guard++) {
        int n = udp.parsePacket();
        if (n <= 0) return;
        MDV2CmdMsg c;
        if (n != sizeof(c)) { udp.flush(); continue; }
        udp.read((uint8_t *)&c, sizeof(c));
        if (c.magic != MDV2_MAGIC) continue;
        IPAddress ip  = udp.remoteIP();
        uint16_t port = udp.remotePort();
        if (c.cmd == MDV2_CMD_SET)          handleSet(c, ip, port);
        else if (c.cmd == MDV2_CMD_MEASURE) handleMeasure(c, ip, port);
    }
}

// ── WLAN ────────────────────────────────────────────────────────────────────
static void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                // keine Funkpausen -> kurze Antwortzeiten
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
    Serial.printf("WLAN \"%s\" ", ssid);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 30000) { Serial.println("\nWLAN fehlgeschlagen, Neustart"); ESP.restart(); }
        delay(250);
        Serial.print('.');
    }
    Serial.printf("\nWLAN OK, IP %s\n", WiFi.localIP().toString().c_str());
}

static void watchWifi() {
    static uint32_t lostSince = 0;
    if (WiFi.status() == WL_CONNECTED) { lostSince = 0; return; }
    if (!lostSince) { lostSince = millis(); Serial.println("WLAN weg, warte auf Wiederverbindung"); }
    if (millis() - lostSince > 60000) ESP.restart();
}

// ── Setup / Loop ────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== PanTiltHead_WM_V2 ===");

    initServos();

    if (ranger.begin(MDV2_RANGER_RX, MDV2_RANGER_TX))
        Serial.printf("Entfernungsmesser: %s\n", ranger.sensorName());
    else
        Serial.println("Entfernungsmesser: keiner erkannt (neuer Versuch bei Messung)");
    applyLaser(false);

    connectWifi();
    udp.begin(MDV2_PORT_HEAD);
    applyMarker(false);
    Serial.printf("Bereit, UDP %u\n", MDV2_PORT_HEAD);
}

void loop() {
    watchWifi();
    pollUdp();
    stepMeasure();

    // stummen Tilt-Servo ab und zu neu suchen
    if (!tiltPresent && mPhase == MP_IDLE && millis() - tiltPingT > 10000) {
        tiltPingT = millis();
        if (tilt.ping()) {
            tiltPresent = true;
            tilt.setTorque(true);
            Serial.println("Tilt-Servo meldet sich jetzt");
        }
    }
    delay(mPhase == MP_IDLE ? 2 : 15);
}
