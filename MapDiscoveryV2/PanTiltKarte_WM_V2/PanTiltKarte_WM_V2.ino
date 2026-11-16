/*
 * PanTiltKarte_WM_V2 — Bedienung von MapDiscoveryV2 (M5Stack Tab5 + Unit 8Encoder)
 *
 * Neu geschrieben (Nachfolger von MapDiscovery/PanTiltKarte_WM). Bedienung,
 * Ablauf und Aussehen wie V1:
 *   1. ORIGIN MESSEN   Laser auf O=(0,0), messen
 *   2. X-ACHSE MESSEN  Punkt A auf der x-Achse messen -> Kopf-Pose
 *   3. Kartieren       PUNKT MESSEN, AUTO SCAN, EDIT, LETZTEN LOESCHEN,
 *                      KARTE SPEICHERN (/karte_NNN.csv), ANFAHRT PRUEFEN
 *   LASER / MARKER EIN/AUS in allen Zuständen.
 *
 * Neu gegenüber V1:
 *   - Tilt-Korrektur: in die Karte geht die waagrechte Distanz d*cos(Tilt-Ist).
 *   - Auto-Scan-Schrittweite wählbar (Knopf rechts neben AUTO SCAN: 0,5/1/2/5°,
 *     wird gespeichert).
 *   - Darunter: Protokoll mit Auftragsnummern (../Common/MDV2Protocol.h),
 *     Messung wartet, bis der Head am Ziel steht, Wiederholung bei Paketverlust.
 *
 * Encoder: 0 Pan grob 3°, 1 Pan fein 0,2°, 7 Tilt 0,5°, 6 Punkt wählen (Edit).
 *
 * Testzugang über USB-Seriell (115200), Zeilenbefehle:
 *   pan <deg> | tilt <deg> | enc <i> <delta> | btn <laser|marker|act|scan|step|
 *   edit|undo|save|check> | tap <x> <y> | head <ip|auto> | dump | restart
 *
 * FQBN: m5stack:esp32:m5stack_tab5
 * Libraries: M5Unified, M5GFX, M5UNIT_8Encoder, MyLGFXConfigs (Credentials.h)
 */

#include <M5Unified.h>
#include <UNIT_8ENCODER.h>
#include <WiFi.h>
#include <Credentials.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <math.h>

#include "App.h"
#include "HeadLink.h"

App app;
static HeadLink headLink;
static UNIT_8ENCODER enc;
static Preferences prefs;

static int32_t encPrev[8];
static bool    needRedraw = false;
static bool    wasConnected = false;

static const float PAN_LIMIT = 170.0f, TILT_LIMIT = 30.0f;

// ── Status (auch seriell, für Tests) ────────────────────────────────────────
static void status(uint16_t col, const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uiSetStatus(buf, col);
    Serial.printf("STATUS: %s\n", buf);
}

// ── Messung starten ─────────────────────────────────────────────────────────
static bool beginMeasure(MeasKind kind, bool approach) {
    headLink.setState(app.panDeg, app.tiltDeg, app.laserOn, app.markerOn);
    if (!headLink.startMeasure(approach)) return false;
    app.measKind = kind;
    needRedraw = true;
    return true;
}

// ── Auto-Scan ───────────────────────────────────────────────────────────────
static void scanNext(bool first) {
    app.panDeg = app.scanCur;
    beginMeasure(MK_SCAN, first);      // Anti-Backlash nur beim ersten Punkt (Scan läuft aufwärts)
    status(TFT_YELLOW, "Scanne Pan %.1f (%.1f..%.1f)", app.scanCur, app.scanMin, app.scanMax);
}

static void startAutoScan() {
    if (app.map.count() < 2) return;
    app.scanMin = app.map.minPan();
    app.scanMax = app.map.maxPan();
    if (app.scanMax - app.scanMin < 1.0f) {
        status(TFT_ORANGE, "Pan-Bereich zu klein fuer Auto-Scan!");
        return;
    }
    app.scanCur   = app.scanMin;
    app.scanAdded = 0;
    app.laserOn   = true;
    app.state     = S_AUTO_SCAN;
    scanNext(true);
}

