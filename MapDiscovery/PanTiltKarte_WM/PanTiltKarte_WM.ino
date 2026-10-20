/*
 * PanTiltKarte_WM — M5Stack Tab5 + Unit 8Encoder  (WM = "With Marker")
 *
 * Wie PanTiltKarte, zusätzlich mit einem Knopf zum Ein-/Ausschalten des
 * Laser-Markers (zur besseren Erkennung des angepeilten Punktes). Wichtig:
 * Diese Karte steuert den Marker NICHT direkt – sie schickt nur das Flag
 * markerOn im ControlMsg. Das eigentliche Ansteuern des Markers (UDP an
 * 192.168.0.182) übernimmt ausschliesslich der PanTiltHead_WM.
 *
 * Erstellt eine 2D-Karte eines Außenbereiches (Garten, Rasenfläche).
 * Kalibrierung über zwei Referenzpunkte (Origin O=(0,0) und X-Achsen-Punkt A).
 * Misst Grenzpunkte mit Laser-Rangefinder, rechnet in globale Koordinaten um,
 * sortiert nach Polarwinkel um den Origin und zeigt sie als Polygon auf der Karte.
 * Speichert die Punkte als CSV auf der SD-Karte.
 *
 * Kommuniziert mit PanTiltHead.ino (unverändert) per WiFi UDP.
 *
 * Hardware:
 *   Board   : M5Stack Tab5 (ESP32-P4 + ESP32-C6 für WiFi)
 *   FQBN    : m5stack:esp32:m5stack_tab5
 *   Encoder : M5Stack Unit 8Encoder an Port A (SDA=53, SCL=54)
 *   SD-Karte: SDMMC (CLK=43, CMD=44, D0-D3=39-42)
 *
 * Bedienung:
 *   Encoder 0 : Pan grob  (+3°/Schritt)
 *   Encoder 1 : Pan fein  (+0.2°/Schritt)
 *   Encoder 7 : Tilt      (+0.5°/Schritt)
 *   Encoder 6 : Punkt-Navigation im Edit-Modus (LED leuchtet grün)
 *   Touch-Buttons: siehe linke Displayhälfte
 */

#include <M5Unified.h>
#include <UNIT_8ENCODER.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <Credentials.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>

// ── WiFi / UDP ───────────────────────────────────────────────────────────────

static const uint16_t UDP_CMD_PORT    = 4210;
static const uint16_t UDP_RESULT_PORT = 4211;
static WiFiUDP udp;

// ── Protokoll (identisch mit PanTiltHead) ────────────────────────────────────

struct __attribute__((packed)) ControlMsg {
    float   panDeg;
    float   tiltDeg;
    uint8_t laserOn;
    uint8_t doMeasure;
    uint8_t approach;    // 1 = Anti-Backlash-Anfahrt (Pan immer von definierter Seite)
    uint8_t markerOn;    // 1 = Laser-Marker an (Head steuert ihn an)
};

struct __attribute__((packed)) ResultMsg {
    float   distanceM;      // Messdistanz in Metern (0 wenn ungültig)
    float   panActualDeg;   // tatsächlicher Pan-Winkel laut Servo-Feedback
    uint8_t laserState;     // aktueller Laser-Zustand (0/1)
    uint8_t valid;          // 1 = Messung gültig, 0 = kein Ergebnis
    uint8_t markerState;    // aktueller Laser-Marker-Zustand (0/1)
};

// ── Zustandsmaschine ─────────────────────────────────────────────────────────

enum AppState { S_CALIB_O, S_CALIB_A, S_MAP, S_AUTO_SCAN, S_EDIT };
static AppState appState = S_CALIB_O;

// ── Steuerungszustand ────────────────────────────────────────────────────────

static float panDeg    = 0.0f;
static float tiltDeg   = 0.0f;
static bool  laserOn   = false;
static bool  markerOn  = false;   // Laser-Marker an/aus (Head steuert ihn)
static bool  needSend  = false;
static bool  doMeas    = false;
static bool  measBusy  = false;
static bool  approachReq = false;  // nächstes Mess-Paket mit Anti-Backlash-Anfahrt
static bool  checkMode   = false;  // Reproduzierbarkeits-Check (Punkt NICHT speichern)
static float lastDistM = 0.0f;
static bool  lastValid = false;

// ── Kalibrierung ─────────────────────────────────────────────────────────────

static float alphaO  = 0.0f, distO  = 0.0f;
static float alphaA  = 0.0f, distA  = 0.0f;
static float headX   = 0.0f;
static float headY   = 0.0f;
static float headPhi = 0.0f;

// ── Grenzpunkte ──────────────────────────────────────────────────────────────

static const int MAX_PTS = 100;

struct MapPt {
    float x, y;
    float pa;      // Kartenwinkel (Polarwinkel) für Sortierung, Radiant
    float panDeg;  // Pan-Winkel bei der Messung
};

static MapPt pts[MAX_PTS];
static int   nPts          = 0;
static float lastPA        = 0.0f;
static bool  hasUndo       = false;
static MapPt undoBuf;
static bool  undoIsReplace = false;

// ── Auto-Scan ─────────────────────────────────────────────────────────────────

static float    scanMinPan    = 0.0f;
static float    scanMaxPan    = 0.0f;
static float    scanCurPan    = 0.0f;
static int      scanPtsAdded  = 0;
static uint32_t scanNextT     = 0;
static bool     scanWaiting   = false;
static uint32_t scanMeasStartT = 0;   // Timeout-Überwachung
static bool     scanNeedApproach = false;  // Anti-Backlash nur beim 1. Scan-Punkt
static const uint32_t SCAN_MEAS_TIMEOUT_MS = 5000;

