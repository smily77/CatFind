// Ui.cpp — Anzeige der Karte (Layout wie PanTiltKarte_WM V1)
//
// Linke Seite: Titel, Schritt, Pan/Tilt, letzte Distanz, Knöpfe.
// Rechte Seite: Karte. Beide werden in einem Puffer (Sprite im PSRAM)
// gezeichnet und dann am Stück übertragen -> kein Flackern.
#include <M5Unified.h>
#include <math.h>
#include "App.h"

// ── Layout (1280 x 720, quer) ───────────────────────────────────────────────
static const int DW = 1280, DH = 720;
static const int SPLIT = 472, STATUS_H = 30;
static const int BX = 14, BW = 444, BH = 62;

static const int Y_LASER = 114, Y_MARKER = 184, Y_ACT = 254, Y_AUTO = 324;
static const int Y_EDIT = 394, Y_UNDO = 464, Y_SAVE = 534, Y_CHECK = 604;

// Auto-Scan-Zeile geteilt: AUTO SCAN + Schrittweite
static const int STEP_W = 120, GAP = 8;
static const int AUTO_W = BW - STEP_W - GAP;
static const int STEP_X = BX + AUTO_W + GAP;

// Kartenbereich
static const int ML = SPLIT + 8, MR = DW - 6, MT = 6, MB = DH - 6;
static const int MW = MR - ML, MH = MB - MT;
static const int MP = 24;

static const uint16_t C_DISABLED = 0x2104, C_DIS_TXT = 0x528A, C_GRID = 0x2945, C_DIM = 0x4208;

static M5Canvas leftCv(&M5.Display);
static M5Canvas mapCv(&M5.Display);
static bool leftOk = false, mapOk = false;

static char     statusMsg[96] = "";
static uint16_t statusCol = TFT_BLACK;

// Ziel der Zeichenbefehle: Sprite (Ursprung 0/0) oder direkt Display (Versatz)
struct Gfx { LovyanGFX *g; int ox, oy; };

void uiBegin() {
    leftCv.setPsram(true);
    mapCv.setPsram(true);
    leftOk = leftCv.createSprite(SPLIT, DH - STATUS_H) != nullptr;
    mapOk  = mapCv.createSprite(MW, MH) != nullptr;
}

// ── Statuszeile ─────────────────────────────────────────────────────────────
static void drawStatus() {
    M5.Display.fillRect(0, DH - STATUS_H, SPLIT, STATUS_H, TFT_BLACK);
    if (!statusMsg[0]) return;
    M5.Display.setTextColor(statusCol);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, DH - STATUS_H + 7);
    M5.Display.print(statusMsg);
}

void uiSetStatus(const char *msg, uint16_t col) {
    strncpy(statusMsg, msg, sizeof(statusMsg) - 1);
    statusMsg[sizeof(statusMsg) - 1] = 0;
    statusCol = col;
    drawStatus();
}

// ── Knöpfe ──────────────────────────────────────────────────────────────────
static void btn(Gfx &d, int x, int y, int w, const char *lbl, uint16_t col, bool en = true) {
    uint16_t bg = en ? col : C_DISABLED;
    uint16_t fg = en ? (uint16_t)TFT_WHITE : C_DIS_TXT;
    d.g->fillRoundRect(d.ox + x, d.oy + y, w, BH, 10, bg);
    d.g->setTextColor(fg);
    d.g->setTextSize(3);
    int tw = strlen(lbl) * 18;
    d.g->setCursor(d.ox + x + (w - tw) / 2, d.oy + y + (BH - 24) / 2);
    d.g->print(lbl);
}
static void btn(Gfx &d, int y, const char *lbl, uint16_t col, bool en = true) {
    btn(d, BX, y, BW, lbl, col, en);
}

// Schrittweiten-Knopf: Zahl + kleiner Grad-Kringel
static void stepBtn(Gfx &d, bool en) {
    char s[8];
    float st = scanStep();
    if (st < 1.0f) snprintf(s, sizeof(s), "%.1f", st);
    else           snprintf(s, sizeof(s), "%.0f", st);
    uint16_t bg = en ? (uint16_t)0x4810 : C_DISABLED;
    uint16_t fg = en ? (uint16_t)TFT_WHITE : C_DIS_TXT;
    int x = d.ox + STEP_X, y = d.oy + Y_AUTO;
    d.g->fillRoundRect(x, y, STEP_W, BH, 10, bg);
    d.g->drawRoundRect(x, y, STEP_W, BH, 10, fg);
    d.g->setTextColor(fg);
    d.g->setTextSize(3);
    int tw = strlen(s) * 18 + 10;
    int tx = x + (STEP_W - tw) / 2, ty = y + (BH - 24) / 2;
    d.g->setCursor(tx, ty);
    d.g->print(s);
    d.g->drawCircle(tx + tw - 4, ty + 3, 3, fg);
    d.g->drawCircle(tx + tw - 4, ty + 3, 4, fg);
}

