// Plausi.h — Nachmessen bei unplausiblen Werten (PUNKT MESSEN und Auto-Scan)
//
// Unplausibel: mehr als 0,5 m bzw. 30 % neben der Distanz, die der Punkt am
// selben Pan (falls vorhanden) bzw. die Nachbar-
// punkte (±30° Pan) erwarten lassen, oder über 50 m. Ohne Nachbarn: plausibel.
//
// Ablauf je Punkt (höchstens 3 Messungen):
//   1. plausibel -> übernehmen, sonst nachmessen
//   2. stimmt mit 1 überein (±5 cm / 2 %) -> echte Stelle, übernehmen;
//      plausibel -> übernehmen (1 war Ausreisser); sonst dritte Messung
//   3. zwei Werte, die übereinstimmen -> deren Mittel; sonst der Wert, der am
//      nächsten an der Erwartung liegt (als "unsicher" gemeldet)
// Ist schon die erste Messung ohne Echo, wird wie bisher "ungueltig" gemeldet.
// Fehlmessungen beim Nachmessen zählen als Versuch ohne Wert.
#pragma once
#include "MapModel.h"

class Plausi {
public:
    enum Verdict { V_ACCEPT, V_REMEASURE, V_FAIL };

    void reset() { _tries = 0; _n = 0; }

    // Ergebnis eines Versuchs einspeisen (valid=false: Fehlmessung)
    Verdict feed(const MapModel &m, bool valid, float panDeg, float horiz);

    // Nach V_ACCEPT: gewählter Wert
    float pan()   const { return _outPan; }
    float horiz() const { return _outH; }
    int   tries() const { return _tries; }        // Anzahl Messungen für diesen Punkt
    bool  unsure() const { return _unsure; }      // keine zwei übereinstimmenden Werte
    bool  hasExpected() const { return _hasExp; }
    float expected() const { return _exp; }
    float lastValue() const { return _n ? _h[_n - 1] : 0.0f; }

    static bool implausibleAgainst(float h, bool hasExp, float exp);
    static bool agree(float a, float b);

private:
    int   _tries = 0, _n = 0;
    float _h[3], _p[3];
    float _outPan = 0, _outH = 0;
    bool  _unsure = false;
    bool  _hasExp = false;
    float _exp = 0;

    Verdict accept(int i) { _outPan = _p[i]; _outH = _h[i]; return V_ACCEPT; }
};