static void scanAdvance() {
    app.scanCur += scanStep();
    if (app.scanCur > app.scanMax + scanStep() * 0.25f) {
        app.state = S_MAP;
        status(TFT_GREEN, "Auto-Scan fertig! %d Punkte hinzugefuegt.", app.scanAdded);
    } else {
        scanNext(false);
    }
    needRedraw = true;
}

// ── Messergebnis ────────────────────────────────────────────────────────────
static void failStatus(const MeasureResult &r) {
    if (r.status == MR_TIMEOUT)              status(TFT_RED,    "Head antwortet nicht.");
    else if (r.status == MDV2_ST_NOSENSOR)   status(TFT_RED,    "Kein Entfernungsmesser am Head!");
    else                                     status(TFT_ORANGE, "Messung ungueltig - kein Echo.");
}

static void onMeasureResult(const MeasureResult &r) {
    MeasKind kind = app.measKind;
    app.measKind  = MK_NONE;
    needRedraw    = true;

    bool ok = (r.status == MDV2_ST_DONE);
    if (ok || r.status == MDV2_ST_FAILED) {
        app.lastValid = ok;
        app.lastDistM = ok ? r.distanceM : 0.0f;
    }
    float horiz = r.distanceM * cosf(r.tiltActDeg * (float)(M_PI / 180.0));

    if (kind == MK_SCAN) {
        if (app.state != S_AUTO_SCAN) return;          // Scan wurde gestoppt
        if (ok) {
            if (app.map.add(r.panActDeg, horiz) == MapModel::ADD_FULL) {
                app.state = S_MAP;
                status(TFT_RED, "Karte voll (%d Punkte) - Scan beendet.", MapModel::MAX_PTS);
                return;
            }
            app.scanAdded++;
        } else {
            failStatus(r);
        }
        scanAdvance();
        return;
    }

    if (!ok) { failStatus(r); return; }

    switch (kind) {
        case MK_ORIGIN:
            app.map.setOrigin(r.panActDeg, horiz);
            app.state = S_CALIB_A;
            status(TFT_GREEN, "Origin OK. Jetzt X-Achsen-Punkt anpeilen.");
            break;
        case MK_AXIS:
            if (app.map.setAxis(r.panActDeg, horiz) != MapModel::CAL_OK) {
                status(TFT_ORANGE, "X-Achsen-Punkt zu nah am Origin!");
                break;
            }
            app.state = S_MAP;
            status(TFT_GREEN, "Kalibriert! Kopf=(%.2f/%.2f)m phi=%.1f",
                   app.map.headX(), app.map.headY(), app.map.headPhi() * 180.0f / (float)M_PI);
            break;
        case MK_POINT: {
            MapModel::AddResult a = app.map.add(r.panActDeg, horiz);
            if (a == MapModel::ADD_FULL) { status(TFT_RED, "Karte voll (%d Punkte)!", MapModel::MAX_PTS); break; }
            float x, y;
            app.map.toWorld(r.panActDeg, horiz, x, y);
            status(TFT_GREEN, "Pt %d:  X=%.2fm  Y=%.2fm", app.map.count(), x, y);
            break;
        }
        case MK_CHECK:
            status(TFT_CYAN, "Check: d=%.3fm  pan_ist=%.2f", r.distanceM, r.panActDeg);
            break;
        default: break;
    }
}

// ── SD-Karte (SPI, NICHT SDMMC: SDMMC teilt sich die Peripherie mit dem WLAN) ─
static void initSD() {
    SPI.begin(43, 39, 44, 42);
    app.sdOk = SD.begin(42, SPI, 4000000);
    if (app.sdOk) status(TFT_GREEN, "SD-Karte OK.");
    else          status(TFT_RED, "SD-Karte nicht erkannt - Speichern deaktiviert.");
}