// ── Edit-Modus ────────────────────────────────────────────────────────────────

static int     editIdx = 0;
static int32_t pe6     = 0;

// ── SD-Karte ─────────────────────────────────────────────────────────────────

static bool sdOk = false;

// ── Encoder ──────────────────────────────────────────────────────────────────

static UNIT_8ENCODER enc;
static int32_t pe0 = 0, pe1 = 0, pe7 = 0;

// ── Display-Konstanten ───────────────────────────────────────────────────────

static const int DW = 1280;
static const int DH = 720;

static const int SPLIT    = 472;
static const int STATUS_H = 30;

static const int BX = 14;
static const int BW = 444;
static const int BH = 62;   // kompakter als zuvor (70), damit 8 Knoepfe passen

// Button Y-Positionen (8 px Abstand → Schrittweite 70 px, 8 Buttons passen)
static const int BTN_LASER_Y     = 114;
static const int BTN_MARKER_Y    = 184;   // NEU: Laser-Marker an/aus
static const int BTN_ACT_Y       = 254;
static const int BTN_AUTO_SCAN_Y = 324;
static const int BTN_EDIT_Y      = 394;
static const int BTN_UNDO_Y      = 464;
static const int BTN_SAVE_Y      = 534;
static const int BTN_CHECK_Y     = 604;

// Kartenbereich (rechte Seite)
static const int ML = SPLIT + 8;
static const int MR = DW - 6;
static const int MT = 6;
static const int MB = DH - 6;
static const int MW = MR - ML;
static const int MH = MB - MT;
static const int MP = 24;

// ── Karten-Skalierung ────────────────────────────────────────────────────────

static float mScale = 1.0f;
static float mOX    = 0.0f;
static float mOY    = 0.0f;

static void calcScale() {
    // y=0 (X-Achse) ist immer die untere Grenze der Karte
    float xmin = min(0.0f, headX);
    float ymin = 0.0f;
    float xmax = max(0.1f, headX);
    float ymax = max(0.1f, headY);

    for (int i = 0; i < nPts; i++) {
        xmin = min(xmin, pts[i].x);
        ymin = min(ymin, pts[i].y);
        xmax = max(xmax, pts[i].x);
        ymax = max(ymax, pts[i].y);
    }
    ymin = min(ymin, 0.0f);

    float rx = xmax - xmin;
    float ry = ymax - ymin;
    if (rx < 0.5f) rx = 0.5f;
    if (ry < 0.5f) ry = 0.5f;

    mScale = min((float)(MW - 2 * MP) / rx, (float)(MH - 2 * MP) / ry);
    mOX    = ML + MP - xmin * mScale;
    mOY    = MT + MP + ymax * mScale;
}

static int sx(float wx) { return (int)(mOX + wx * mScale); }
static int sy(float wy) { return (int)(mOY - wy * mScale); }

// ── Mathematik ───────────────────────────────────────────────────────────────

static void toGlobal(float aDeg, float d, float &gx, float &gy) {
    float dir = headPhi - aDeg * (float)(M_PI / 180.0);
    gx = headX + d * cosf(dir);
    gy = headY + d * sinf(dir);
}

static void computeCalib() {
    float ao = alphaO * (float)(M_PI / 180.0);
    float aa = alphaA * (float)(M_PI / 180.0);

    float num = distO * sinf(ao) - distA * sinf(aa);
    float den = distO * cosf(ao) - distA * cosf(aa);
    headPhi = atan2f(num, den);

    headX = -distO * cosf(headPhi - ao);
    headY = -distO * sinf(headPhi - ao);

    float xA = headX + distA * cosf(headPhi - aa);
    if (xA < 0.0f) {
        headPhi += (float)M_PI;
        headX = -distO * cosf(headPhi - ao);
        headY = -distO * sinf(headPhi - ao);
    }
}

static void sortPts() {
    // Nach Pan-Winkel sortieren: pts[0]=minPan(links), pts[nPts-1]=maxPan(rechts)
    // Das stellt sicher, dass Polygon-Kette und X-Achsen-Abschluss konsistent sind.
    for (int i = 1; i < nPts; i++) {
        MapPt k = pts[i];
        int j = i - 1;
        while (j >= 0 && pts[j].panDeg > k.panDeg) { pts[j + 1] = pts[j]; j--; }
        pts[j + 1] = k;
    }
}

// Punkt hinzufügen. Punkte mit x<0, y>=0 sind zulässig (pa in (π/2, π)).
static void addPt(float x, float y, float pan) {
    float pa = atan2f(y, x);
    const float thr = 0.1f * (float)(M_PI / 180.0);
    int dup = -1;
    for (int i = 0; i < nPts; i++) {
        if (fabsf(pts[i].pa - pa) < thr) { dup = i; break; }
    }
    if (dup >= 0) {
        undoBuf       = pts[dup];
        undoIsReplace = true;
        pts[dup].x      = x;
        pts[dup].y      = y;
        pts[dup].pa     = pa;
        pts[dup].panDeg = pan;
    } else if (nPts < MAX_PTS) {
        undoIsReplace    = false;
        pts[nPts].x      = x;
        pts[nPts].y      = y;
        pts[nPts].pa     = pa;
        pts[nPts].panDeg = pan;
        nPts++;
    } else {
        return;
    }
    lastPA  = pa;
    hasUndo = true;
    sortPts();
}

