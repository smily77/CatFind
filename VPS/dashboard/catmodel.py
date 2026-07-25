#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CatFinder Erkennungsmodell v2 (Analyse auf dem VPS).

Bewertet Tracks aus catObserved-Events als SCORE (0..100, "Katzen-
Wahrscheinlichkeit") statt mit starren Regeln. Kernideen (GesamtKonzept,
Aufgabe "VPS-Modellierung der Katzenerkennung"):

  - IDENTISCH zum CatIdentifier (catTrack.ino): das VPS spielt jeden Track
    KAUSAL Punkt für Punkt durch (score_track) und bestätigt in genau dem
    Moment, in dem der laufende Score confirm_score erreicht (t_confirm) — mit
    ausschliesslich den bis dahin bekannten Merkmalen, wie das Gerät im Feld.
    Deshalb gibt es KEINEN Austritts-Bonus/-Malus und keinen Sprung-Malus (die
    setzten den fertigen Track voraus). So zeigt der Analyse-Tab exakt, was der
    CatIdentifier entscheiden würde, und das gemeinsame Modell lässt sich daran
    justieren. Die Ruhefenster-Sperre (quiet_t0/t1) bleibt bewusst NUR auf dem
    Gerät — das VPS analysiert auch die Mäherzeit, damit man sie tunen kann.

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
Abdeckung) -> (Tracks mit Score-Aufschlüsselung); die maßgebliche Referenz für
die ESP32-Implementierung (catTrack.ino spiegelt score_track 1:1).
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
    # KAUSAL wie der CatIdentifier: nur Merkmale, die WÄHREND des Tracks schon
    # feststehen. Ein Austritts-Bonus/-Malus (score_exit/penalty_mid_death) und
    # der Sprung-Malus (penalty_jumpy) gibt es bewusst NICHT — das Gerät kann den
    # Track-Tod nicht abwarten und das VPS soll dieselbe Entscheidung treffen.
    "confirm_score": 60,
    "score_move": 50,               # kohärente Bewegung gefunden
    "score_entry": 18,              # Geburt nahe Rand der Gesamtabdeckung
    "score_fusion": 25,             # >=2 Sender sehen dasselbe Objekt
    "score_crossing": 12,           # grosse Netto-Verschiebung (>= crossing_net_mm)
    "score_long": 8,                # langer Track (>= long_track_points saubere Punkte)
    "score_stationary": 8,          # sitzt stabil (koten!) nach Bewegung
    "penalty_mid_birth": 12,        # mitten in der Abdeckung aufgetaucht (weich)
    "penalty_short": 10,            # sehr kurzer Track (Vogel/Zufall)
    "crossing_net_mm": 800,
    "long_track_points": 10,
    "short_track_ms": 800,
    # Weg-Obergrenze: RoboMäher/Personen laufen in EINEM Track hunderte Meter
    # zusammenhängend ab — eine Katze nicht. Längerer Pfad => keine Katze. 0 = aus.
    "max_path_mm": 40000,
    # --- NoShot-Zeit (Mäher-Ruhefenster): lokale Minuten seit Mitternacht.
    # Im Fenster feuert der CatIdentifier NIE (Modell pausiert) — ein Fehlschuss
    # auf den Mäher wäre teuer (Regensensor -> er stellt die Arbeit ein), eine
    # im Fenster verpasste Katze nicht (Katzen meiden den laufenden Mäher ohnehin).
    # t0 == t1 = deaktiviert; t0 > t1 läuft über Mitternacht. Wird NUR vom
    # CatIdentifier (Echtzeit) als Feuer-Sperre angewendet — die VPS-Analyse rechnet
    # bewusst auch die Mäherzeit durch, damit man im Analyse-Tab sieht, was das (sonst
    # identische) Modell dort erkennen würde, und die Katze/Mäher-Trennung tunen kann.
    "quiet_t0_min": 900,            # 15:00 lokale Zeit (Mäherstart)
    "quiet_t1_min": 990,            # 16:30 (Mäher lädt spätestens dann wieder)
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
            d = DEFAULT_PARAMS[k]
            # int-Parameter RUNDEN statt abschneiden (59.9 wurde sonst still zu 59)
            p[k] = int(round(float(v))) if isinstance(d, int) else float(v)
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



