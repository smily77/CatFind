"""Boden-Homographie der Kamera (Phase 4): Bildpunkt <-> Rasen-Welt (mm).

Kalibriert wird nicht von Hand, sondern aus pairs.jsonl: jede Zeile ist eine
Radar-Meldung mit Weltposition plus die Kamera-Spuren, die sich in dem Moment
bewegten. Zeilen mit genau EINER (mind. 1 s alten) Spur ergeben ein Paar
Fusspunkt <-> Weltpunkt. Rund die Haelfte davon sind Fehlpaare (Kamera und
Radar sahen gerade Verschiedenes, z.B. Maeher und Katze) - RANSAC wirft sie
raus. Nachgerechnet am 2026-10-17 mit 10 800 Paaren: ~40 % passen, deren
Fehler im Median ~210 mm (so genau wie das Radar selbst), und eine Haelfte der
Zeit sagt die andere gleich gut voraus.

Bewusst (noch) nicht:
  * Linsenverzeichnung - die Abweichung bleibt im Radarrauschen; erst wenn das
    eingeblendete Gitter am Rand sichtbar danebenliegt, lohnt sie.
  * Kamera-Pose (Standort/Hoehe) - aus der Homographie zerlegt widersprechen
    sich die beiden Brennweiten-Schaetzungen (Verzeichnung), poseReport bleibt
    darum vorerst ungueltig.
  * LidarC1 (17): nur ~9 % seiner Paare passen, er verwaessert die Rechnung.

Koordinaten: Bildpunkte normiert 0..1 (wie "foot" der Bewegungserkennung,
unabhaengig von der Aufloesung). Intern wird in einem 1640x1232-Raster
gerechnet, damit die RANSAC-Schwelle sich auf Pixel und Millimeter bezieht.
"""

import json
import math
import time
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError:                       # ohne OpenCV: Phase 4 einfach aus
    cv2 = None

FIT_SENDERS = (1, 2, 3, 18, 21)           # Radare (+ CatIdent, rechnet mit Radar-Welt)
MIN_AGE = 1.0                             # s - frische Spuren sind oft Rauschen
RANSAC_MM = 400                           # Paar zaehlt, wenn es so nah liegt
MIN_INLIERS = 200
GRID_MM = 1000
STEP_MM = 150                             # Linien so fein zerlegt projizieren
_FRAME = (1640.0, 1232.0)


def _rows(paths):
    for path in paths:
        try:
            fh = Path(path).open()
        except OSError:
            continue
        with fh:
            for line in fh:
                try:
                    yield json.loads(line)
                except ValueError:        # abgerissene Zeile beim Rotieren
                    continue


def pairs(paths, senders=FIT_SENDERS):
    """Paare (Fusspunkt in Pixel des 1640er-Rasters, Welt-mm, Zeit, Sender)."""
    src, dst, t, snd = [], [], [], []
    for r in _rows(paths):
        if senders is not None and r.get("sender") not in senders:
            continue
        tracks = [k for k in r.get("tracks", []) if k.get("age", 0) >= MIN_AGE]
        if len(tracks) != 1:
            continue
        fx, fy = tracks[0]["foot"]
        src.append((fx * _FRAME[0], fy * _FRAME[1]))
        dst.append((r["wx"], r["wy"]))
        t.append(r["t"])
        snd.append(r["sender"])
    return np.float64(src), np.float64(dst), np.float64(t), np.int32(snd)


def fit(paths, flips):
    """Homographie aus den Paaren rechnen. Liefert das Ergebnis-dict (zum
    Speichern) oder wirft ValueError mit einer lesbaren Begruendung."""
    if cv2 is None:
        raise ValueError("OpenCV fehlt")
    src, dst, t, _ = pairs(paths)
    if len(src) < MIN_INLIERS:
        raise ValueError("zu wenig Paare (%d)" % len(src))
    h_px, mask = cv2.findHomography(src, dst, cv2.RANSAC, RANSAC_MM,
                                    maxIters=20000, confidence=0.999)
    if h_px is None:
        raise ValueError("keine Homographie gefunden")
    inl = mask.ravel().astype(bool)
    if inl.sum() < MIN_INLIERS:
        raise ValueError("nur %d passende Paare von %d" % (inl.sum(), len(src)))
    err = _err(h_px, src, dst)
    # Gegenprobe: mit der ersten Haelfte der Zeit rechnen, an der zweiten
    # pruefen. Faellt das deutlich ab, hat sich die Kamera bewegt.
    half = t < np.median(t)
    check = None
    h_a, m_a = cv2.findHomography(src[half], dst[half], cv2.RANSAC, RANSAC_MM,
                                  maxIters=20000, confidence=0.999)
    if h_a is not None:
        e = _err(h_a, src[~half], dst[~half])
        ok = e < RANSAC_MM
        check = {"share": round(float(ok.mean()), 3),
                 "median_mm": round(float(np.median(e[ok]))) if ok.any() else None}
    # auf normierte Bildkoordinaten umrechnen: H_norm = H_px * diag(W, H, 1)
    h = h_px @ np.diag([_FRAME[0], _FRAME[1], 1.0])
    h /= h[2, 2]
    return {
        "H": h.tolist(),
        "fitted": time.time(),
        "pairs": int(len(src)),
        "inliers": int(inl.sum()),
        "median_mm": round(float(np.median(err[inl]))),
        "p90_mm": round(float(np.percentile(err[inl], 90))),
        "check": check,
        "t0": float(t.min()), "t1": float(t.max()),
        "flips": flips,
        "senders": list(FIT_SENDERS),
    }