UiButton uiHitTest(int x, int y) {
    if (x < BX || x > BX + BW) return BTN_NONE;
    auto in = [&](int by) { return y >= by && y <= by + BH; };
    if (in(Y_LASER))  return BTN_LASER;
    if (in(Y_MARKER)) return BTN_MARKER;
    if (in(Y_ACT))    return BTN_ACT;
    if (in(Y_AUTO))   return (app.state == S_MAP && x >= STEP_X) ? BTN_SCAN_STEP : BTN_AUTO_SCAN;
    if (in(Y_EDIT))   return BTN_EDIT;
    if (in(Y_UNDO))   return BTN_UNDO;
    if (in(Y_SAVE))   return BTN_SAVE;
    if (in(Y_CHECK))  return BTN_CHECK;
    return BTN_NONE;
}

// ── Linke Seite ─────────────────────────────────────────────────────────────
static void drawLeftTo(Gfx d) {
    LovyanGFX &g = *d.g;
    const MapModel &m = app.map;
    bool busy = app.measKind != MK_NONE;
    g.fillRect(d.ox, d.oy, SPLIT, DH - STATUS_H, TFT_BLACK);

    g.setTextColor(TFT_CYAN); g.setTextSize(3);
    g.setCursor(d.ox + 8, d.oy + 4);
    g.print("PAN/TILT KARTE");

    g.setTextColor(TFT_WHITE); g.setTextSize(2);
    g.setCursor(d.ox + 8, d.oy + 34);
    switch (app.state) {
        case S_CALIB_O:   g.print("Schritt 1: Origin messen"); break;
        case S_CALIB_A:   g.print("Schritt 2: X-Achse messen"); break;
        case S_AUTO_SCAN: g.print("Auto-Scan laeuft..."); break;
        case S_EDIT:      g.print("Edit-Modus  Enc6=Navigation"); break;
        default:          g.printf("Messmodus  %d/%d Punkte", m.count(), MapModel::MAX_PTS); break;
    }

    g.setTextSize(3);
    g.setCursor(d.ox + 8, d.oy + 56);
    g.printf("Pan%+6.1f  Tilt%+5.1f", app.panDeg, app.tiltDeg);

    if (app.lastValid) {
        g.setTextColor(TFT_YELLOW);
        g.setCursor(d.ox + 8, d.oy + 86);
        if (app.lastDistM < 10.0f) g.printf("%.0f mm", app.lastDistM * 1000.0f);
        else                       g.printf("%.2f m", app.lastDistM);
    } else {
        g.setTextColor(C_DIM);
        g.setCursor(d.ox + 190, d.oy + 86);
        g.print("---");
    }

    const char *laserLbl  = app.laserOn  ? "LASER AUS"  : "LASER EIN";
    const char *markerLbl = app.markerOn ? "MARKER AUS" : "MARKER EIN";
    uint16_t laserCol  = app.laserOn  ? 0x03C0 : 0x7800;
    uint16_t markerCol = app.markerOn ? 0x05FF : 0x7800;

    if (app.state == S_AUTO_SCAN) {
        btn(d, Y_LASER,  "LASER AUS/EIN", C_DISABLED, false);
        btn(d, Y_MARKER, app.markerOn ? "MARKER EIN" : "MARKER AUS", C_DISABLED, false);
        btn(d, Y_ACT,    "SCAN LAEUFT...", TFT_ORANGE, false);
        btn(d, Y_UNDO,   "SCAN STOPPEN", TFT_RED, true);
        g.setTextColor(TFT_WHITE); g.setTextSize(2);
        g.setCursor(d.ox + 8, d.oy + Y_ACT + BH + 8);
        g.printf("Pan %.1f  von %.1f bis %.1f", app.scanCur, app.scanMin, app.scanMax);
        g.setCursor(d.ox + 8, d.oy + Y_ACT + BH + 28);
        g.printf("%d neue Punkte hinzugefuegt", app.scanAdded);

    } else if (app.state == S_EDIT) {
        btn(d, Y_LASER,  laserLbl,  laserCol);
        btn(d, Y_MARKER, markerLbl, markerCol);
        btn(d, Y_ACT,    "EDIT BEENDEN", 0x0460);
        btn(d, Y_EDIT,   "PUNKT LOESCHEN", m.count() ? 0x7800 : C_DISABLED, m.count() > 0);
        if (m.count() && app.editIdx < m.count()) {
            const MapPt &p = m.pt(app.editIdx);
            g.setTextColor(TFT_MAGENTA); g.setTextSize(3);
            g.setCursor(d.ox + 8, d.oy + Y_ACT + BH + 10);
            g.printf("Pkt %d/%d", app.editIdx + 1, m.count());
            g.setTextColor(TFT_WHITE); g.setTextSize(2);
            g.setCursor(d.ox + 8, d.oy + Y_ACT + BH + 40);
            g.printf("X=%.2fm  Y=%.2fm", p.x, p.y);
            g.setCursor(d.ox + 8, d.oy + Y_ACT + BH + 60);
            g.printf("Pan=%.1f", p.panDeg);
        } else {
            g.setTextColor(C_DIM); g.setTextSize(2);
            g.setCursor(d.ox + 8, d.oy + Y_ACT + BH + 10);
            g.print("Keine Punkte vorhanden");
        }

    } else {
        btn(d, Y_LASER,  laserLbl,  laserCol);
        btn(d, Y_MARKER, markerLbl, markerCol);

        if (app.state == S_CALIB_O) {
            btn(d, Y_ACT, busy ? "MESSE..." : "ORIGIN MESSEN", busy ? TFT_ORANGE : TFT_NAVY, !busy);
        } else if (app.state == S_CALIB_A) {
            btn(d, Y_ACT, busy ? "MESSE..." : "X-ACHSE MESSEN", busy ? TFT_ORANGE : TFT_NAVY, !busy);
            g.setTextColor(TFT_DARKGREY); g.setTextSize(2);
            g.setCursor(d.ox + 8, d.oy + Y_ACT + BH + 6);
            g.printf("Origin: Pan=%.1f  d=%.3fm", m.originPan(), m.originDist());
        } else {   // S_MAP
            bool pointBusy = busy && app.measKind != MK_CHECK;
            bool checkBusy = busy && app.measKind == MK_CHECK;
            btn(d, Y_ACT, pointBusy ? "MESSE..." : "PUNKT MESSEN",
                pointBusy ? TFT_ORANGE : 0x000F, !busy);

            bool canScan = m.count() >= 2 && !busy;
            btn(d, BX, Y_AUTO, AUTO_W, "AUTO SCAN", canScan ? 0x4810 : C_DISABLED, canScan);
            stepBtn(d, !busy);

            btn(d, Y_EDIT, "EDIT", m.count() ? 0x6008 : C_DISABLED, m.count() > 0 && !busy);
            btn(d, Y_UNDO, "LETZTEN LOESCHEN", m.canUndo() ? 0x7800 : C_DISABLED, m.canUndo() && !busy);
            btn(d, Y_SAVE, "KARTE SPEICHERN", 0x0460, app.sdOk && m.count() >= 3);
            btn(d, Y_CHECK, checkBusy ? "MESSE..." : "ANFAHRT PRUEFEN",
                checkBusy ? TFT_ORANGE : 0x780F, !busy);

            g.setTextColor(C_DIM); g.setTextSize(2);
            g.setCursor(d.ox + 8, d.oy + Y_CHECK + BH + 4);
            g.printf("Kopf:(%.2f/%.2fm) phi=%.1f", m.headX(), m.headY(), m.headPhi() * 180.0f / (float)M_PI);
        }
    }
}