def pose_hash(pose):
    """Stabiler Fingerprint einer Welt-Pose fuer posegebundene Coverage-Profile."""
    if not pose or not pose.get("valid"):
        return ""
    return "%d:%d:%.3f:%d" % (int(pose.get("x", 0)), int(pose.get("y", 0)),
                                float(pose.get("head", 0)), int(pose.get("mir", 1)))


def polygon_area(poly):
    if len(poly) < 3:
        return 0.0
    a = 0.0
    for i, (x1, y1) in enumerate(poly):
        x2, y2 = poly[(i + 1) % len(poly)]
        a += x1 * y2 - x2 * y1
    return abs(a) * 0.5


def point_in_poly(x, y, poly):
    inside = False
    if len(poly) < 3:
        return False
    j = len(poly) - 1
    for i, (xi, yi) in enumerate(poly):
        xj, yj = poly[j]
        if (yi > y) != (yj > y):
            xint = (xj - xi) * (y - yi) / ((yj - yi) or 1e-9) + xi
            if x < xint:
                inside = not inside
        j = i
    return inside


def _rdp(points, eps):
    if len(points) <= 2:
        return points[:]
    ax, ay = points[0]; bx, by = points[-1]
    den = math.hypot(bx - ax, by - ay) or 1.0
    best_i, best_d = 0, -1.0
    for i, (px, py) in enumerate(points[1:-1], 1):
        d = abs((by - ay) * px - (bx - ax) * py + bx * ay - by * ax) / den
        if d > best_d:
            best_i, best_d = i, d
    if best_d > eps:
        return _rdp(points[:best_i + 1], eps)[:-1] + _rdp(points[best_i:], eps)
    return [points[0], points[-1]]


def simplify_polygon(poly, min_pts=6, max_pts=10):
    if len(poly) <= max_pts:
        return poly[:]
    closed = poly + [poly[0]]
    lo, hi = 0.0, max(max(abs(x) for x, _ in poly), max(abs(y) for _, y in poly), 1.0)
    best = poly[:]
    for _ in range(24):
        mid = (lo + hi) / 2.0
        simp = _rdp(closed, mid)[:-1]
        if len(simp) > max_pts:
            lo = mid
        else:
            hi = mid
            if len(simp) >= min_pts:
                best = simp
    if len(best) > max_pts:
        # Fallback: gleichmaessig ausduennen, damit der ESP32 kleine Polygone bekommt.
        step = len(best) / max_pts
        best = [best[int(i * step) % len(best)] for i in range(max_pts)]
    return best


def _quantile(vals, q):
    vals = sorted(vals)
    if not vals:
        return 0.0
    pos = (len(vals) - 1) * q
    lo = int(math.floor(pos)); hi = int(math.ceil(pos))
    if lo == hi:
        return vals[lo]
    return vals[lo] * (hi - pos) + vals[hi] * (pos - lo)