static void saveMap() {
    const MapModel &m = app.map;
    if (!app.sdOk)     { status(TFT_RED, "SD-Karte nicht bereit!"); return; }
    if (m.count() < 3) { status(TFT_ORANGE, "Mind. 3 Punkte benoetigt!"); return; }

    char fn[24];
    int n = 1;
    do { snprintf(fn, sizeof(fn), "/karte_%03d.csv", n++); } while (SD.exists(fn) && n < 1000);

    File f = SD.open(fn, FILE_WRITE);
    if (!f) { status(TFT_RED, "Datei-Fehler!"); return; }
    f.printf("# PanTiltKarte  Kopf=(%.4f,%.4f)  phi=%.6f\n", m.headX(), m.headY(), m.headPhi());
    f.println("# X,Y");
    for (int i = 0; i < m.count(); i++) f.printf("%.4f,%.4f\n", m.pt(i).x, m.pt(i).y);
    f.close();
    status(TFT_GREEN, "Gespeichert: %s (%d Pkt.)", fn, m.count());
}

// ── Edit ────────────────────────────────────────────────────────────────────
static void enterEdit() {
    if (!app.map.count()) return;
    app.editIdx = 0;
    app.state   = S_EDIT;
    enc.setLEDColor(6, 0x00FF00);
    status(TFT_MAGENTA, "Enc6 = Navigation | Loeschen-Button = Punkt loeschen");
    needRedraw = true;
}

static void exitEdit() {
    app.state = S_MAP;
    enc.setLEDColor(6, 0x000000);
    status(TFT_GREEN, "Edit beendet.");
    needRedraw = true;
}

// ── Eingabe ─────────────────────────────────────────────────────────────────
static bool posFrozen() {
    return app.measKind != MK_NONE || app.state == S_AUTO_SCAN || app.state == S_EDIT;
}

static void setPan(float p)  { app.panDeg  = constrain(p, -PAN_LIMIT, PAN_LIMIT);   needRedraw = true; }
static void setTilt(float t) { app.tiltDeg = constrain(t, -TILT_LIMIT, TILT_LIMIT); needRedraw = true; }

static void onEncoder(int i, int32_t delta) {
    if (!delta) return;
    if (i == 6) {
        if (app.state == S_EDIT && app.map.count()) {
            int n = app.map.count();
            app.editIdx = (app.editIdx + (delta > 0 ? 1 : -1) + n) % n;
            needRedraw = true;
        }
        return;
    }
    if (posFrozen()) return;
    if (i == 0) setPan(app.panDeg + delta * 3.0f);
    if (i == 1) setPan(app.panDeg + delta * 0.2f);
    if (i == 7) setTilt(app.tiltDeg + delta * 0.5f);
}

static void toggleLaser() { app.laserOn = !app.laserOn; needRedraw = true; }
static void toggleMarker() {
    app.markerOn = !app.markerOn;
    status(app.markerOn ? TFT_CYAN : TFT_DARKGREY, app.markerOn ? "Marker EIN" : "Marker AUS");
    needRedraw = true;
}

