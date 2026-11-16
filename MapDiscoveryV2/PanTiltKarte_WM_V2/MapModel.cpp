// MapModel.cpp — siehe MapModel.h
#include "MapModel.h"
#include <math.h>

const float MapModel::DUP_PAN_DEG = 0.1f;

static const float DEG2RAD = (float)(M_PI / 180.0);

// Strahlrichtung im Kopf-Rahmen (headPhi = 0)
static void headRay(float panDeg, float d, float &x, float &y) {
    float a = -panDeg * DEG2RAD;
    x = d * cosf(a);
    y = d * sinf(a);
}

void MapModel::setOrigin(float panDeg, float horizDist) {
    _panO  = panDeg;
    _distO = horizDist;
    _calibrated = false;
}

// Kopf-Pose aus O und A:
//   Im Kopf-Rahmen liegen O' und A' (Strahl + Distanz). Die Welt entsteht durch
//   Drehung um phi und Verschiebung so, dass O' -> (0,0) und O'->A' entlang +x
//   zeigt. phi = -Winkel(A' - O'), Kopf = -R(phi) * O'.
MapModel::CalibResult MapModel::setAxis(float panDeg, float horizDist) {
    float ox, oy, ax, ay;
    headRay(_panO, _distO, ox, oy);
    headRay(panDeg, horizDist, ax, ay);
    float vx = ax - ox, vy = ay - oy;
    if (vx * vx + vy * vy < 0.05f * 0.05f) return CAL_TOO_CLOSE;

    _phi = -atan2f(vy, vx);
    float c = cosf(_phi), s = sinf(_phi);
    _hx = -(c * ox - s * oy);
    _hy = -(s * ox + c * oy);
    _calibrated = true;
    return CAL_OK;
}

void MapModel::resetCalib() {
    _calibrated = false;
    _hx = _hy = _phi = 0;
}

void MapModel::toWorld(float panDeg, float horizDist, float &x, float &y) const {
    float dir = _phi - panDeg * DEG2RAD;
    x = _hx + horizDist * cosf(dir);
    y = _hy + horizDist * sinf(dir);
}

int MapModel::findPan(float panDeg) const {
    for (int i = 0; i < _n; i++)
        if (fabsf(_pts[i].panDeg - panDeg) < DUP_PAN_DEG) return i;
    return -1;
}

MapModel::AddResult MapModel::add(float panDeg, float horizDist) {
    MapPt p;
    toWorld(panDeg, horizDist, p.x, p.y);
    p.panDeg = panDeg;

    int dup = findPan(panDeg);
    if (dup >= 0) {
        _undoOld      = _pts[dup];
        _undoReplaced = true;
        _undoPan      = panDeg;
        _undoValid    = true;
        // Ersatz kann die Sortierung verschieben (Pan weicht bis 0,1° ab): neu einsortieren
        for (int j = dup; j < _n - 1; j++) _pts[j] = _pts[j + 1];
        _n--;
    } else if (_n >= MAX_PTS) {
        return ADD_FULL;
    } else {
        _undoReplaced = false;
        _undoPan      = panDeg;
        _undoValid    = true;
    }

    int i = _n;
    while (i > 0 && _pts[i - 1].panDeg > panDeg) { _pts[i] = _pts[i - 1]; i--; }
    _pts[i] = p;
    _n++;
    return dup >= 0 ? ADD_REPLACED : ADD_NEW;
}

void MapModel::undo() {
    if (!_undoValid) return;
    _undoValid = false;
    int idx = -1;
    for (int i = 0; i < _n; i++)
        if (_pts[i].panDeg == _undoPan) { idx = i; break; }
    if (idx < 0) return;
    for (int j = idx; j < _n - 1; j++) _pts[j] = _pts[j + 1];
    _n--;
    if (_undoReplaced) {
        int i = _n;
        while (i > 0 && _pts[i - 1].panDeg > _undoOld.panDeg) { _pts[i] = _pts[i - 1]; i--; }
        _pts[i] = _undoOld;
        _n++;
    }
}

void MapModel::remove(int idx) {
    if (idx < 0 || idx >= _n) return;
    for (int j = idx; j < _n - 1; j++) _pts[j] = _pts[j + 1];
    _n--;
    _undoValid = false;
}

void MapModel::clearPoints() {
    _n = 0;
    _undoValid = false;
}

bool MapModel::expectedDist(float panDeg, float maxGapDeg, float &dist) const {
    int lo = -1, hi = -1, same = -1;
    for (int i = 0; i < _n; i++) {
        float d = _pts[i].panDeg - panDeg;
        if (fabsf(d) < DUP_PAN_DEG) { same = i; continue; }
        if (fabsf(d) > maxGapDeg) continue;
        if (d < 0) lo = i;                    // sortiert: letzter links = nächster
        else if (hi < 0) hi = i;              // erster rechts = nächster
    }
    auto r = [&](int i) { return hypotf(_pts[i].x - _hx, _pts[i].y - _hy); };
    if (same >= 0) {
        dist = r(same);                       // Punkt am selben Pan: bester Vergleich
    } else if (lo >= 0 && hi >= 0) {
        float t = (panDeg - _pts[lo].panDeg) / (_pts[hi].panDeg - _pts[lo].panDeg);
        dist = r(lo) + t * (r(hi) - r(lo));
    } else if (lo >= 0) dist = r(lo);
    else if (hi >= 0)   dist = r(hi);
    else return false;
    return true;
}

float MapModel::minPan() const { return _n ? _pts[0].panDeg : 0.0f; }
float MapModel::maxPan() const { return _n ? _pts[_n - 1].panDeg : 0.0f; }