static void undoPt() {
    if (!hasUndo || nPts == 0) return;
    const float thr = 0.1f * (float)(M_PI / 180.0);
    for (int i = 0; i < nPts; i++) {
        if (fabsf(pts[i].pa - lastPA) < thr) {
            if (undoIsReplace) {
                pts[i] = undoBuf;
            } else {
                for (int j = i; j < nPts - 1; j++) pts[j] = pts[j + 1];
                nPts--;
            }
            hasUndo = false;
            return;
        }
    }
    hasUndo = false;
}

// Beliebigen Punkt nach Index löschen (Edit-Modus)
static void deleteEditPt() {
    if (nPts == 0) return;
    if (editIdx >= nPts) editIdx = nPts - 1;
    for (int j = editIdx; j < nPts - 1; j++) pts[j] = pts[j + 1];
    nPts--;
    hasUndo = false;  // Undo-Puffer ist nach beliebiger Löschung ungültig
    if (editIdx >= nPts && nPts > 0) editIdx = nPts - 1;
}

// ── Status-Zeile ─────────────────────────────────────────────────────────────

static char     statusMsg[80] = "";
static uint16_t statusCol     = TFT_BLACK;

static void drawStatusLine() {
    M5.Display.fillRect(0, DH - STATUS_H, SPLIT, STATUS_H, TFT_BLACK);
    if (statusMsg[0]) {
        M5.Display.setTextColor(statusCol);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(8, DH - STATUS_H + 7);
        M5.Display.print(statusMsg);
    }
}

static void setStatus(const char *msg, uint16_t col) {
    strncpy(statusMsg, msg, sizeof(statusMsg) - 1);
    statusMsg[sizeof(statusMsg) - 1] = '\0';
    statusCol = col;
    drawStatusLine();
}

// ── Buttons ──────────────────────────────────────────────────────────────────

static void drawBtn(int y, const char *lbl, uint16_t col, bool en = true) {
    uint16_t bg = en ? col : (uint16_t)0x2104;
    uint16_t fg = en ? (uint16_t)TFT_WHITE : (uint16_t)0x528A;
    M5.Display.fillRoundRect(BX, y, BW, BH, 10, bg);
    M5.Display.setTextColor(fg);
    M5.Display.setTextSize(3);
    int tw = strlen(lbl) * 18;
    M5.Display.setCursor(BX + (BW - tw) / 2, y + (BH - 24) / 2);
    M5.Display.print(lbl);
}

static inline bool inBtn(int tx, int ty, int by) {
    return tx >= BX && tx <= BX + BW && ty >= by && ty <= by + BH;
}

// ── Linke Seite ───────────────────────────────────────────────────────────────

