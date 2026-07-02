#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CatFinder Erkennungsmodell (Offline/Analyse auf dem VPS).

Bildet aus catObserved-Events (Welt-Koordinaten, ms-genaue Zeit) Tracks und
bewertet sie: Katze oder Störung? Regeln nach GesamtKonzeptCatFinder.md,
Aufgabe "Katze oder Störung" / "VPS-Modellierung der Katzenerkennung":

  - Track-Bildung:   Nearest-Neighbor mit Geschwindigkeits-Gate
  - Sturm-Erkennung: Ereignisrate je Sensor (Sonnen-Bursts des Lidars usw.)
  - Bestätigung:     >= confirm_min_points Beobachtungen in <= confirm_window_ms,
                     Netto-Verschiebung >= min_net_displacement_mm,
                     Geschwindigkeit überwiegend in [speed_min, speed_max],
                     Sensor-Gewichtssumme >= min_confirm_weight
                     (Radar hoch, Lidar niedrig -> Lidar allein bestätigt nie)
  - Fusion:          zwei verschiedene Sensoren sehen dasselbe Objekt
                     (< fusion_distance_mm, < fusion_time_ms) -> sofort bestätigt

Alle Schwellwerte kommen aus einer Parameter-JSON (im Docker-Volume, ohne
Rebuild änderbar). Dieses Modul ist bewusst frei von Flask/DB -- reine
Funktion (Events, Parameter) -> (Tracks, Stürme), damit es später 1:1 als
Referenz für die ESP32-Implementierung dienen kann.
"""
import math

DEFAULT_PARAMS = {
    # Track-Assoziation
    "gate_speed_mm_s": 4000,        # max. Katzengeschwindigkeit fürs Gate
    "gate_base_mm": 400,            # Grundunschärfe (Sensorrauschen, Zentroid)
    "track_gap_ms": 1500,           # so lange ohne Fortsetzung -> Track endet
    # Bestätigung (Ebene 2)
    "confirm_min_points": 4,
    "confirm_window_ms": 1000,
    "min_net_displacement_mm": 400,
    "speed_min_mm_s": 100,
    "speed_max_mm_s": 4000,
    "speed_ok_ratio": 0.7,          # Anteil plausibler Schrittgeschwindigkeiten
    # Sturm-/Burst-Erkennung je Sensor (Ebene 1, nachgerechnet)
    "storm_window_ms": 3000,
    "storm_rate_evts_s": 8.0,
    "storm_min_events": 12,
    # Sensor-Gewichte: sender-ID -> Gewicht ("default" für unbekannte).
    # Radar = primärer Katzendetektor, LidarC1 (17) nur Unterstützer.
    "sensor_weight": {"default": 1.0, "17": 0.3},
    "min_confirm_weight": 0.9,
    # Fusion (Ebene 3a)
    "fusion_distance_mm": 500,
    "fusion_time_ms": 500,
}


def merged_params(params):
    p = dict(DEFAULT_PARAMS)
    for k, v in (params or {}).items():
        if k == "sensor_weight" and isinstance(v, dict):
            w = dict(DEFAULT_PARAMS["sensor_weight"])
            w.update({str(kk): float(vv) for kk, vv in v.items()})
            p[k] = w
        elif k in DEFAULT_PARAMS:
            p[k] = type(DEFAULT_PARAMS[k])(v) if not isinstance(DEFAULT_PARAMS[k], dict) else v
    return p


def _weight(p, sender):
    w = p["sensor_weight"]
    return float(w.get(str(sender), w.get("default", 1.0)))


# ---------------------------------------------------------------- Stürme

def detect_storms(events, p):
    """Ereignisrate je Sender; Rückgabe {sender: [[t0,t1], ...]} (Serverzeit s)."""
    win = p["storm_window_ms"] / 1000.0
    need = max(int(p["storm_min_events"]), int(p["storm_rate_evts_s"] * win))
    by_sender = {}
    for e in events:
        by_sender.setdefault(e["sender"], []).append(e["t"])
    storms = {}
    for sid, ts in by_sender.items():
        ts.sort()
        ivs = []
        j = 0
        for i, t in enumerate(ts):
            while t - ts[j] > win:
                j += 1
            if i - j + 1 >= need:
                a, b = ts[j], t
                if ivs and a <= ivs[-1][1]:
                    ivs[-1][1] = max(ivs[-1][1], b)
                else:
                    ivs.append([a, b])
        if ivs:
            storms[sid] = ivs
    return storms


def in_storm(storms, sender, t):
    for a, b in storms.get(sender, ()):
        if a <= t <= b:
            return True
    return False


# ---------------------------------------------------------------- Tracks

def build_tracks(events, p):
    """Events (zeitsortiert) -> Liste von Tracks; Punkt = (t,x,y,sender,sensor,speed)."""
    gap = p["track_gap_ms"] / 1000.0
    active, done = [], []
    for e in events:
        t, x, y = e["t"], e["wx"], e["wy"]
        keep = []
        for tr in active:
            (done if t - tr[-1][0] > gap else keep).append(tr)
        active = keep
        best, bestd = None, None
        for tr in active:
            lt, lx, ly = tr[-1][0], tr[-1][1], tr[-1][2]
            dt = max(0.0, t - lt)
            gate = p["gate_base_mm"] + p["gate_speed_mm_s"] * dt
            d = math.hypot(x - lx, y - ly)
            if d <= gate and (bestd is None or d < bestd):
                best, bestd = tr, d
        pt = (t, x, y, e["sender"], e["sensor"], e.get("speed", 0))
        if best is not None:
            best.append(pt)
        else:
            active.append([pt])
            if len(active) > 400:               # Sicherheitsventil bei Extrem-Bursts
                done.append(active.pop(0))
    done.extend(active)
    done.sort(key=lambda tr: tr[0][0])
    return done


# ---------------------------------------------------------------- Bewertung

def _speeds(pts):
    vs = []
    for a, b in zip(pts, pts[1:]):
        dt = b[0] - a[0]
        if dt > 0:
            vs.append(math.hypot(b[1] - a[1], b[2] - a[2]) / dt)
    return vs


def _try_fusion(pts, storms, p):
    """Zwei verschiedene Sender sehen dasselbe -> sofortige Bestätigung."""
    fd, ft = p["fusion_distance_mm"], p["fusion_time_ms"] / 1000.0
    last = {}                                    # sender -> letzter Punkt
    for pt in pts:
        for sid, o in last.items():
            if sid != pt[3] and pt[0] - o[0] <= ft and \
               math.hypot(pt[1] - o[1], pt[2] - o[2]) <= fd and \
               not in_storm(storms, pt[3], pt[0]) and \
               not in_storm(storms, sid, o[0]):
                return pt[0]
        last[pt[3]] = pt
    return None


def _try_confirm(pts, storms, p):
    """Fenster-Scan nach den Ebene-2-Regeln; liefert (t_confirm, reasons) oder (None, ...)."""
    win = p["confirm_window_ms"] / 1000.0
    j = 0
    for i in range(len(pts)):
        while pts[i][0] - pts[j][0] > win:
            j += 1
        w = pts[j:i + 1]
        if len(w) < p["confirm_min_points"]:
            continue
        if math.hypot(w[-1][1] - w[0][1], w[-1][2] - w[0][2]) < p["min_net_displacement_mm"]:
            continue
        vs = _speeds(w)
        if not vs:
            continue
        ok = sum(1 for v in vs if p["speed_min_mm_s"] <= v <= p["speed_max_mm_s"])
        if ok / len(vs) < p["speed_ok_ratio"]:
            continue
        if sum(_weight(p, s) for s in {pt[3] for pt in w}) < p["min_confirm_weight"]:
            continue
        if any(in_storm(storms, pt[3], pt[0]) for pt in w):
            continue
        return w[-1][0], ["TRACK_CONTINUOUS", "NET_DISPLACEMENT_OK", "SPEED_OK", "WEIGHT_OK"]
    return None, []


def _reject_reason(pts, feats, storms, p):
    if feats["n"] == 1:
        return "Einzelereignis"
    if feats["storm_ratio"] > 0.5:
        return "Sturm/Burst"
    if feats["net_mm"] < p["min_net_displacement_mm"]:
        return "stationär/oszillierend"
    if feats["n"] < p["confirm_min_points"]:
        return "zu wenige Punkte"
    if sum(_weight(p, s) for s in feats["senders"]) < p["min_confirm_weight"]:
        return "nur schwacher Sensor"
    return "Geschwindigkeit/Fenster unplausibel"


def score_track(pts, storms, p, tid):
    vs = _speeds(pts)
    feats = {
        "id": tid,
        "t0": pts[0][0], "t1": pts[-1][0],
        "n": len(pts),
        "net_mm": round(math.hypot(pts[-1][1] - pts[0][1], pts[-1][2] - pts[0][2])),
        "path_mm": round(sum(math.hypot(b[1] - a[1], b[2] - a[2])
                             for a, b in zip(pts, pts[1:]))),
        "v_mean": round(sum(vs) / len(vs)) if vs else 0,
        "v_max": round(max(vs)) if vs else 0,
        "senders": sorted({pt[3] for pt in pts}),
        "storm_ratio": round(sum(1 for pt in pts
                                 if in_storm(storms, pt[3], pt[0])) / len(pts), 2),
    }
    tf = _try_fusion(pts, storms, p)
    tc, reasons = _try_confirm(pts, storms, p)
    if tf is not None and (tc is None or tf < tc):
        tc, reasons = tf, ["MULTISENSOR_CONFIRMED"]
    feats["confirmed"] = tc is not None
    feats["t_confirm"] = tc
    feats["reasons"] = reasons
    feats["reject"] = None if tc is not None else _reject_reason(pts, feats, storms, p)
    # Punkte fürs UI (grosse Tracks ausdünnen, Anfang/Ende behalten)
    out = pts
    if len(pts) > 800:
        step = len(pts) // 800 + 1
        out = pts[::step]
        if out[-1] is not pts[-1]:
            out.append(pts[-1])
    feats["pts"] = [[round(pt[0], 3), pt[1], pt[2], pt[3], pt[4]] for pt in out]
    return feats


def analyze(events, params=None):
    """events: [{t,sender,sensor,wx,wy,speed}] (nur worldValid) -> Modell-Ergebnis."""
    p = merged_params(params)
    evs = sorted(events, key=lambda e: e["t"])
    storms = detect_storms(evs, p)
    tracks = [score_track(tr, storms, p, i)
              for i, tr in enumerate(build_tracks(evs, p))]
    return {
        "tracks": tracks,
        "storms": {str(k): v for k, v in storms.items()},
        "n_events": len(evs),
        "n_confirmed": sum(1 for t in tracks if t["confirmed"]),
        "params": p,
    }