static void onButton(UiButton b) {
    bool busy = app.measKind != MK_NONE;
    switch (app.state) {
        case S_AUTO_SCAN:
            if (b == BTN_UNDO) {
                app.state = S_MAP;
                status(TFT_ORANGE, "Scan gestoppt. %d Punkte.", app.scanAdded);
                needRedraw = true;
            }
            break;

        case S_EDIT:
            if (b == BTN_LASER)  toggleLaser();
            if (b == BTN_MARKER) toggleMarker();
            if (b == BTN_ACT)    exitEdit();
            if (b == BTN_EDIT && app.map.count()) {
                int idx = app.editIdx;
                app.map.remove(idx);
                if (!app.map.count()) { exitEdit(); break; }
                if (app.editIdx >= app.map.count()) app.editIdx = app.map.count() - 1;
                status(TFT_ORANGE, "Punkt %d geloescht.", idx + 1);
                needRedraw = true;
            }
            break;

        default:   // S_CALIB_O, S_CALIB_A, S_MAP
            if (b == BTN_LASER)  toggleLaser();
            if (b == BTN_MARKER) toggleMarker();
            if (busy) break;
            if (app.state == S_CALIB_O && b == BTN_ACT) beginMeasure(MK_ORIGIN, false);
            if (app.state == S_CALIB_A && b == BTN_ACT) beginMeasure(MK_AXIS, false);
            if (app.state != S_MAP) break;
            if (b == BTN_ACT)       beginMeasure(MK_POINT, false);
            if (b == BTN_AUTO_SCAN) startAutoScan();
            if (b == BTN_SCAN_STEP) {
                app.scanStepIdx = (app.scanStepIdx + 1) % SCAN_STEP_COUNT;
                prefs.putUChar("scanStep", app.scanStepIdx);
                status(TFT_CYAN, "Scan-Schritt %.1f Grad", scanStep());
                needRedraw = true;
            }
            if (b == BTN_EDIT) enterEdit();
            if (b == BTN_UNDO && app.map.canUndo()) { app.map.undo(); needRedraw = true; }
            if (b == BTN_SAVE)  { saveMap(); needRedraw = true; }
            if (b == BTN_CHECK) {
                app.laserOn = true;
                if (beginMeasure(MK_CHECK, true)) status(TFT_YELLOW, "Anfahrt-Check laeuft...");
            }
            break;
    }
}

static void onTap(int x, int y) {
    if (x >= 472) return;            // nur linke Seite hat Knöpfe
    onButton(uiHitTest(x, y));
}

static void pollEncoders() {
    for (int i : {0, 1, 6, 7}) {
        int32_t v = (int32_t)enc.getEncoderValue(i);
        int32_t d = v - encPrev[i];
        encPrev[i] = v;
        onEncoder(i, d);
    }
}

// ── Testzugang (seriell) ────────────────────────────────────────────────────
static void dump() {
    const MapModel &m = app.map;
    Serial.printf("DUMP state=%d meas=%d pan=%.2f tilt=%.2f laser=%d marker=%d step=%.1f head=%s conn=%d sensor=%d\n",
                  app.state, app.measKind, app.panDeg, app.tiltDeg, app.laserOn, app.markerOn, scanStep(),
                  headLink.headIp().toString().c_str(), headLink.connected(), headLink.sensor());
    Serial.printf("DUMP calib=%d O=(%.3f,%.4f) head=(%.4f,%.4f) phi=%.6f last=%d/%.4f\n",
                  m.calibrated(), m.originPan(), m.originDist(), m.headX(), m.headY(), m.headPhi(),
                  app.lastValid, app.lastDistM);
    Serial.printf("DUMP points=%d undo=%d\n", m.count(), m.canUndo());
    for (int i = 0; i < m.count(); i++)
        Serial.printf("PT %d %.3f %.4f %.4f\n", i, m.pt(i).panDeg, m.pt(i).x, m.pt(i).y);
    Serial.println("DUMP end");
}

static void serialCommand(char *line) {
    char *cmd = strtok(line, " \t");
    if (!cmd) return;
    char *a = strtok(nullptr, " \t");
    char *b = strtok(nullptr, " \t");
    if (!strcmp(cmd, "pan") && a)       { if (!posFrozen()) setPan(atof(a)); }
    else if (!strcmp(cmd, "tilt") && a) { if (!posFrozen()) setTilt(atof(a)); }
    else if (!strcmp(cmd, "enc") && a && b) onEncoder(atoi(a), atoi(b));
    else if (!strcmp(cmd, "tap") && a && b) onTap(atoi(a), atoi(b));
    else if (!strcmp(cmd, "head") && a) {
        IPAddress ip;
        if (!strcmp(a, "auto") || !ip.fromString(a)) ip = IPAddress(0, 0, 0, 0);
        headLink.forceHeadIp(ip);
    }
    else if (!strcmp(cmd, "dump")) dump();
    else if (!strcmp(cmd, "restart")) { Serial.println("ok"); Serial.flush(); delay(100); ESP.restart(); }
    else if (!strcmp(cmd, "btn") && a) {
        static const struct { const char *n; UiButton b; } map[] = {
            {"laser", BTN_LASER}, {"marker", BTN_MARKER}, {"act", BTN_ACT}, {"scan", BTN_AUTO_SCAN},
            {"step", BTN_SCAN_STEP}, {"edit", BTN_EDIT}, {"undo", BTN_UNDO}, {"save", BTN_SAVE},
            {"check", BTN_CHECK}};
        for (auto &e : map) if (!strcmp(a, e.n)) onButton(e.b);
    }
    else { Serial.println("? pan|tilt|enc|tap|btn|head|dump|restart"); return; }
    Serial.println("ok");
}