void uiDrawLeft() {
    if (leftOk) {
        drawLeftTo({&leftCv, 0, 0});
        leftCv.pushSprite(0, 0);
    } else {
        drawLeftTo({&M5.Display, 0, 0});
    }
    M5.Display.drawLine(SPLIT, 0, SPLIT, DH, TFT_DARKGREY);
}

// ── Karte ───────────────────────────────────────────────────────────────────
static float sc = 1, offX = 0, offY = 0;   // Welt -> Kartenpixel (lokal)

static void calcScale() {
    const MapModel &m = app.map;
    float xmin = fminf(0.0f, m.headX()), xmax = fmaxf(0.1f, m.headX());
    float ymin = 0.0f,                    ymax = fmaxf(0.1f, m.headY());
    for (int i = 0; i < m.count(); i++) {
        const MapPt &p = m.pt(i);
        xmin = fminf(xmin, p.x); xmax = fmaxf(xmax, p.x);
        ymin = fminf(ymin, p.y); ymax = fmaxf(ymax, p.y);
    }
    float rx = fmaxf(xmax - xmin, 0.5f), ry = fmaxf(ymax - ymin, 0.5f);
    sc   = fminf((MW - 2 * MP) / rx, (MH - 2 * MP) / ry);
    offX = MP - xmin * sc;
    offY = MP + ymax * sc;
}

