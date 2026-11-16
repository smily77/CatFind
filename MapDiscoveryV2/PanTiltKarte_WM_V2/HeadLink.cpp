// HeadLink.cpp — siehe HeadLink.h
#include "HeadLink.h"
#include <esp_random.h>

static const uint32_t SET_MIN_GAP_MS    = 40;
static const uint32_t SET_KEEPALIVE_MS  = 1000;
static const uint32_t MEAS_RETRY_MS     = 400;    // bis Annahme
static const uint32_t MEAS_RESEND_MS    = 2500;   // nach Annahme
static const uint32_t MEAS_TIMEOUT_MS   = 25000;
static const uint32_t HEAD_LOST_MS      = 5000;

void HeadLink::begin() {
    _udp.begin(MDV2_PORT_KARTE);
    _seq = (uint16_t)esp_random();     // nach Neustart keine Verwechslung mit alten Aufträgen
    _headIp = IPAddress(0, 0, 0, 0);
}

uint8_t HeadLink::flags() const {
    return (_laser ? MDV2_F_LASER : 0) | (_marker ? MDV2_F_MARKER : 0);
}

IPAddress HeadLink::target() const {
    if (_forced) return _headIp;
    if (_headIp != IPAddress(0, 0, 0, 0) && millis() - _lastRxMs < HEAD_LOST_MS) return _headIp;
    // Netz-Broadcast (z.B. 192.168.0.255); 255.255.255.255 kommt beim Tab5 nicht an
    IPAddress b = WiFi.broadcastIP();
    return b != IPAddress(0, 0, 0, 0) ? b : IPAddress(255, 255, 255, 255);
}

void HeadLink::send(uint16_t seq, uint8_t cmd, uint8_t fl, float pan, float tilt) {
    MDV2CmdMsg m;
    m.magic   = MDV2_MAGIC;
    m.seq     = seq;
    m.cmd     = cmd;
    m.flags   = fl;
    m.panDeg  = pan;
    m.tiltDeg = tilt;
    _udp.beginPacket(target(), MDV2_PORT_HEAD);
    _udp.write((const uint8_t *)&m, sizeof(m));
    _udp.endPacket();
}

void HeadLink::forceHeadIp(IPAddress ip) {
    _forced = (ip != IPAddress(0, 0, 0, 0));
    _headIp = ip;
}

bool HeadLink::connected() const {
    return _lastRxMs != 0 && millis() - _lastRxMs < HEAD_LOST_MS;
}

void HeadLink::setState(float pan, float tilt, bool laser, bool marker) {
    if (pan != _pan || tilt != _tilt || laser != _laser || marker != _marker) _dirty = true;
    _pan = pan; _tilt = tilt; _laser = laser; _marker = marker;
}

bool HeadLink::startMeasure(bool approach) {
    if (_measActive) return false;
    _measSeq      = ++_seq;
    _measFlags    = flags() | (approach ? MDV2_F_APPROACH : 0);
    _measPan      = _pan;
    _measTilt     = _tilt;
    _measActive   = true;
    _measAccepted = false;
    _measStartMs  = _measTxMs = millis();
    _dirty        = false;      // der Messauftrag trägt den Sollzustand mit
    _lastSetMs    = millis();
    send(_measSeq, MDV2_CMD_MEASURE, _measFlags, _measPan, _measTilt);
    return true;
}

bool HeadLink::poll(MeasureResult &out) {
    uint32_t now = millis();
    bool finished = false;

    // ── Empfang ──
    for (int guard = 0; guard < 8; guard++) {
        int n = _udp.parsePacket();
        if (n <= 0) break;
        MDV2ReplyMsg r;
        if (n != sizeof(r)) { _udp.flush(); continue; }
        _udp.read((uint8_t *)&r, sizeof(r));
        if (r.magic != MDV2_MAGIC) continue;

        _lastRxMs = now ? now : 1;
        if (!_forced) _headIp = _udp.remoteIP();
        _sensor = r.sensor;

        if (_measActive && r.cmd == MDV2_CMD_MEASURE && r.seq == _measSeq) {
            if (r.status == MDV2_ST_ACCEPTED || r.status == MDV2_ST_BUSY) {
                _measAccepted = true;
            } else if (!finished) {
                out.status     = r.status;
                out.distanceM  = r.distanceM;
                out.panActDeg  = r.panActDeg;
                out.tiltActDeg = r.tiltActDeg;
                out.tiltOk     = r.tiltOk != 0;
                _measActive    = false;
                finished       = true;
                _dirty         = true;   // Sollzustand danach bestätigen lassen
            }
        }
        // Antworten auf SET und alte Aufträge: nur Lebenszeichen
    }

    // ── Messauftrag wiederholen / abbrechen ──
    if (_measActive) {
        if (now - _measStartMs > MEAS_TIMEOUT_MS) {
            out = {};
            out.status  = MR_TIMEOUT;
            _measActive = false;
            _dirty      = true;
            return true;
        }
        uint32_t gap = _measAccepted ? MEAS_RESEND_MS : MEAS_RETRY_MS;
        if (now - _measTxMs > gap) {
            _measTxMs = now;
            send(_measSeq, MDV2_CMD_MEASURE, _measFlags, _measPan, _measTilt);
        }
    }

    // ── Sollzustand senden ──
    if (!_measActive || _dirty) {
        bool due = (_dirty && now - _lastSetMs >= SET_MIN_GAP_MS) ||
                   (!_measActive && now - _lastSetMs >= SET_KEEPALIVE_MS);
        if (due) {
            _setSeq = ++_seq;
            send(_setSeq, MDV2_CMD_SET, flags(), _pan, _tilt);
            _lastSetMs = now;
            _dirty = false;
        }
    }
    return finished;
}