static void pollSerial() {
    static char buf[64];
    static int  len = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') { buf[len] = 0; if (len) serialCommand(buf); len = 0; }
        else if (len < (int)sizeof(buf) - 1) buf[len++] = c;
    }
}

// ── WLAN ────────────────────────────────────────────────────────────────────
static void connectWiFi() {
    status(TFT_YELLOW, "Verbinde WLAN...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 30000) {
            status(TFT_RED, "WLAN fehlgeschlagen! Neustart...");
            delay(3000);
            ESP.restart();
        }
        delay(250);
    }
    status(TFT_GREEN, "WLAN OK  %s", WiFi.localIP().toString().c_str());
}

// ── Setup / Loop ────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    M5.begin();
    if (M5.Display.width() < M5.Display.height())
        M5.Display.setRotation(M5.Display.getRotation() ^ 1);
    M5.Display.setRotation((M5.Display.getRotation() + 2) % 4);
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(TFT_BLACK);
    uiBegin();

    // Unit 8Encoder; hängt der STM32 im Bootloader (0x54), holt ihn 0x77 zurück
    Wire.begin(53, 54, 100000);
    Wire.beginTransmission(ENCODER_ADDR);
    if (Wire.endTransmission() != 0) {
        Wire.beginTransmission(0x54);
        Wire.write(0x77);
        Wire.endTransmission();
        delay(200);
    }
    enc.begin(&Wire, ENCODER_ADDR, 53, 54, 100000);
    for (int i = 0; i < 8; i++) encPrev[i] = (int32_t)enc.getEncoderValue(i);
    enc.setLEDColor(6, 0x000000);

    prefs.begin("mdv2", false);
    app.scanStepIdx = prefs.getUChar("scanStep", 2) % SCAN_STEP_COUNT;

    initSD();
    uiRedraw();
    connectWiFi();
    headLink.begin();
    uiRedraw();
    Serial.println("PanTiltKarte_WM_V2 bereit");
}

void loop() {
    M5.update();

    if (WiFi.status() != WL_CONNECTED) {
        status(TFT_RED, "WLAN verloren - verbinde neu...");
        connectWiFi();
        needRedraw = true;
    }

    pollEncoders();
    pollSerial();
    auto t = M5.Touch.getDetail();
    if (t.wasClicked()) onTap(t.x, t.y);

    headLink.setState(app.panDeg, app.tiltDeg, app.laserOn, app.markerOn);
    MeasureResult r;
    if (headLink.poll(r)) onMeasureResult(r);

    bool conn = headLink.connected();
    if (conn != wasConnected) {
        wasConnected = conn;
        if (conn) {
            static const char *SN[] = {"kein Sensor", "SEN0366", "X40"};
            status(TFT_GREEN, "Head verbunden %s (%s)", headLink.headIp().toString().c_str(),
                   SN[headLink.sensor() < 3 ? headLink.sensor() : 0]);
        } else {
            status(TFT_RED, "Head nicht erreichbar!");
        }
    }

    if (needRedraw) { needRedraw = false; uiRedraw(); }
    delay(10);
}