def learn_mower_polygon(points, pose, angle_bin_deg=10.0, quantile=0.90,
                        min_points=40, min_bins=4, target_vertices=8):
    """Erzeugt ein einfaches Weltpolygon aus Mäherpunkten (wx,wy).

    Variante B: Winkel-Bins um den Sensorstandort, robuste Maximaldistanz je
    Bin, danach Vereinfachung auf 6..10 Punkte. Die Punkte sind bereits die vom
    Sensor gemeldeten Weltpunkte (inkl. RasenMap-Filterung auf dem Radar)."""
    if not pose or not pose.get("valid"):
        return None, {"ok": False, "reason": "keine gueltige Pose"}
    px, py = float(pose["x"]), float(pose["y"])
    pts = [(float(x), float(y)) for x, y in points]
    if len(pts) < min_points:
        return None, {"ok": False, "reason": "zu wenige Punkte", "point_count": len(pts)}
    bins = {}
    step = max(float(angle_bin_deg), 1.0)
    for x, y in pts:
        dx, dy = x - px, y - py
        r = math.hypot(dx, dy)
        if r < 100:       # Sensor-Eigenrauschen im Ursprung ignorieren
            continue
        deg = math.degrees(math.atan2(dx, dy))  # wie Radar: 0 = lokale/world +y Richtung
        b = int(math.floor((deg + 180.0) / step))
        bins.setdefault(b, []).append(r)
    used = [(b, _quantile(rs, min(max(float(quantile), 0.5), 0.99)))
            for b, rs in bins.items() if len(rs) >= 2]
    used.sort()
    if len(used) < min_bins:
        return None, {"ok": False, "reason": "zu wenige Winkel-Bins",
                      "point_count": len(pts), "bin_count": len(used)}
    # Deckt der Sensor nur einen Teilkreis ab (deutliche Winkelluecke), muss der
    # SENSOR-STANDORT selbst Polygon-Scheitel sein - sonst schneidet die Sehne
    # zwischen den Randwinkeln das Nahfeld direkt vor dem Sensor ab (eine Katze
    # 2 m vor dem Radar laege "ausserhalb der Abdeckung"). Vgl. sector_polygon.
    nbins_full = max(int(round(360.0 / step)), 1)
    bidx = [b for b, _ in used]
    gap_after = [((bidx[(i + 1) % len(bidx)] - bidx[i]) % nbins_full) or nbins_full
                 for i in range(len(bidx))]
    gi = max(range(len(gap_after)), key=lambda i: gap_after[i])
    hull = []
    if gap_after[gi] * step >= 60.0:            # Teilkreis: Faecher mit Sensor als Scheitel
        order = used[gi + 1:] + used[:gi + 1]
        hull.append((px, py))                   # bleibt als poly[0] auch nach simplify erhalten
    else:                                       # Rundum-Abdeckung: geschlossener Ring
        order = used
    for b, r in order:
        deg = -180.0 + (b + 0.5) * step
        rad = math.radians(deg)
        hull.append((px + math.sin(rad) * r, py + math.cos(rad) * r))
    poly = simplify_polygon(hull, max(3, target_vertices - 2), min(10, max(6, target_vertices + 2)))
    if polygon_area(poly) <= 1.0:
        return None, {"ok": False, "reason": "Polygonflaeche zu klein",
                      "point_count": len(pts), "bin_count": len(used)}
    q = {"ok": True, "point_count": len(pts), "bin_count": len(used),
         "area_mm2": polygon_area(poly), "angle_bin_deg": step,
         "quantile": float(quantile), "vertices": len(poly)}
    return [[int(round(x)), int(round(y))] for x, y in poly], q


def sector_polygon(dev, pose, arc_step_deg=12.0, max_pts=16):
    """Default-xComDef-Sektor als Weltpolygon, damit alle Coverage-Quellen als
    Polygonliste exportierbar sind (ESP32: inside-any-polygon).

    max_pts begrenzt die Gesamtpunktzahl (Ursprung + Bogen) aufs ESP32-Budget:
    ein 360-Grad-Lidar bekaeme sonst 32 Ecken, und ein hartes Abschneiden im
    Export wuerde den Sektor zum Halbkreis machen."""
    rng = float(dev.get("covRange", 0) or 0)
    if rng <= 0 or not pose or not pose.get("valid"):
        return []
    px, py = float(pose["x"]), float(pose["y"])
    a = float(pose.get("head", 0)) * (2.0 * math.pi / 4096.0)
    mir = -1 if int(pose.get("mir", 1)) < 0 else 1
    ca, sa = math.cos(a), math.sin(a)
    left, right = float(dev.get("covL", -180)), float(dev.get("covR", 180))
    span = max(right - left, 1.0)
    n = max(2, int(math.ceil(span / max(arc_step_deg, 2.0))))
    n = min(n, max(2, int(max_pts) - 2))     # Ursprung + n+1 Bogenpunkte <= max_pts
    poly = [[int(round(px)), int(round(py))]]
    for i in range(n + 1):
        deg = left + span * i / n
        rad = math.radians(deg)
        xl, yl = math.sin(rad) * rng, math.cos(rad) * rng
        ym = mir * yl
        wx = px + xl * ca - ym * sa
        wy = py + xl * sa + ym * ca
        poly.append([int(round(wx)), int(round(wy))])
    return poly


