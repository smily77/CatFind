// App.h — gemeinsamer Zustand der Karte (Ablauf, Anzeige, Eingabe)
#pragma once
#include <Arduino.h>
#include "MapModel.h"

enum AppState { S_CALIB_O, S_CALIB_A, S_MAP, S_AUTO_SCAN, S_EDIT };

// Art der laufenden Messung
enum MeasKind { MK_NONE, MK_ORIGIN, MK_AXIS, MK_POINT, MK_CHECK, MK_SCAN };

struct App {
    AppState state = S_CALIB_O;

    // Sollzustand
    float panDeg  = 0.0f;
    float tiltDeg = 0.0f;
    bool  laserOn  = false;
    bool  markerOn = false;

    // Messung
    MeasKind measKind = MK_NONE;
    bool  lastValid = false;
    float lastDistM = 0.0f;        // Schrägdistanz der letzten Messung

    // Auto-Scan
    float scanMin = 0, scanMax = 0, scanCur = 0;
    int   scanAdded = 0;
    int   scanStepIdx = 2;         // Index in SCAN_STEPS

    // Edit
    int   editIdx = 0;

    bool  sdOk = false;

    MapModel map;
};

extern App app;

static const float SCAN_STEPS[]   = {0.5f, 1.0f, 2.0f, 5.0f};
static const int   SCAN_STEP_COUNT = 4;
inline float scanStep() { return SCAN_STEPS[app.scanStepIdx]; }

// ── Anzeige (Ui.cpp) ────────────────────────────────────────────────────────
void uiBegin();
void uiRedraw();                    // linke Seite + Karte + Statuszeile
void uiDrawLeft();
void uiSetStatus(const char *msg, uint16_t col);

// Touch-Ziele
enum UiButton {
    BTN_NONE, BTN_LASER, BTN_MARKER, BTN_ACT, BTN_AUTO_SCAN, BTN_SCAN_STEP,
    BTN_EDIT, BTN_UNDO, BTN_SAVE, BTN_CHECK
};
UiButton uiHitTest(int x, int y);   // welcher Knopf (unabhängig davon, ob aktiv)
