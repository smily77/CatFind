// HeadLink.h — Verbindung Karte -> PanTiltHead_WM_V2 (UDP, ../Common/MDV2Protocol.h)
//
// - Sollzustand (Pan/Tilt/Laser/Marker) wird bei Änderung gesendet (höchstens
//   alle 40 ms) und sonst jede Sekunde als Lebenszeichen.
// - Messauftrag mit eigener Nummer; Wiederholung bis Annahme, danach alle
//   2,5 s (falls die Endantwort verloren ging), Abbruch nach 25 s.
// - Head-Adresse: Broadcast, bis sich ein Head meldet, danach Unicast. Kommt
//   5 s nichts mehr, wieder Broadcast.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "../Common/MDV2Protocol.h"

struct MeasureResult {
    uint8_t status;      // MDV2_ST_DONE / _FAILED / _NOSENSOR, oder MR_TIMEOUT
    float   distanceM;   // Schrägdistanz
    float   panActDeg;
    float   tiltActDeg;
    bool    tiltOk;
};
static const uint8_t MR_TIMEOUT = 99;

class HeadLink {
public:
    void begin();
    void setState(float panDeg, float tiltDeg, bool laser, bool marker);
    bool startMeasure(bool approach);     // nimmt den aktuellen Sollzustand
    bool measuring() const { return _measActive; }

    // Regelmässig aufrufen. true, wenn eine Messung abgeschlossen ist (out gefüllt).
    bool poll(MeasureResult &out);

    bool      connected() const;
    uint8_t   sensor() const { return _sensor; }
    IPAddress headIp() const { return _headIp; }
    void      forceHeadIp(IPAddress ip);   // Testzugang; 0.0.0.0 = automatisch

private:
    WiFiUDP   _udp;
    uint16_t  _seq = 0;
    IPAddress _headIp;
    bool      _forced = false;
    uint32_t  _lastRxMs = 0;
    uint8_t   _sensor = MDV2_SENSOR_NONE;

    float _pan = 0, _tilt = 0;
    bool  _laser = false, _marker = false;
    bool  _dirty = true;
    uint32_t _lastSetMs = 0;
    uint16_t _setSeq = 0;

    bool     _measActive = false;
    bool     _measAccepted = false;
    uint16_t _measSeq = 0;
    uint8_t  _measFlags = 0;
    float    _measPan = 0, _measTilt = 0;
    uint32_t _measStartMs = 0, _measTxMs = 0;

    uint8_t  flags() const;
    void     send(uint16_t seq, uint8_t cmd, uint8_t flags, float pan, float tilt);
    IPAddress target() const;
};
