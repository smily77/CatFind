#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CatFinder Erkennungsmodell v2 (Offline/Analyse auf dem VPS).

Bewertet Tracks aus catObserved-Events als SCORE (0..100, "Katzen-
Wahrscheinlichkeit") statt mit starren Regeln. Kernideen (GesamtKonzept,
Aufgabe "VPS-Modellierung der Katzenerkennung"):

  - Sensor-Gewichte je GERÄTETYP (HLK-Radar hoch, Lidar niedrig), die
    Typen kommen dynamisch aus der Gerätedatenbank xComDef6_3.h — neue
    Sensoren brauchen keine Modell-Änderung.
  - Erfassungsgrenzen GEOMETRISCH aus der xComDef (covLeftDeg/covRightDeg/
    covRangeMm je Gerät, z.B. HLK-Radar -60..+60 Grad 7 m, Lidar 360 Grad
    12 m) + den per poseReport gemeldeten Welt-Posen der Sensoren
    (build_coverage_geo). Vorteil gegenüber der früheren rein empirischen
    Abdeckung: nach dem Versetzen eines Sensors stimmt der Bereich sofort
    wieder — nichts muss neu "eingelaufen" werden. Eine Katze läuft in den
    Erfassungsbereich hinein und hinaus -> Track-Geburt/-Tod nahe der
    Grenze der GESAMT-Abdeckung (Vereinigung aller Sektoren, damit
    Überlappungen/Übergaben nicht als Austritt zählen) gibt Bonus;
    Auftauchen mitten im Feld nur einen weichen Malus ("kann sein, muss
    nicht"). Fallback, solange keine Posen bekannt sind: empirische
    Abdeckung aus den Langzeitdaten (build_coverage).
  - Eine Katze darf STEHENBLEIBEN (koten!): kurze Radar-Aussetzer werden
    per Track-Stitching überbrückt; "sitzt am Ende" gibt Bonus + Flag
    STATIONAER; ein am Fensterende noch offener Track bekommt keinen
    Austritts-Malus.
  - Pflicht bleibt EINE kohärente Bewegungsphase irgendwo im Track
    (sonst wäre oszillierende Vegetation eine "sitzende Katze").
  - Sturm-/Burst-Erkennung je Sensor über die Ereignisrate; Punkte im
    Sturm zählen nicht als Evidenz.

Bewusst frei von Flask/DB — reine Funktion (Events, Parameter, Geräte,
Abdeckung) -> (Tracks mit Score-Aufschlüsselung), als spätere Referenz
für die ESP32-Implementierung.
"""
import math

DEFAULT_PARAMS = {
    # --- Track-Assoziation
    "gate_speed_mm_s": 4000,        # max. Katzengeschwindigkeit fürs Gate
    "gate_base_mm": 400,            # Grundunschärfe (Rauschen, Zentroid)
    "track_gap_ms": 1500,           # ohne Fortsetzung -> Track endet
    "stitch_gap_ms": 6000,          # Naht: sitzende Katze verschwindet kurz
    "stitch_dist_mm": 700,
    # --- Kern: kohärente Bewegungsphase (Pflicht für Bestätigung)
    "move_min_points": 4,
    "move_window_ms": 1200,
    "move_min_net_mm": 400,
    "speed_min_mm_s": 60,           # Katze schleicht auch mal
    "speed_max_mm_s": 4000,
    "speed_ok_ratio": 0.7,
    "min_confirm_weight": 0.9,      # Typ-Gewichtssumme in der Bewegungsphase
    "type_weight": {"HLK": 1.0, "Lidar": 0.3, "default": 0.6},
    # --- Score (Bestätigung ab confirm_score, Bewegungsphase vorausgesetzt)
    "confirm_score": 60,
    "score_move": 50,               # kohärente Bewegung gefunden
    "score_entry": 18,              # Geburt nahe Rand der Gesamtabdeckung
    "score_exit": 12,               # Tod nahe Rand der Gesamtabdeckung
    "score_fusion": 25,             # >=2 Sender sehen dasselbe Objekt
    "score_crossing": 12,           # grosse Netto-Verschiebung (>= crossing_net_mm)
    "score_long": 8,                # langer Track (>= long_track_points saubere Punkte)
    "score_stationary": 8,          # sitzt am Ende stabil (koten!) nach Bewegung
    "penalty_mid_birth": 12,        # mitten in der Abdeckung aufgetaucht (weich)
    "penalty_mid_death": 8,         # mitten in der Abdeckung verschwunden (weich)
    "penalty_short": 10,            # sehr kurzer Track (Vogel/Zufall)
    "penalty_jumpy": 10,            # viele unphysikalische Sprünge
    "crossing_net_mm": 800,
    "long_track_points": 10,
    "short_track_ms": 800,
    # Weg-Obergrenze: RoboMäher/Personen laufen in EINEM Track hunderte Meter
    # zusammenhängend ab — eine Katze nicht. Längerer Pfad => keine Katze. 0 = aus.
    "max_path_mm": 40000,
    # --- Erfassungsgrenzen (empirische Abdeckung)
    "edge_dist_mm": 900,            # "nahe am Rand" der Gesamtabdeckung
    "cov_cell_mm": 300,             # Rasterzelle der Abdeckung
    "cov_min_events": 3,            # Zelle gilt ab so vielen Events als abgedeckt
    "cov_min_cells": 40,            # darunter: Abdeckung zu dünn -> Randlogik neutral
    # --- Sitzen / Stationär am Track-Ende
    "stationary_end_s": 3.0,
    "stationary_radius_mm": 350,
    # --- Sturm-/Burst-Erkennung je Sensor: hohe Rate ALLEIN genügt nicht
    #     (ein Radar trackt eine Katze auch mit ~10 Hz) — ein Sturm ist
    #     gleichzeitig ÜBERALL (grosse räumliche Streuung pro Zeitscheibe).
    "storm_window_ms": 3000,
    "storm_rate_evts_s": 8.0,
    "storm_min_events": 12,
    "storm_scatter_mm": 2500,       # mediane Streuung (BBox-Diagonale je 0,5 s)
    # --- Fusion
    "fusion_distance_mm": 500,
    "fusion_time_ms": 700,
}


def merged_params(params):
    p = dict(DEFAULT_PARAMS)
    for k, v in (params or {}).items():
        if k == "type_weight" and isinstance(v, dict):
            w = dict(DEFAULT_PARAMS["type_weight"])
            w.update({str(kk): float(vv) for kk, vv in v.items()})
            p[k] = w
        elif k in DEFAULT_PARAMS and not isinstance(DEFAULT_PARAMS[k], dict):
            p[k] = type(DEFAULT_PARAMS[k])(v)
    return p


def _weight(p, devices, sender):
    """Gewicht eines Senders über seinen Gerätetyp (aus xComDef6_3.h)."""
    if not devices:
        return 1.0                       # Gerätetabelle fehlt -> nicht alles abwürgen
    typ = (devices.get(sender) or devices.get(str(sender)) or {}).get("type", "")
    w = p["type_weight"]
    return float(w.get(typ, w.get("default", 1.0)))


# ---------------------------------------------------------------- Abdeckung

def _cov_finish(senders, cell_mm, source, sectors=None):
    """Gemeinsamer Abschluss beider Abdeckungs-Bauer: Union + Randzellen."""
    union = set().union(*senders.values()) if senders else set()
    boundary = []
    for (ix, iy) in union:
        if any((ix + dx, iy + dy) not in union
               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            boundary.append(((ix + 0.5) * cell_mm, (iy + 0.5) * cell_mm))
    return {"cell_mm": cell_mm, "senders": senders, "union": union,
            "boundary": boundary, "source": source, "sectors": sectors or {}}


def build_coverage(pts_by_sender, cell_mm, min_events):
    """Empirische Abdeckung aus (Langzeit-)Punkten je Sender (FALLBACK, wenn
    keine Sensor-Posen bekannt sind).

    pts_by_sender: {sender: [(wx,wy), ...]}  ->  {
      cell_mm, senders: {sender: set((ix,iy))}, union: set, boundary: [(cx,cy)mm]
    }"""
    senders = {}
    for sid, pts in pts_by_sender.items():
        cnt = {}
        for x, y in pts:
            c = (math.floor(x / cell_mm), math.floor(y / cell_mm))
            cnt[c] = cnt.get(c, 0) + 1
        senders[sid] = {c for c, n in cnt.items() if n >= min_events}
    return _cov_finish(senders, cell_mm, "empirisch")


def build_coverage_geo(devices, poses, cell_mm):
    """Geometrische Abdeckung aus den NOMINELLEN Erfassungsbereichen der
    xComDef (covL/covR in Grad relativ zur Blickrichtung, covRange in mm) und
    den Welt-Posen der Sensoren (poseReport über den Manager).

    devices: {sender: {"type","name","covL","covR","covRange"}}
    poses:   {sender: {"x","y","head","mir","valid"}}  head = PA-Einheiten 0..4096

    Rasterisiert jeden Sektor auf dasselbe Zellraster wie die empirische
    Abdeckung, damit Union/Rand/Edge-Distanz identisch weiterverwendet werden.
    Winkelkonvention wie auf den Geräten (toPol): phi = atan2(x_lokal, y_lokal),
    Blickrichtung = lokale +y-Achse, links negativ / rechts positiv."""
    senders, sectors = {}, {}
    for sid, dev in (devices or {}).items():
        rng = float(dev.get("covRange", 0) or 0)
        if rng <= 0:
            continue
        pose = (poses or {}).get(sid) or (poses or {}).get(str(sid))
        if not pose or not pose.get("valid"):
            continue
        px, py = float(pose["x"]), float(pose["y"])
        a = float(pose.get("head", 0)) * (2.0 * math.pi / 4096.0)
        mir = -1 if int(pose.get("mir", 1)) < 0 else 1
        ca, sa = math.cos(a), math.sin(a)
        left, right = float(dev.get("covL", -180)), float(dev.get("covR", 180))
        cells = set()
        r_cells = int(math.ceil(rng / cell_mm)) + 1
        cx0 = math.floor(px / cell_mm)
        cy0 = math.floor(py / cell_mm)
        for ix in range(cx0 - r_cells, cx0 + r_cells + 1):
            for iy in range(cy0 - r_cells, cy0 + r_cells + 1):
                wx = (ix + 0.5) * cell_mm
                wy = (iy + 0.5) * cell_mm
                dx, dy = wx - px, wy - py
                if dx * dx + dy * dy > rng * rng:
                    continue
                # worldToLocal (vgl. xComProc): xl = dx*c + dy*s, yl = mir*(-dx*s + dy*c)
                xl = dx * ca + dy * sa
                yl = mir * (-dx * sa + dy * ca)
                phi = math.degrees(math.atan2(xl, yl))
                if left <= phi <= right:
                    cells.add((ix, iy))
        if cells:
            senders[sid] = cells
            sectors[sid] = {"x": px, "y": py, "head": float(pose.get("head", 0)),
                            "mir": mir, "left": left, "right": right, "range": rng}
    return _cov_finish(senders, cell_mm, "xComDef", sectors)


def _edge_dist(cov, x, y):
    """Distanz eines Welt-Punkts zum Rand der Gesamtabdeckung (mm)."""
    if not cov or not cov["boundary"]:
        return None
    return min(math.hypot(x - bx, y - by) for bx, by in cov["boundary"])


def _in_union(cov, x, y):
    c = (math.floor(x / cov["cell_mm"]), math.floor(y / cov["cell_mm"]))
    return c in cov["union"]


# ---------------------------------------------------------------- Stürme

def detect_storms(events, p):
    """Sturm je Sender: hohe Ereignisrate UND grosse räumliche Streuung.

    Rate allein reicht nicht — ein Radar liefert beim Tracken einer Katze
    dieselben ~10 Hz wie ein Sonnen-Burst. Der Unterschied: die Katze ist zu
    jedem Zeitpunkt EIN kompakter Cluster, der Burst liegt gleichzeitig weit
    verstreut. Gemessen als BBox-Diagonale je 0,5-s-Zeitscheibe (Median über
    das Sturm-Fenster). Rückgabe {sender: [[t0,t1], ...]}."""
    win = p["storm_window_ms"] / 1000.0
    need = max(int(p["storm_min_events"]), int(p["storm_rate_evts_s"] * win))
    slot = 0.5
    slots_per_win = max(1, int(round(win / slot)))
    by_sender = {}
    for e in events:
        by_sender.setdefault(e["sender"], []).append((e["t"], e["wx"], e["wy"]))
    storms = {}
    for sid, pts in by_sender.items():
        pts.sort()
        # je 0,5-s-Zeitscheibe: Anzahl + Ausdehnung (BBox-Diagonale)
        buckets = {}
        for t, x, y in pts:
            b = buckets.setdefault(int(t // slot), [0, x, x, y, y])
            b[0] += 1
            b[1] = min(b[1], x); b[2] = max(b[2], x)
            b[3] = min(b[3], y); b[4] = max(b[4], y)
        if not buckets:
            continue
        ivs = []
        for k in range(min(buckets), max(buckets) + 1):
            ws = [buckets[i] for i in range(k, k + slots_per_win) if i in buckets]
            n = sum(b[0] for b in ws)
            if n < need:
                continue
            diags = sorted(math.hypot(b[2] - b[1], b[4] - b[3]) for b in ws)
            if diags[len(diags) // 2] < p["storm_scatter_mm"]:
                continue                          # kompakt = ein Objekt, kein Sturm
            a, b = k * slot, (k + slots_per_win) * slot
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
    """Events (zeitsortiert) -> Tracks; Punkt = (t,x,y,sender,sensor,speed)."""
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


def stitch_tracks(tracks, p):
    """Nähte: eine sitzende Katze verschwindet kurz aus dem Radar (kaum
    Mikrobewegung) und taucht am selben Ort wieder auf -> zusammenfügen."""
    gap = p["stitch_gap_ms"] / 1000.0
    dist = p["stitch_dist_mm"]
    out = []
    for tr in tracks:                            # tracks sind nach t0 sortiert
        merged = False
        for o in out:
            if 0 <= tr[0][0] - o[-1][0] <= gap and \
               math.hypot(tr[0][1] - o[-1][1], tr[0][2] - o[-1][2]) <= dist:
                o.extend(tr)
                merged = True
                break
        if not merged:
            out.append(tr)
    out.sort(key=lambda tr: tr[0][0])
    return out


# ---------------------------------------------------------------- Bewertung

def _speeds(pts):
    vs = []
    for a, b in zip(pts, pts[1:]):
        dt = b[0] - a[0]
        if dt > 0:
            vs.append(math.hypot(b[1] - a[1], b[2] - a[2]) / dt)
    return vs


def _find_move(pts, devices, p):
    """Kohärente Bewegungsphase (Fenster-Scan). None oder (t_ende, senders)."""
    win = p["move_window_ms"] / 1000.0
    j = 0
    for i in range(len(pts)):
        while pts[i][0] - pts[j][0] > win:
            j += 1
        w = pts[j:i + 1]
        if len(w) < p["move_min_points"]:
            continue
        if math.hypot(w[-1][1] - w[0][1], w[-1][2] - w[0][2]) < p["move_min_net_mm"]:
            continue
        vs = _speeds(w)
        if not vs:
            continue
        ok = sum(1 for v in vs if p["speed_min_mm_s"] <= v <= p["speed_max_mm_s"])
        if ok / len(vs) < p["speed_ok_ratio"]:
            continue
        senders = {pt[3] for pt in w}
        if sum(_weight(p, devices, s) for s in senders) < p["min_confirm_weight"]:
            continue
        return w[-1][0], senders
    return None


def _has_fusion(pts, storms, p):
    fd, ft = p["fusion_distance_mm"], p["fusion_time_ms"] / 1000.0
    last = {}
    for pt in pts:
        for sid, o in last.items():
            if sid != pt[3] and pt[0] - o[0] <= ft and \
               math.hypot(pt[1] - o[1], pt[2] - o[2]) <= fd:
                return True
        last[pt[3]] = pt
    return False


def _stationary_end(pts, p):
    """Sitzen die letzten stationary_end_s Sekunden stabil am Ort?"""
    t_end = pts[-1][0]
    tail = [pt for pt in pts if t_end - pt[0] <= p["stationary_end_s"]]
    if len(tail) < 3 or t_end - tail[0][0] < p["stationary_end_s"] * 0.6:
        return False
    cx = sum(pt[1] for pt in tail) / len(tail)
    cy = sum(pt[2] for pt in tail) / len(tail)
    return all(math.hypot(pt[1] - cx, pt[2] - cy) <= p["stationary_radius_mm"]
               for pt in tail)


def score_track(pts, storms, cov, devices, p, tid, w_t0, w_t1):
    """Score 0..100 mit Aufschlüsselung. w_t0/w_t1 = Analysefenster (damit an
    den Fensterrändern kein falscher Eintritts-/Austritts-Malus entsteht)."""
    gap = p["track_gap_ms"] / 1000.0
    clean = [pt for pt in pts if not in_storm(storms, pt[3], pt[0])]
    vs_all = _speeds(pts)
    feats = {
        "id": tid, "t0": pts[0][0], "t1": pts[-1][0], "n": len(pts),
        # stabiler Schlüssel für manuelle Markierungen (Katze/keine Katze):
        # Geburtszeit (ms) + Geburtsort identifizieren den Track auch dann noch,
        # wenn das Analysefenster anders liegt (die id ist nur der Fenster-Index)
        "key": "%d_%d_%d" % (round(pts[0][0] * 1000), pts[0][1], pts[0][2]),
        "net_mm": round(math.hypot(pts[-1][1] - pts[0][1], pts[-1][2] - pts[0][2])),
        "path_mm": round(sum(math.hypot(b[1] - a[1], b[2] - a[2])
                             for a, b in zip(pts, pts[1:]))),
        "v_mean": round(sum(vs_all) / len(vs_all)) if vs_all else 0,
        "v_max": round(max(vs_all)) if vs_all else 0,
        "senders": sorted({pt[3] for pt in pts}),
        "storm_ratio": round(1 - len(clean) / len(pts), 2),
        "flags": [],
    }
    detail = []            # [label, +/-punkte]
    score = 0

    def add(label, points):
        nonlocal score
        score += points
        detail.append([label, points])

    move = _find_move(clean, devices, p) if clean else None
    t_confirm = None
    if move:
        t_confirm = move[0]
        add("kohärente Bewegung", p["score_move"])
        dur = feats["t1"] - feats["t0"]
        if feats["net_mm"] >= p["crossing_net_mm"]:
            add("durchquert das Feld", p["score_crossing"])
        if len(clean) >= p["long_track_points"]:
            add("langer Track", p["score_long"])
        if dur * 1000 < p["short_track_ms"]:
            add("sehr kurzer Track", -p["penalty_short"])
    else:
        detail.append(["keine kohärente Bewegung (Pflicht)", 0])

    if clean and _has_fusion(clean, storms, p):
        add("mehrere Sensoren sehen dasselbe", p["score_fusion"])
        feats["flags"].append("FUSION")

    # Erfassungsgrenzen: nur wenn genug empirische Abdeckung vorhanden ist.
    edge_active = bool(cov) and len(cov.get("union", ())) >= p["cov_min_cells"]
    feats["edge_active"] = edge_active
    if edge_active and clean:
        b, d = clean[0], clean[-1]
        # Fensterrand-Schutz: Track schon am Anfang aktiv / am Ende noch offen?
        birth_cut = b[0] <= w_t0 + 2 * gap
        death_open = d[0] >= w_t1 - 2 * gap
        if not birth_cut:
            ed = _edge_dist(cov, b[1], b[2])
            if ed is not None and ed <= p["edge_dist_mm"]:
                add("Eintritt am Rand der Abdeckung", p["score_entry"])
                feats["flags"].append("EINTRITT")
            elif _in_union(cov, b[1], b[2]):
                add("mitten in der Abdeckung aufgetaucht", -p["penalty_mid_birth"])
        if death_open:
            feats["flags"].append("OFFEN")       # läuft noch — kein Austritts-Urteil
        else:
            ed = _edge_dist(cov, d[1], d[2])
            if ed is not None and ed <= p["edge_dist_mm"]:
                add("Austritt am Rand der Abdeckung", p["score_exit"])
                feats["flags"].append("AUSTRITT")
            elif _in_union(cov, d[1], d[2]) and not _stationary_end(pts, p):
                add("mitten in der Abdeckung verschwunden", -p["penalty_mid_death"])

    if _stationary_end(pts, p):
        feats["flags"].append("STATIONAER")      # sitzt — koten? Ziel nicht verlieren!
        if move:
            add("sitzt nach Bewegung stabil (koten?)", p["score_stationary"])

    if vs_all:
        jumpy = sum(1 for v in vs_all if v > p["speed_max_mm_s"] * 1.5)
        if jumpy / len(vs_all) > 0.2:
            add("unphysikalische Sprünge", -p["penalty_jumpy"])

    # RoboMäher/Person: ein einzelner kohärenter Track mit sehr langem Weg ist
    # keine Katze (Mäher mäht systematisch, Personen gehen Runden). Hartes K.o.
    too_far = p["max_path_mm"] > 0 and feats["path_mm"] > p["max_path_mm"]
    if too_far:
        add("Weg zu lang (%d m) - Mäher/Person?" % round(feats["path_mm"] / 1000), -100)

    score = max(0, min(100, score))
    confirmed = bool(move) and score >= p["confirm_score"] and not too_far
    feats["score"] = score
    feats["score_detail"] = detail
    feats["confirmed"] = confirmed
    feats["t_confirm"] = t_confirm if confirmed else None
    if confirmed:
        feats["reject"] = None
    elif not clean:
        feats["reject"] = "Sturm/Burst"
    elif too_far:
        feats["reject"] = "Weg zu lang (Mäher/Person?)"
    elif not move:
        feats["reject"] = ("Einzelereignis" if feats["n"] == 1 else
                           "keine kohärente Bewegung")
    else:
        feats["reject"] = "Score zu tief (%d < %d)" % (score, p["confirm_score"])
    # Punkte fürs UI (grosse Tracks ausdünnen, Anfang/Ende behalten)
    out = pts
    if len(pts) > 800:
        step = len(pts) // 800 + 1
        out = pts[::step]
        if out[-1] is not pts[-1]:
            out.append(pts[-1])
    feats["pts"] = [[round(pt[0], 3), pt[1], pt[2], pt[3], pt[4]] for pt in out]
    return feats


def analyze(events, params=None, devices=None, coverage=None, exclude=None):
    """events: [{t,sender,sensor,wx,wy,speed}] (nur worldValid).
    devices:  {sender: {"name","type"}} — dynamisch aus xComDef6_3.h.
    coverage: build_coverage()-Ergebnis aus den LANGZEIT-Daten (nicht nur dem
    Fenster), oder None (Randlogik dann neutral).
    exclude:  [(t0,t1), ...] — Zeitfenster, die NICHT analysiert werden (z.B.
    per Label „Mäher" markierte RoboMäher-Läufe). Die Events bleiben in der
    Aufnahme und in der Karten-/Zeitleisten-Darstellung — sie fließen nur nicht
    in Tracking/Bestätigung ein."""
    p = merged_params(params)
    n_raw = len(events)
    if exclude:
        events = [e for e in events
                  if not any(a <= e["t"] <= b for a, b in exclude)]
    evs = sorted(events, key=lambda e: e["t"])
    storms = detect_storms(evs, p)
    tracks = stitch_tracks(build_tracks(evs, p), p)
    w_t0 = evs[0]["t"] if evs else 0.0
    w_t1 = evs[-1]["t"] if evs else 0.0
    scored = [score_track(tr, storms, coverage, devices, p, i, w_t0, w_t1)
              for i, tr in enumerate(tracks)]
    return {
        "tracks": scored,
        "storms": {str(k): v for k, v in storms.items()},
        "n_events": len(evs),
        "n_excluded": n_raw - len(evs),
        "n_confirmed": sum(1 for t in scored if t["confirmed"]),
        "edge_active": bool(coverage) and
                       len(coverage.get("union", ())) >= p["cov_min_cells"],
        "cov_source": (coverage or {}).get("source", ""),
        "params": p,
    }