def build_coverage_polygons(poly_by_sender, cell_mm, source="polygon"):
    """Rasterisiert Weltpolygone in dieselbe Coverage-Struktur wie Sektoren."""
    senders, polygons = {}, {}
    for sid, poly in (poly_by_sender or {}).items():
        if not poly or len(poly) < 3:
            continue
        minx, maxx = min(x for x, _ in poly), max(x for x, _ in poly)
        miny, maxy = min(y for _, y in poly), max(y for _, y in poly)
        cells = set()
        for ix in range(math.floor(minx / cell_mm) - 1, math.floor(maxx / cell_mm) + 2):
            for iy in range(math.floor(miny / cell_mm) - 1, math.floor(maxy / cell_mm) + 2):
                wx, wy = (ix + 0.5) * cell_mm, (iy + 0.5) * cell_mm
                if point_in_poly(wx, wy, poly):
                    cells.add((ix, iy))
        if cells:
            senders[int(sid)] = cells
            polygons[int(sid)] = poly
    cov = _cov_finish(senders, cell_mm, source)
    cov["polygons"] = polygons
    return cov

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
        # nur BELEGTE Slots als Fensterstart: range(min,max) waere bei duennen
        # Daten ueber Wochen Millionen Leer-Iterationen pro Sender (Backfill);
        # ein Fenster ab leerem Slot kann die Schwelle ohnehin kaum erreichen.
        for k in sorted(buckets):
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
    Mikrobewegung) und taucht am selben Ort wieder auf -> zusammenfügen.
    Kandidaten werden über ihre Endzeit ausgedünnt (sonst O(n²) — wichtig,
    seit der Hintergrund-Analysierer grosse Häppchen am Stück verarbeitet)."""
    gap = p["stitch_gap_ms"] / 1000.0
    dist = p["stitch_dist_mm"]
    out, cand = [], []               # cand = Tracks, deren Ende noch in Reichweite
    for tr in tracks:                            # tracks sind nach t0 sortiert
        t0 = tr[0][0]
        cand = [o for o in cand if t0 - o[-1][0] <= gap]
        for o in cand:
            if t0 >= o[-1][0] and \
               math.hypot(tr[0][1] - o[-1][1], tr[0][2] - o[-1][2]) <= dist:
                o.extend(tr)
                break
        else:
            out.append(tr)
            cand.append(tr)
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


def _stationary_now(pts, k, p):
    """Sitzt der Track im Moment von Punkt k stabil? Kausale Variante von
    _stationary_end (Tail = Punkte innerhalb stationary_end_s VOR/bis k)."""
    t_end = pts[k][0]
    lo = k
    while lo > 0 and t_end - pts[lo - 1][0] <= p["stationary_end_s"]:
        lo -= 1
    tail = pts[lo:k + 1]
    if len(tail) < 3 or t_end - tail[0][0] < p["stationary_end_s"] * 0.6:
        return False
    cx = sum(pt[1] for pt in tail) / len(tail)
    cy = sum(pt[2] for pt in tail) / len(tail)
    return all(math.hypot(pt[1] - cx, pt[2] - cy) <= p["stationary_radius_mm"]
               for pt in tail)


def _signed_edge(cov, x, y):
    """Signierte Grenzdistanz wie ctUnionBorderDist auf dem ESP32: >0 = drinnen
    (Tiefe bis zum Rand), <0 = draussen (Abstand zum Rand), None = keine
    Abdeckung. Aus der Raster-Abdeckung der VPS (Boundary-Zellen + Union)."""
    ed = _edge_dist(cov, x, y)
    if ed is None:
        return None
    return ed if _in_union(cov, x, y) else -ed


def score_track(pts, storms, cov, devices, p, tid, w_t0, w_t1):
    """KAUSALE Bewertung — treuer Nachbau der Echtzeit-Erkennung des
    CatIdentifier (catTrack.ino, ctFeed/ctEvaluate). Statt den fertigen Track als
    Ganzes zu bewerten (retrospektiv), wird er Punkt für Punkt durchgespielt und
    die Bestätigung fällt GENAU in dem Moment, in dem der laufende Score zum
    ersten Mal confirm_score erreicht (t_confirm) — mit ausschliesslich den bis
    dahin bekannten Merkmalen. Damit fehlt (wie auf dem Gerät) jeder Austritts-
    Bonus/-Malus und der Sprung-Malus; so zeigt der Analyse-Tab genau das, was
    das Gerät im Feld entscheiden würde, und das Modell lässt sich daran justieren.

    w_t0 wird nicht mehr gebraucht (das Gerät hat immer eine echte Geburt);
    w_t1 markiert nur noch einen am Fensterende offenen Track als OFFEN."""
    gap = p["track_gap_ms"] / 1000.0
    # Sturmpunkte fallen VOR dem Tracking weg (ctFeed kehrt bei Sturm sofort um) —
    # der Durchlauf sieht nur die sturmbereinigten Punkte, wie auf dem Gerät.
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
    edge_active = bool(cov) and len(cov.get("union", ())) >= p["cov_min_cells"]
    feats["edge_active"] = edge_active

    # Geburt: Randlogik einmalig am ersten (sturmbereinigten) Punkt festhalten,
    # so wie das Gerät birthValid/birthDist bei der Track-Geburt einfriert.
    birth_signed = None
    if clean and edge_active:
        birth_signed = _signed_edge(cov, clean[0][1], clean[0][2])
    if birth_signed is not None and abs(birth_signed) <= p["edge_dist_mm"]:
        feats["flags"].append("EINTRITT")         # nahe am Rand geboren (hereingelaufen)

    # move wird sticky, sobald irgendein Fenster (endend am jeweils neusten Punkt)
    # eine kohärente Bewegung zeigt — _find_move liefert genau diesen Zeitpunkt.
    move = _find_move(clean, devices, p) if clean else None
    move_t = move[0] if move else None

    st = p["confirm_score"]                        # Bestätigungsschwelle
    stat_gate = st - p["score_stationary"]         # ab hier kann Sitzen kippen/zählen
    reported = False
    dead_far = False
    fusion = False
    lo_fus = 0
    path = 0.0
    x0, y0, t0c = (clean[0][1], clean[0][2], clean[0][0]) if clean else (0, 0, 0.0)
    t_confirm = None
    conf_score, conf_detail = 0, None
    best_score, best_detail = -1, None
    fus_dist = p["fusion_distance_mm"]
    fus_win = p["fusion_time_ms"] / 1000.0

    for k in range(len(clean)):
        t, x, y, snd = clean[k][0], clean[k][1], clean[k][2], clean[k][3]
        if k > 0:
            path += math.hypot(x - clean[k - 1][1], y - clean[k - 1][2])
        # Fusion (sticky): anderer Sender sah fast gleichzeitig fast denselben Ort
        if not fusion:
            while clean[lo_fus][0] < t - fus_win:
                lo_fus += 1
            for j in range(lo_fus, k):
                if clean[j][3] != snd and \
                   math.hypot(x - clean[j][1], y - clean[j][2]) <= fus_dist:
                    fusion = True
                    break
        if reported:
            continue                               # ctEvaluate kehrt nach dem Melden sofort um
        # Mäher/Person: hartes K.o., BEVOR ein Score entsteht (wie ctEvaluate)
        if p["max_path_mm"] > 0 and path > p["max_path_mm"]:
            dead_far = True
            break
        if move_t is None or t < move_t:
            continue                               # Pflicht: kohärente Bewegung gefunden
        # ---- laufender Score mit ausschliesslich kausalen Merkmalen ----
        detail = [["kohärente Bewegung", p["score_move"]]]
        score = p["score_move"]
        if math.hypot(x - x0, y - y0) >= p["crossing_net_mm"]:
            score += p["score_crossing"]
            detail.append(["durchquert das Feld", p["score_crossing"]])
        if (k + 1) >= p["long_track_points"]:
            score += p["score_long"]
            detail.append(["langer Track", p["score_long"]])
        if t - t0c < p["short_track_ms"] / 1000.0:
            score -= p["penalty_short"]
            detail.append(["sehr kurzer Track", -p["penalty_short"]])
        if fusion:
            score += p["score_fusion"]
            detail.append(["mehrere Sensoren sehen dasselbe", p["score_fusion"]])
        if birth_signed is not None:
            if abs(birth_signed) <= p["edge_dist_mm"]:
                score += p["score_entry"]
                detail.append(["Eintritt am Rand der Abdeckung", p["score_entry"]])
            elif birth_signed > p["edge_dist_mm"]:
                score -= p["penalty_mid_birth"]
                detail.append(["mitten in der Abdeckung aufgetaucht",
                               -p["penalty_mid_birth"]])
        # Sitzen nur prüfen, wenn es das Ergebnis noch kippen könnte (spart O(n²))
        if score >= stat_gate and _stationary_now(clean, k, p):
            score += p["score_stationary"]
            detail.append(["sitzt stabil (koten?)", p["score_stationary"]])
        score = max(0, min(100, score))
        if score > best_score:
            best_score, best_detail = score, detail
        if score >= st:
            reported = True
            t_confirm, conf_score, conf_detail = t, score, detail

    if _stationary_end(pts, p):
        feats["flags"].append("STATIONAER")        # sitzt — koten? Ziel nicht verlieren!
    if fusion:
        feats["flags"].append("FUSION")
    if clean and clean[-1][0] >= w_t1 - 2 * gap:
        feats["flags"].append("OFFEN")             # läuft am Fensterende evtl. noch

    feats["confirmed"] = reported
    feats["t_confirm"] = t_confirm if reported else None
    if reported:
        feats["score"] = conf_score
        feats["score_detail"] = conf_detail
        feats["reject"] = None
    else:
        feats["score"] = max(0, best_score)
        feats["score_detail"] = best_detail or [["keine kohärente Bewegung (Pflicht)", 0]]
        if not clean:
            feats["reject"] = "Sturm/Burst"
        elif dead_far:
            feats["reject"] = "Weg zu lang (Mäher/Person?)"
        elif move_t is None:
            feats["reject"] = ("Einzelereignis" if feats["n"] == 1 else
                               "keine kohärente Bewegung")
        else:
            feats["reject"] = "Score zu tief (%d < %d)" % (max(0, best_score), st)
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


def analyze_stream(events, params=None, devices=None, coverage=None,
                   exclude=None, final_before=None):
    """Kontinuierliche Analyse für den Hintergrund-Analysierer im Dashboard:
    wie analyze(), aber OHNE Betrachtungsfenster-Logik — die Trackerkennung
    läuft fortlaufend über die Aufnahme, völlig unabhängig davon, was im
    Browser gerade angeschaut wird (dasselbe Modell, das später auf dem ESP32
    in Echtzeit laufen soll).

    final_before: Tracks, deren letzter Punkt davor liegt, sind FERTIG
    (bekommen in der DB eine fortlaufende Nummer); jüngere Tracks laufen
    möglicherweise noch und bleiben provisorisch (Flag OFFEN, kein
    Austritts-Urteil). Rückgabe {"final": [...], "prov": [...], "storms": {}}."""
    p = merged_params(params)
    if exclude:
        events = [e for e in events
                  if not any(a <= e["t"] <= b for a, b in exclude)]
    evs = sorted(events, key=lambda e: e["t"])
    storms = detect_storms(evs, p)
    tracks = stitch_tracks(build_tracks(evs, p), p)
    fb = float("inf") if final_before is None else float(final_before)
    final, prov = [], []
    for tr in tracks:
        if tr[-1][0] < fb:
            # fertig: Geburt/Tod sind echt beobachtet -> volle Randlogik
            final.append(score_track(tr, storms, coverage, devices, p, 0,
                                     -1e18, 1e18))
        else:
            # läuft evtl. noch: w_t1 = fb setzt das Flag OFFEN (kein Austritts-Malus)
            prov.append(score_track(tr, storms, coverage, devices, p, 0,
                                    -1e18, fb))
    return {"final": final, "prov": prov, "storms": storms, "params": p}