static void drawLeft() {
    M5.Display.fillRect(0, 0, SPLIT, DH - STATUS_H, TFT_BLACK);

    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(8, 4);
    M5.Display.print("PAN/TILT KARTE");

    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 34);
    if (appState == S_CALIB_O)
        M5.Display.print("Schritt 1: Origin messen");
    else if (appState == S_CALIB_A)
        M5.Display.print("Schritt 2: X-Achse messen");
    else if (appState == S_AUTO_SCAN)
        M5.Display.print("Auto-Scan laeuft...");
    else if (appState == S_EDIT)
        M5.Display.printf("Edit-Modus  Enc6=Navigation");
    else
        M5.Display.printf("Messmodus  %d/%d Punkte", nPts, MAX_PTS);

    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(8, 56);
    M5.Display.printf("Pan%+6.1f  Tilt%+5.1f", panDeg, tiltDeg);

    M5.Display.fillRect(0, 84, SPLIT, 26, TFT_BLACK);
    if (lastValid) {
        M5.Display.setTextColor(TFT_YELLOW);
        M5.Display.setTextSize(3);
        M5.Display.setCursor(8, 86);
        if (lastDistM < 10.0f)
            M5.Display.printf("%.0f mm", lastDistM * 1000.0f);
        else
            M5.Display.printf("%.2f m", lastDistM);
    } else {
        M5.Display.setTextColor(0x4208);
        M5.Display.setTextSize(3);
        M5.Display.setCursor(190, 86);
        M5.Display.print("---");
    }

    if (appState == S_AUTO_SCAN) {
        drawBtn(BTN_LASER_Y,     "LASER AUS/EIN", 0x2104, false);
        drawBtn(BTN_MARKER_Y,    markerOn ? "MARKER EIN" : "MARKER AUS", 0x2104, false);
        drawBtn(BTN_ACT_Y,       "SCAN LAEUFT...", TFT_ORANGE, false);
        drawBtn(BTN_UNDO_Y,      "SCAN STOPPEN",   TFT_RED,    true);

        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(8, BTN_ACT_Y + BH + 8);
        M5.Display.printf("Pan %.0f  von %.0f bis %.0f",
                          scanCurPan, scanMinPan, scanMaxPan);
        M5.Display.setCursor(8, BTN_ACT_Y + BH + 28);
        M5.Display.printf("%d neue Punkte hinzugefuegt", scanPtsAdded);

    } else if (appState == S_EDIT) {
        drawBtn(BTN_LASER_Y, laserOn ? "LASER AUS" : "LASER EIN",
                laserOn ? (uint16_t)0x03C0 : (uint16_t)0x7800);
        drawBtn(BTN_MARKER_Y, markerOn ? "MARKER AUS" : "MARKER EIN",
                markerOn ? (uint16_t)0x05FF : (uint16_t)0x7800);
        drawBtn(BTN_ACT_Y,   "EDIT BEENDEN", (uint16_t)0x0460);
        drawBtn(BTN_EDIT_Y,  "PUNKT LOESCHEN",
                nPts > 0 ? (uint16_t)0x7800 : (uint16_t)0x2104, nPts > 0);

        if (nPts > 0 && editIdx < nPts) {
            M5.Display.setTextColor(TFT_MAGENTA);
            M5.Display.setTextSize(3);
            M5.Display.setCursor(8, BTN_ACT_Y + BH + 10);
            M5.Display.printf("Pkt %d/%d", editIdx + 1, nPts);
            M5.Display.setTextColor(TFT_WHITE);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(8, BTN_ACT_Y + BH + 40);
            M5.Display.printf("X=%.2fm  Y=%.2fm", pts[editIdx].x, pts[editIdx].y);
            M5.Display.setCursor(8, BTN_ACT_Y + BH + 60);
            M5.Display.printf("Pan=%.1f°", pts[editIdx].panDeg);
        } else {
            M5.Display.setTextColor(0x4208);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(8, BTN_ACT_Y + BH + 10);
            M5.Display.print("Keine Punkte vorhanden");
        }

    } else {
        drawBtn(BTN_LASER_Y, laserOn ? "LASER AUS" : "LASER EIN",
                laserOn ? (uint16_t)0x03C0 : (uint16_t)0x7800);
        drawBtn(BTN_MARKER_Y, markerOn ? "MARKER AUS" : "MARKER EIN",
                markerOn ? (uint16_t)0x05FF : (uint16_t)0x7800);

        if (appState == S_CALIB_O) {
            drawBtn(BTN_ACT_Y, measBusy ? "MESSE..." : "ORIGIN MESSEN",
                    measBusy ? (uint16_t)TFT_ORANGE : (uint16_t)TFT_NAVY, !measBusy);

        } else if (appState == S_CALIB_A) {
            drawBtn(BTN_ACT_Y, measBusy ? "MESSE..." : "X-ACHSE MESSEN",
                    measBusy ? (uint16_t)TFT_ORANGE : (uint16_t)TFT_NAVY, !measBusy);
            M5.Display.setTextColor(TFT_DARKGREY);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(8, BTN_ACT_Y + BH + 6);
            M5.Display.printf("Origin: Pan=%.1f  d=%.3fm", alphaO, distO);

        } else {  // S_MAP
            drawBtn(BTN_ACT_Y, measBusy ? "MESSE..." : "PUNKT MESSEN",
                    measBusy ? (uint16_t)TFT_ORANGE : (uint16_t)0x000F, !measBusy);

            bool canScan = (nPts >= 2 && !measBusy);
            drawBtn(BTN_AUTO_SCAN_Y, "AUTO SCAN",
                    canScan ? (uint16_t)0x4810 : (uint16_t)0x2104, canScan);

            drawBtn(BTN_EDIT_Y, "EDIT",
                    nPts > 0 ? (uint16_t)0x6008 : (uint16_t)0x2104, nPts > 0);

            drawBtn(BTN_UNDO_Y, "LETZTEN LOESCHEN",
                    hasUndo ? (uint16_t)0x7800 : (uint16_t)0x2104, hasUndo);
            drawBtn(BTN_SAVE_Y, "KARTE SPEICHERN",
                    (uint16_t)0x0460, sdOk && nPts >= 3);

            drawBtn(BTN_CHECK_Y, measBusy ? "MESSE..." : "ANFAHRT PRUEFEN",
                    measBusy ? (uint16_t)TFT_ORANGE : (uint16_t)0x780F, !measBusy);

            M5.Display.setTextColor(0x4208);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(8, BTN_CHECK_Y + BH + 4);
            M5.Display.printf("Kopf:(%.2f/%.2fm) phi=%.1f",
                              headX, headY, headPhi * 180.0f / (float)M_PI);
        }
    }

    M5.Display.drawLine(SPLIT, 0, SPLIT, DH, TFT_DARKGREY);
}

// ── Karte (rechte Seite) ─────────────────────────────────────────────────────