static void drawMapTo(Gfx d) {
    LovyanGFX &g = *d.g;
    const MapModel &m = app.map;
    const int L = d.ox, T = d.oy, R = d.ox + MW, B = d.oy + MH;
    auto X = [&](float wx, int pad = 0) { return constrain((int)(L + offX + wx * sc), L + pad, R - pad); };
    auto Y = [&](float wy, int pad = 0) { return constrain((int)(T + offY - wy * sc), T + pad, B - pad); };

    g.fillRect(L, T, MW, MH, TFT_BLACK);
    g.drawRect(L, T, MW, MH, C_GRID);

    if (app.state == S_CALIB_O || app.state == S_CALIB_A) {
        g.setTextColor(C_DIM); g.setTextSize(2);
        g.setCursor(L + 10, T + MH / 2 - 8);
        g.print(app.state == S_CALIB_O ? "Schritt 1: Origin messen..." : "Schritt 2: X-Achse messen...");
        return;
    }

    calcScale();
    int ox = X(0), oy = Y(0);
    g.drawLine(L, oy, R, oy, C_GRID);
    g.drawLine(ox, T, ox, B, C_GRID);

    int n = m.count();
    if (n > 0) {
        int xl = X(m.pt(0).x), xr = X(m.pt(n - 1).x);
        for (int dy = -1; dy <= 1; dy++) g.drawLine(xl, oy + dy, xr, oy + dy, TFT_WHITE);
        g.setTextColor(TFT_WHITE); g.setTextSize(2);
        g.setCursor(constrain(xr + 5, L, R - 14), constrain(oy - 14, T, B));
        g.print("X");

        if (n >= 2) {
            for (int i = 0; i < n - 1; i++)
                g.drawLine(X(m.pt(i).x), Y(m.pt(i).y), X(m.pt(i + 1).x), Y(m.pt(i + 1).y), TFT_GREEN);
            g.drawLine(X(m.pt(n - 1).x), Y(m.pt(n - 1).y), xr, oy, TFT_GREEN);
            g.drawLine(xr, oy, xl, oy, TFT_GREEN);
            g.drawLine(xl, oy, X(m.pt(0).x), Y(m.pt(0).y), TFT_GREEN);
        }
        g.fillCircle(xl, oy, 5, TFT_CYAN);
        g.fillCircle(xr, oy, 5, TFT_CYAN);

        for (int i = 0; i < n; i++) {
            int px = X(m.pt(i).x, 4), py = Y(m.pt(i).y, 4);
            if (app.state == S_EDIT && i == app.editIdx) {
                g.drawCircle(px, py, 10, TFT_MAGENTA);
                g.drawCircle(px, py, 11, TFT_MAGENTA);
                g.fillCircle(px, py, 6, TFT_MAGENTA);
            } else {
                g.fillCircle(px, py, 5, TFT_YELLOW);
            }
        }
    }

    g.drawCircle(ox, oy, 7, TFT_CYAN);
    g.setTextColor(TFT_CYAN); g.setTextSize(2);
    g.setCursor(constrain(ox + 10, L, R - 16), constrain(oy - 18, T, B));
    g.print("O");

    if (app.state != S_EDIT) {
        int hx = X(m.headX(), 6), hy = Y(m.headY(), 6);
        float dir = m.headPhi() - app.panDeg * (float)(M_PI / 180.0);
        int ax = hx + (int)(50.0f * cosf(dir)), ay = hy - (int)(50.0f * sinf(dir));
        g.drawLine(hx, hy, ax, ay, TFT_RED);
        g.drawLine(hx + 1, hy, ax + 1, ay, TFT_RED);
        g.fillCircle(hx, hy, 7, TFT_RED);
    }

    g.setTextColor(C_DIM); g.setTextSize(2);
    g.setCursor(L + 4, B - 20);
    g.printf("%d Pkt | %.2f m/px", n, 1.0f / sc);
}

static void uiDrawMap() {
    if (mapOk) {
        drawMapTo({&mapCv, 0, 0});
        mapCv.pushSprite(ML, MT);
    } else {
        drawMapTo({&M5.Display, ML, MT});
    }
}

void uiRedraw() {
    uiDrawLeft();
    uiDrawMap();
    drawStatus();
}