def _err(h, src, dst):
    p = cv2.perspectiveTransform(src.reshape(-1, 1, 2), h).reshape(-1, 2)
    return np.hypot(p[:, 0] - dst[:, 0], p[:, 1] - dst[:, 1])


class Homography:
    def __init__(self, path, map_dir):
        self.path = Path(path)
        self.map_dir = Path(map_dir)
        self.data = None
        self.H = self.Hinv = None
        self._overlay = None
        self.load()

    def load(self):
        try:
            self._set(json.loads(self.path.read_text()))
        except (OSError, ValueError, KeyError):
            self._set(None)

    def _set(self, data):
        self.data = data
        self._overlay = None
        if data:
            self.H = np.float64(data["H"])
            self.Hinv = np.linalg.inv(self.H)
        else:
            self.H = self.Hinv = None

    def calibrate(self, pair_paths, flips):
        data = fit(pair_paths, flips)
        self.path.write_text(json.dumps(data, indent=1))
        self._set(data)
        return data

    def valid(self, flips):
        """Nur gueltig, solange das Bild nicht anders gespiegelt ist als beim
        Kalibrieren - sonst stuende jeder Punkt seitenverkehrt."""
        return self.H is not None and self.data.get("flips") == flips

    def to_world(self, u, v):
        x, y, w = (float(c) for c in self.H @ (u, v, 1.0))
        if w <= 0:
            return None
        return int(round(x / w)), int(round(y / w))

    def to_image(self, x, y):
        return self._img(x, y)

    # -- Einblendung ins Livebild ---------------------------------------------
    def overlay(self):
        """Rasenrand, Schussverbot-Zone und 1-m-Gitter als Linienzuege in
        normierten Bildkoordinaten (Zwischenspeicher bis zur naechsten
        Kalibrierung)."""
        if self.Hinv is None:
            return None
        if self._overlay is None:
            maps = {name: _read_poly(self.map_dir / (name + ".csv"))
                    for name in ("rasen", "noshot")}
            # Rasen offen: die Schlusslinie vorne ist nicht vermessen
            out = {name: self._project(poly if name == "rasen" else poly + poly[:1])
                   for name, poly in maps.items() if poly}
            pts = [p for poly in maps.values() for p in poly]
            if pts:
                x0 = math.floor(min(p[0] for p in pts) / GRID_MM) * GRID_MM
                x1 = math.ceil(max(p[0] for p in pts) / GRID_MM) * GRID_MM
                y0 = math.floor(min(p[1] for p in pts) / GRID_MM) * GRID_MM
                y1 = math.ceil(max(p[1] for p in pts) / GRID_MM) * GRID_MM
                grid, labels = [], []
                for x in range(x0, x1 + 1, GRID_MM):
                    grid += self._project([(x, y0), (x, y1)])
                for y in range(y0, y1 + 1, GRID_MM):
                    grid += self._project([(x0, y), (x1, y)])
                for x in range(x0, x1 + 1, GRID_MM):
                    for y in range(y0, y1 + 1, GRID_MM):
                        if (x // GRID_MM) % 2 == 0 and (y // GRID_MM) % 2 == 0:
                            uv = self._img(x, y)
                            if uv:
                                labels.append([uv[0], uv[1], "%d/%d" % (x // 1000, y // 1000)])
                out["grid"] = grid
                out["labels"] = labels
            self._overlay = out
        return self._overlay

    def _img(self, x, y):
        u, v, w = (float(c) for c in self.Hinv @ (x, y, 1.0))
        if w <= 1e-9:
            return None
        u, v = u / w, v / w
        if not (-0.2 <= u <= 1.2 and -0.2 <= v <= 1.2):
            return None
        return [round(u, 4), round(v, 4)]

    def _project(self, poly):
        """Linienzug fein zerlegen und projizieren; wo er hinter den Horizont
        oder weit aus dem Bild laeuft, wird er aufgetrennt."""
        lines, cur = [], []
        for (ax, ay), (bx, by) in zip(poly, poly[1:]):
            n = max(1, int(math.hypot(bx - ax, by - ay) / STEP_MM))
            for i in range(n + 1):
                uv = self._img(ax + (bx - ax) * i / n, ay + (by - ay) * i / n)
                if uv is None:
                    if len(cur) > 1:
                        lines.append(cur)
                    cur = []
                elif not cur or cur[-1] != uv:
                    cur.append(uv)
        if len(cur) > 1:
            lines.append(cur)
        return lines


def _read_poly(path):
    pts = []
    try:
        for line in Path(path).read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            x, y = line.split(",")[:2]
            pts.append((float(x), float(y)))
    except (OSError, ValueError):
        return []
    return pts
