// Plausi.cpp — siehe Plausi.h
#include "Plausi.h"
#include <math.h>

static const float NEIGH_DEG   = 30.0f;   // Nachbarn in diesem Pan-Abstand
static const float DEV_ABS_M   = 0.5f;    // erlaubte Abweichung absolut
static const float DEV_REL     = 0.30f;   //   bzw. relativ zur Erwartung
static const float MAX_RANGE_M = 50.0f;
static const float AGREE_ABS_M = 0.05f;   // zwei Messungen gleich
static const float AGREE_REL   = 0.02f;
static const int   MAX_TRIES   = 3;

bool Plausi::implausibleAgainst(float h, bool hasExp, float exp) {
    if (h <= 0.0f || h > MAX_RANGE_M) return true;
    if (!hasExp) return false;
    return fabsf(h - exp) > fmaxf(DEV_ABS_M, DEV_REL * exp);
}

bool Plausi::agree(float a, float b) {
    return fabsf(a - b) <= fmaxf(AGREE_ABS_M, AGREE_REL * fmaxf(a, b));
}

Plausi::Verdict Plausi::feed(const MapModel &m, bool valid, float panDeg, float horiz) {
    if (_tries == 0) {
        _unsure = false;
        _hasExp = m.expectedDist(panDeg, NEIGH_DEG, _exp);
    }
    _tries++;
    // Erste Messung ohne Echo: wie bisher melden, nicht wiederholen (der Head
    // hat selbst schon 3 Versuche gemacht; ins Leere zielen kostet sonst viel Zeit)
    if (!valid && _tries == 1) return V_FAIL;
    if (valid && _n < 3) { _h[_n] = horiz; _p[_n] = panDeg; _n++; }

    bool last = _tries >= MAX_TRIES;

    if (valid) {
        int i = _n - 1;
        // plausibel -> nehmen
        if (!implausibleAgainst(_h[i], _hasExp, _exp)) return accept(i);
        // stimmt mit einem früheren Wert überein -> echte Stelle
        for (int j = 0; j < i; j++) {
            if (agree(_h[i], _h[j])) {
                _outPan = _p[i];
                _outH   = 0.5f * (_h[i] + _h[j]);
                return V_ACCEPT;
            }
        }
    }
    if (!last) return V_REMEASURE;

    // Versuche aufgebraucht: bestes nehmen, was da ist
    if (_n == 0) return V_FAIL;
    _unsure = true;
    int best = _n - 1;
    if (_hasExp) {
        for (int j = 0; j < _n; j++)
            if (fabsf(_h[j] - _exp) < fabsf(_h[best] - _exp)) best = j;
    }
    return accept(best);
}
