// MapModel.h — Rechenteil der Karte: Kalibrierung + Grenzpunkte
//
// Kein Display, kein Netz. Alle Distanzen, die hier hineingehen, sind
// WAAGRECHTE Distanzen (Schrägdistanz * cos(Tilt), Umrechnung im Aufrufer).
//
// Koordinaten: Welt in Metern, Origin O = (0,0), x-Achse durch A.
// Richtung eines Strahls in der Welt: headPhi - pan (pan in Grad -> rad),
// d.h. positiver Pan dreht im Uhrzeigersinn (wie in V1).
#pragma once
#include <stdint.h>

struct MapPt {
    float x, y;       // Welt (m)
    float panDeg;     // Ist-Pan bei der Messung
};

class MapModel {
public:
    static const int MAX_PTS = 1000;

    // ── Kalibrierung ────────────────────────────────────────────────────
    enum CalibResult { CAL_OK, CAL_TOO_CLOSE };

    void  setOrigin(float panDeg, float horizDist);
    CalibResult setAxis(float panDeg, float horizDist);   // rechnet Kopf-Pose
    void  resetCalib();

    bool  calibrated() const { return _calibrated; }
    float originPan()  const { return _panO; }
    float originDist() const { return _distO; }
    float headX()   const { return _hx; }
    float headY()   const { return _hy; }
    float headPhi() const { return _phi; }   // rad

    void  toWorld(float panDeg, float horizDist, float &x, float &y) const;

    // ── Punkte ─────────────────────────────────────────────────────────
    enum AddResult { ADD_NEW, ADD_REPLACED, ADD_FULL };

    // Punkt aus Messung einfügen (nach Pan sortiert). Liegt bereits ein Punkt
    // innerhalb DUP_PAN_DEG, wird er ersetzt.
    AddResult add(float panDeg, float horizDist);
    bool  canUndo() const { return _undoValid; }
    void  undo();                  // letzte Einfüge-/Ersetz-Aktion zurück
    void  remove(int idx);         // Punkt löschen (macht Undo ungültig)
    void  clearPoints();

    int          count() const { return _n; }
    const MapPt &pt(int i) const { return _pts[i]; }
    float minPan() const;
    float maxPan() const;

    static const float DUP_PAN_DEG;

private:
    bool  _calibrated = false;
    float _panO = 0, _distO = 0;
    float _hx = 0, _hy = 0, _phi = 0;

    MapPt _pts[MAX_PTS];
    int   _n = 0;

    // Undo: genau eine Aktion
    bool  _undoValid    = false;
    bool  _undoReplaced = false;   // true: _undoOld wiederherstellen; false: Punkt entfernen
    float _undoPan      = 0;       // Pan des betroffenen Punkts
    MapPt _undoOld;

    int   findPan(float panDeg) const;   // Index innerhalb DUP_PAN_DEG oder -1
};