static void drawMap() {
    M5.Display.fillRect(ML, MT, MW, MH, TFT_BLACK);
    M5.Display.drawRect(ML, MT, MW, MH, 0x2945);

    if (appState == S_CALIB_O || appState == S_CALIB_A) {
        M5.Display.setTextColor(0x4208);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(ML + 10, MT + MH / 2 - 8);
        M5.Display.print(appState == S_CALIB_O
            ? "Schritt 1: Origin messen..."
            : "Schritt 2: X-Achse messen...");
        return;
    }

    calcScale();

    // Koordinatengitter
    int osx = constrain(sx(0.0f), ML, MR);
    int osy = constrain(sy(0.0f), MT, MB);
    M5.Display.drawLine(ML, osy, MR, osy, 0x2945);
    M5.Display.drawLine(osx, MT, osx, MB, 0x2945);

    // X-Achse + Polygon (nach Pan-Winkel sortiert: pts[0]=links, pts[nPts-1]=rechts)
    if (nPts > 0) {
        // Direkt aus sort: pts[0]=minPan(links), pts[nPts-1]=maxPan(rechts)
        int xProjL = constrain(sx(pts[0].x),        ML, MR);
        int xProjR = constrain(sx(pts[nPts - 1].x), ML, MR);

        // Weisse X-Achse (3 px dick) – zuerst zeichnen
        for (int dy = -1; dy <= 1; dy++)
            M5.Display.drawLine(xProjL, osy + dy, xProjR, osy + dy, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(constrain(xProjR + 5, ML, MR - 14),
                             constrain(osy - 14, MT, MB));
        M5.Display.print("X");

        // Grünes Polygon: obere Grenze links→rechts, Abschluss über X-Achse zurück
        if (nPts >= 2) {
            for (int i = 0; i < nPts - 1; i++) {
                M5.Display.drawLine(
                    constrain(sx(pts[i].x),     ML, MR), constrain(sy(pts[i].y),     MT, MB),
                    constrain(sx(pts[i + 1].x), ML, MR), constrain(sy(pts[i + 1].y), MT, MB),
                    TFT_GREEN);
            }
            // rechter Rand: pts[nPts-1] → rechte Projektion
            M5.Display.drawLine(
                constrain(sx(pts[nPts - 1].x), ML, MR), constrain(sy(pts[nPts - 1].y), MT, MB),
                xProjR, osy, TFT_GREEN);
            // entlang X-Achse: rechts → links
            M5.Display.drawLine(xProjR, osy, xProjL, osy, TFT_GREEN);
            // linker Rand: linke Projektion → pts[0]
            M5.Display.drawLine(
                xProjL, osy,
                constrain(sx(pts[0].x), ML, MR), constrain(sy(pts[0].y), MT, MB),
                TFT_GREEN);
        }

        // Projizierte Punkte auf X-Achse (cyan)
        M5.Display.fillCircle(xProjL, osy, 5, TFT_CYAN);
        M5.Display.fillCircle(xProjR, osy, 5, TFT_CYAN);

        // Gemessene Punkte
        for (int i = 0; i < nPts; i++) {
            int px = constrain(sx(pts[i].x), ML + 4, MR - 4);
            int py = constrain(sy(pts[i].y), MT + 4, MB - 4);
            if (appState == S_EDIT && i == editIdx) {
                // Ausgewählter Punkt im Edit-Modus: größerer Magenta-Ring
                M5.Display.drawCircle(px, py, 10, TFT_MAGENTA);
                M5.Display.drawCircle(px, py, 11, TFT_MAGENTA);
                M5.Display.fillCircle(px, py, 6, TFT_MAGENTA);
            } else {
                M5.Display.fillCircle(px, py, 5, TFT_YELLOW);
            }
        }
    }

    // Origin O
    M5.Display.drawCircle(osx, osy, 7, TFT_CYAN);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(constrain(osx + 10, ML, MR - 16),
                         constrain(osy - 18, MT, MB));
    M5.Display.print("O");

    // Pan/Tilt-Kopf + Richtungspfeil (nicht im Edit-Modus)
    if (appState != S_EDIT) {
        int hx = constrain(sx(headX), ML + 6, MR - 6);
        int hy = constrain(sy(headY), MT + 6, MB - 6);
        float dir = headPhi - panDeg * (float)(M_PI / 180.0);
        int ax = hx + (int)(50.0f * cosf(dir));
        int ay = hy - (int)(50.0f * sinf(dir));
        M5.Display.drawLine(hx, hy, ax, ay, TFT_RED);
        M5.Display.drawLine(hx + 1, hy, ax + 1, ay, TFT_RED);
        M5.Display.fillCircle(hx, hy, 7, TFT_RED);
    }

    // Maßstab
    M5.Display.setTextColor(0x4208);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(ML + 4, MB - 20);
    M5.Display.printf("%d Pkt | %.2f m/px", nPts, 1.0f / mScale);
}

// ── Vollständiger Neuaufbau ───────────────────────────────────────────────────

static void redraw() {
    drawLeft();
    drawMap();
    drawStatusLine();
}

// ── Edit-Modus ein/aus ───────────────────────────────────────────────────────

static void enterEdit() {
    if (nPts == 0) return;
    editIdx  = 0;
    appState = S_EDIT;
    pe6 = (int32_t)enc.getEncoderValue(6);
    enc.setLEDColor(6, 0x00FF00);  // Enc-6-LED grün
    setStatus("Enc6 = Navigation | Loeschen-Button = Punkt loeschen", TFT_MAGENTA);
    redraw();
}

static void exitEdit() {
    appState = S_MAP;
    enc.setLEDColor(6, 0x000000);  // Enc-6-LED aus
    setStatus("Edit beendet.", TFT_GREEN);
    redraw();
}

// ── Auto-Scan starten ─────────────────────────────────────────────────────────

static void startAutoScan() {
    if (nPts < 2) return;

    scanMinPan = pts[0].panDeg;
    scanMaxPan = pts[0].panDeg;
    for (int i = 1; i < nPts; i++) {
        if (pts[i].panDeg < scanMinPan) scanMinPan = pts[i].panDeg;
        if (pts[i].panDeg > scanMaxPan) scanMaxPan = pts[i].panDeg;
    }
    if (scanMaxPan - scanMinPan < 1.0f) {
        setStatus("Pan-Bereich zu klein fuer Auto-Scan!", TFT_ORANGE);
        return;
    }

    scanCurPan    = scanMinPan;
    scanPtsAdded  = 0;
    appState      = S_AUTO_SCAN;
    laserOn       = true;
    panDeg        = scanCurPan;
    needSend      = true;
    scanNextT     = millis() + 800;
    scanWaiting   = true;
    scanMeasStartT = 0;
    scanNeedApproach = true;   // einmalige Anti-Backlash-Anfahrt zu Scan-Beginn
    redraw();
}

// ── SD-Karte (SPI-Modus) ──────────────────────────────────────────────────────
// SD-Karte wird über SPI (SD.h) angesprochen, NICHT über SDMMC (SD_MMC.h).
// Grund: SDMMC Slot 0 = WiFi-SDIO (ESP32-C6), Slot 1 = SD; beide teilen
// dasselbe SDMMC-Peripheral. SD_MMC.begin/end() nach WiFi-Start zerstört
// die WiFi-SDIO-Verbindung (H_SDIO_DRV Unrecoverable Crash).
// SPI2 nutzt die gleichen Pins (SCK=43, MISO=39, MOSI=44, CS=42) über den
// GPIO-Matrix-Router – völlig unabhängig vom SDMMC-Peripheral.

static bool sdMount() {
    SPI.begin(43 /*SCK*/, 39 /*MISO*/, 44 /*MOSI*/, 42 /*CS*/);
    return SD.begin(42 /*CS*/, SPI, 4000000);
}

static void initSD() {
    if (sdMount()) {
        sdOk = true;
        setStatus("SD-Karte OK.", TFT_GREEN);
    } else {
        sdOk = false;
        setStatus("SD-Karte nicht erkannt – Speichern deaktiviert.", TFT_RED);
    }
}

static void saveMap() {
    if (!sdOk) {
        setStatus("SD-Karte nicht bereit!", TFT_RED);
        return;
    }
    if (nPts < 3) {
        setStatus("Mind. 3 Punkte benoetigt!", TFT_ORANGE);
        return;
    }

    char fn[44];
    int  n = 1;
    do { snprintf(fn, sizeof(fn), "/karte_%03d.csv", n++); }
    while (SD.exists(fn) && n < 1000);

    File f = SD.open(fn, FILE_WRITE);
    if (!f) {
        setStatus("Datei-Fehler!", TFT_RED);
        return;
    }

    f.printf("# PanTiltKarte  Kopf=(%.4f,%.4f)  phi=%.6f\n",
             headX, headY, headPhi);
    f.println("# X,Y");
    for (int i = 0; i < nPts; i++)
        f.printf("%.4f,%.4f\n", pts[i].x, pts[i].y);
    f.close();

    char msg[80];
    snprintf(msg, sizeof(msg), "Gespeichert: %s (%d Pkt.)", fn, nPts);
    setStatus(msg, TFT_GREEN);
}

// ── WiFi ─────────────────────────────────────────────────────────────────────

static void connectWiFi() {
    setStatus("Verbinde WLAN...", TFT_YELLOW);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 30000) {
            setStatus("WLAN fehlgeschlagen! Neustart...", TFT_RED);
            delay(3000);
            ESP.restart();
        }
        delay(500);
    }
    udp.begin(UDP_RESULT_PORT);
    char buf[52];
    snprintf(buf, sizeof(buf), "WLAN OK  %s", WiFi.localIP().toString().c_str());
    setStatus(buf, TFT_GREEN);
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    M5.begin();

    if (M5.Display.width() < M5.Display.height())
        M5.Display.setRotation(M5.Display.getRotation() ^ 1);
    M5.Display.setRotation((M5.Display.getRotation() + 2) % 4);
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(TFT_BLACK);

    Wire.begin(53, 54, 100000);
    Wire.beginTransmission(ENCODER_ADDR);
    if (Wire.endTransmission() != 0) {
        Wire.beginTransmission(0x54);
        Wire.write(0x77);
        Wire.endTransmission();
        delay(200);
    }
    enc.begin(&Wire, ENCODER_ADDR, 53, 54, 100000);
    pe0 = (int32_t)enc.getEncoderValue(0);
    pe1 = (int32_t)enc.getEncoderValue(1);
    pe6 = (int32_t)enc.getEncoderValue(6);
    pe7 = (int32_t)enc.getEncoderValue(7);
    enc.setLEDColor(6, 0x000000);  // Enc-6-LED aus

    initSD();
    redraw();
    connectWiFi();
    redraw();
    needSend = true;
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    M5.update();

    if (WiFi.status() != WL_CONNECTED) {
        setStatus("WLAN verloren – verbinde neu...", TFT_RED);
        connectWiFi();
        redraw();
    }

    // ── Encoder 6: Punkt-Navigation im Edit-Modus ────────────────────────────
    if (appState == S_EDIT && nPts > 0) {
        int32_t e6 = (int32_t)enc.getEncoderValue(6);
        if (e6 != pe6) {
            // Pro Rastschritt (2er-Sprung) genau 1 Punkt weiter/zurück
            int dir = (e6 > pe6) ? 1 : -1;
            editIdx = (editIdx + dir + nPts) % nPts;
            pe6 = e6;
            redraw();
        }
    }

    // ── Encoder 0/1/7: Pan/Tilt (nicht während Auto-Scan oder Edit) ──────────
    if (appState != S_AUTO_SCAN && appState != S_EDIT) {
        int32_t e0 = (int32_t)enc.getEncoderValue(0);
        int32_t e1 = (int32_t)enc.getEncoderValue(1);
        int32_t e7 = (int32_t)enc.getEncoderValue(7);
        bool posChg = false;
        if (e0 != pe0) { panDeg  += (e0 - pe0) * 3.0f;  pe0 = e0; posChg = true; }
        if (e1 != pe1) { panDeg  += (e1 - pe1) * 0.2f;  pe1 = e1; posChg = true; }
        if (e7 != pe7) { tiltDeg += (e7 - pe7) * 0.5f;  pe7 = e7; posChg = true; }
        panDeg  = constrain(panDeg,  -170.0f, 170.0f);
        tiltDeg = constrain(tiltDeg,  -30.0f,  30.0f);
        if (posChg) { needSend = true; redraw(); }
    }

    // ── Touch-Buttons ────────────────────────────────────────────────────────
    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int tx = touch.x, ty = touch.y;
        if (tx < SPLIT) {
            if (appState == S_AUTO_SCAN) {
                if (inBtn(tx, ty, BTN_UNDO_Y)) {
                    appState    = S_MAP;
                    measBusy    = false;
                    scanWaiting = false;
                    char buf[48];
                    snprintf(buf, sizeof(buf), "Scan gestoppt. %d Punkte.", scanPtsAdded);
                    setStatus(buf, TFT_ORANGE);
                    redraw();
                }

            } else if (appState == S_EDIT) {
                if (inBtn(tx, ty, BTN_LASER_Y)) {
                    laserOn  = !laserOn;
                    needSend = true;
                    redraw();
                }
                if (inBtn(tx, ty, BTN_MARKER_Y)) {
                    markerOn = !markerOn;
                    needSend = true;
                    setStatus(markerOn ? "Marker EIN" : "Marker AUS",
                              markerOn ? TFT_CYAN : TFT_DARKGREY);
                    redraw();
                }
                if (inBtn(tx, ty, BTN_ACT_Y)) {
                    exitEdit();
                }
                if (inBtn(tx, ty, BTN_EDIT_Y) && nPts > 0) {
                    char buf[48];
                    snprintf(buf, sizeof(buf), "Punkt %d geloescht.", editIdx + 1);
                    deleteEditPt();
                    if (nPts == 0) exitEdit();
                    else { setStatus(buf, TFT_ORANGE); redraw(); }
                }

            } else {
                if (inBtn(tx, ty, BTN_LASER_Y)) {
                    laserOn  = !laserOn;
                    needSend = true;
                    redraw();
                }
                if (inBtn(tx, ty, BTN_MARKER_Y)) {
                    // Marker umschalten – der Head schaltet ihn tatsaechlich.
                    markerOn = !markerOn;
                    needSend = true;
                    setStatus(markerOn ? "Marker EIN" : "Marker AUS",
                              markerOn ? TFT_CYAN : TFT_DARKGREY);
                    redraw();
                }
                if (appState == S_CALIB_O) {
                    if (inBtn(tx, ty, BTN_ACT_Y) && !measBusy) {
                        doMeas = true; measBusy = true; needSend = true;
                        drawLeft();
                    }
                } else if (appState == S_CALIB_A) {
                    if (inBtn(tx, ty, BTN_ACT_Y) && !measBusy) {
                        doMeas = true; measBusy = true; needSend = true;
                        drawLeft();
                    }
                } else {  // S_MAP
                    if (inBtn(tx, ty, BTN_ACT_Y) && !measBusy) {
                        doMeas = true; measBusy = true; needSend = true;
                        drawLeft();
                    }
                    if (inBtn(tx, ty, BTN_AUTO_SCAN_Y) && nPts >= 2 && !measBusy) {
                        startAutoScan();
                    }
                    if (inBtn(tx, ty, BTN_EDIT_Y) && nPts > 0) {
                        enterEdit();
                    }
                    if (inBtn(tx, ty, BTN_UNDO_Y) && hasUndo) {
                        undoPt();
                        redraw();
                    }
                    if (inBtn(tx, ty, BTN_SAVE_Y)) {
                        saveMap();
                        drawLeft();
                    }
                    if (inBtn(tx, ty, BTN_CHECK_Y) && !measBusy) {
                        // Reproduzierbarkeits-Check: aktuelle Pan-Position per
                        // definierter Anfahrt erneut anfahren und messen,
                        // OHNE den Punkt zu speichern.
                        doMeas      = true;  measBusy = true;  needSend = true;
                        approachReq = true;  checkMode = true;
                        laserOn     = true;  // sicherstellen, dass gemessen werden kann
                        setStatus("Anfahrt-Check laeuft...", TFT_YELLOW);
                        drawLeft();
                    }
                }
            }
        }
    }

    // ── Auto-Scan: Messung nach Einschwingezeit auslösen ────────────────────
    if (scanWaiting && millis() >= scanNextT) {
        scanWaiting    = false;
        doMeas         = true;
        measBusy       = true;
        approachReq      = scanNeedApproach;  // nur beim 1. Punkt nötig (Scan ist monoton)
        scanNeedApproach = false;
        needSend       = true;
        scanMeasStartT = millis();
        char buf[52];
        snprintf(buf, sizeof(buf), "Scanne Pan %.0f° (%.0f..%.0f)°",
                 scanCurPan, scanMinPan, scanMaxPan);
        setStatus(buf, TFT_YELLOW);
    }

    // ── Auto-Scan Timeout: hängende Messung überspringen ────────────────────
    if (appState == S_AUTO_SCAN && measBusy && !scanWaiting &&
        scanMeasStartT > 0 && millis() - scanMeasStartT > SCAN_MEAS_TIMEOUT_MS) {
        measBusy = false;
        setStatus("Timeout – Punkt uebersprungen.", TFT_ORANGE);
        scanCurPan += 2.0f;
        if (scanCurPan > scanMaxPan + 0.5f) {
            appState = S_MAP;
            char buf[64];
            snprintf(buf, sizeof(buf), "Auto-Scan fertig! %d Punkte hinzugefuegt.", scanPtsAdded);
            setStatus(buf, TFT_GREEN);
        } else {
            panDeg         = scanCurPan;
            needSend       = true;
            scanNextT      = millis() + 800;
            scanWaiting    = true;
            scanMeasStartT = 0;
        }
        redraw();
    }

    // ── UDP senden ───────────────────────────────────────────────────────────
    if (needSend) {
        ControlMsg msg;
        msg.panDeg    = panDeg;
        msg.tiltDeg   = tiltDeg;
        msg.laserOn   = laserOn ? 1 : 0;
        msg.doMeasure = doMeas      ? 1 : 0;
        msg.approach  = approachReq ? 1 : 0;
        msg.markerOn  = markerOn    ? 1 : 0;
        udp.beginPacket("255.255.255.255", UDP_CMD_PORT);
        udp.write((uint8_t *)&msg, sizeof(msg));
        udp.endPacket();
        needSend    = false;
        doMeas      = false;
        approachReq = false;
    }

    // ── UDP empfangen ────────────────────────────────────────────────────────
    int n = udp.parsePacket();
    if (n == sizeof(ResultMsg)) {
        ResultMsg r;
        udp.read((uint8_t *)&r, sizeof(r));
        // wasBusy merken BEVOR es gelöscht wird:
        // Head antwortet auf JEDEN ControlMsg, nicht nur auf doMeasure=1
        // (auch reine Positions-, Laser- oder Marker-Befehle erzeugen ein Echo).
        // Nur wenn wir measBusy=true gesetzt hatten, war das eine echte Messung.
        bool wasBusy = measBusy;
        measBusy = false;
        bool wasCheck = checkMode;  // Reproduzierbarkeits-Check?
        checkMode = false;

        // Distanz-Anzeige nur bei echter Messung aktualisieren, sonst bleibt der
        // letzte Messwert stehen (statt bei jedem Positions-/Marker-Echo zu löschen).
        if (wasBusy) {
            lastDistM = r.distanceM;
            lastValid = (bool)r.valid;
        }
        // Laser-/Marker-Anzeige mit dem vom Head gemeldeten Ist-Zustand abgleichen.
        // Im Auto-Scan Laser NICHT übernehmen – r.laserState kann 0 sein
        // (Rangefinder schaltet nach Messung ab), was sonst alle folgenden
        // Messungen ungültig machen würde.
        if (appState != S_AUTO_SCAN) {
            laserOn  = (bool)r.laserState;
            markerOn = (bool)r.markerState;
        }

        if (appState == S_AUTO_SCAN) {
            if (wasBusy) {
                // Echte Messantwort – Ergebnis auswerten und zur nächsten Position
                if (r.valid) {
                    float gx, gy;
                    toGlobal(r.panActualDeg, r.distanceM, gx, gy);
                    addPt(gx, gy, r.panActualDeg);
                    scanPtsAdded++;
                }
                scanCurPan += 2.0f;
                if (scanCurPan > scanMaxPan + 0.5f) {
                    appState = S_MAP;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Auto-Scan fertig! %d Punkte hinzugefuegt.", scanPtsAdded);
                    setStatus(buf, TFT_GREEN);
                } else {
                    panDeg         = scanCurPan;
                    needSend       = true;
                    scanNextT      = millis() + 800;
                    scanWaiting    = true;
                    scanMeasStartT = 0;
                }
                redraw();
            }
            // wasBusy=false: Antwort auf Pan-Befehl (doMeasure=0) → ignorieren

        } else if (wasBusy && r.valid) {
            if (appState == S_CALIB_O) {
                alphaO   = r.panActualDeg;
                distO    = r.distanceM;
                appState = S_CALIB_A;
                setStatus("Origin OK. Jetzt X-Achsen-Punkt anpeilen.", TFT_GREEN);
            } else if (appState == S_CALIB_A) {
                alphaA = r.panActualDeg;
                distA  = r.distanceM;
                computeCalib();
                appState = S_MAP;
                char buf[72];
                snprintf(buf, sizeof(buf), "Kalibriert! Kopf=(%.2f/%.2f)m phi=%.1f",
                         headX, headY, headPhi * 180.0f / (float)M_PI);
                setStatus(buf, TFT_GREEN);
            } else if (appState == S_MAP) {
                if (wasCheck) {
                    // Reproduzierbarkeits-Check: nur anzeigen, NICHT speichern
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Check: d=%.3fm  pan_ist=%.2f°",
                             r.distanceM, r.panActualDeg);
                    setStatus(buf, TFT_CYAN);
                } else {
                    float gx, gy;
                    toGlobal(r.panActualDeg, r.distanceM, gx, gy);
                    addPt(gx, gy, r.panActualDeg);
                    char buf[56];
                    snprintf(buf, sizeof(buf), "Pt %d:  X=%.2fm  Y=%.2fm", nPts, gx, gy);
                    setStatus(buf, TFT_GREEN);
                }
            }
            redraw();
        } else if (wasBusy) {
            // Messung angefordert, aber kein gültiges Echo erhalten
            setStatus("Messung ungueltig – kein Echo.", TFT_ORANGE);
            redraw();
        }
        // sonst: reines Positions-/Laser-/Marker-Echo (doMeasure=0) → ignorieren
    }

    delay(20);
}
