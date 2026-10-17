"""Kamera-Modell (Phase 4, Klick-Kalibrierung): Lochkamera + Linsenverzeichnung.

Warum nicht nur die Homographie aus den Radar-Paaren (kivision_homography.py)?
Die lernt das Koordinatensystem der RADARE - und deren Posen wurden von Hand
auf die Lidar-Pose abgestimmt, die Rasenkarte dagegen mit dem Laser-Rangefinder
vermessen. Mit der gemessenen Kamera-Position passt nur Dome zu den Paaren,
MiniDome und Dome-C1 kaum (2026-10-17). Die Karte lag deshalb ~0,5-1 m daneben.

Hier ist bekannt bzw. gemessen:
  * Kamera-Standort und -Hoehe (vom User gemessen, 8,05/-0,6 m, 2,25 m hoch)
  * Brennweite aus dem Objektiv (2,1 mm, OV5647-Pixel 1,4 um -> 949 px bei 1640)
Gesucht sind nur die Blickrichtung (3 Winkel) und die Verzeichnung (k1, k2 bei
genug Punkten). Dafuer klickt man im Livebild ein paar Punkte mit bekannter
Weltposition an (Fusspunkte von Lidar/Radaren) und Punkte auf der Nulllinie
(Veranda-Kante, y = 0).

Bildpunkte normiert 0..1 (volles Sensorfeld; Livebild und KI-Bild zeigen
dasselbe Feld). Gerechnet wird im 1640x1232-Raster.
"""

import json
import math
import time
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None

W, H = 1640.0, 1232.0
PIXEL_MM = 0.0014 * 2592 / W        # OV5647: 1,4 um bei 2592 Breite
DEFAULT = {
    "cam": [8050.0, -600.0, 2250.0],  # mm, vom User gemessen 2026-10-17
    "lens_mm": 2.1,
    "refs": [                         # Weltpunkte (mm), uv = Klick im Bild (0..1)
        {"name": "LidarC1", "x": 3155, "y": 8942, "uv": None},
        {"name": "Dome", "x": 2981, "y": 7891, "uv": None},
        {"name": "Dome-C1", "x": 8210, "y": 2905, "uv": None},
    ],
    "zero": [],                       # Klicks auf der Nulllinie (y = 0), je [u, v]
}
STEP_MM = 150
K_PRIOR_MM = 200          # k1 = 0,5 "kostet" so viel wie 100 mm Klickfehler
# Bei genug Punkten wird der Kamera-Standort (x/y) mitgerechnet: der gemessene
# lag am 2026-10-17 ~1 m neben dem, den die Klicks verlangen (anderer
# Bezugspunkt als das Weltsystem der Laserkarte). 1 m Abweichung kostet so
# viel wie 20 mm Klickfehler - die Messung zieht nur leicht.
CAM_PRIOR = 0.02
GRID_MM = 1000


def _rot(yaw, pitch, roll):
    """Welt->Kamera. yaw: Blickrichtung in der Ebene (rad, von +x gegen den
    Uhrzeigersinn), pitch: nach unten geneigt, roll: um die Blickachse."""
    fwd = np.array([math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), -math.sin(pitch)])
    right = np.array([math.sin(yaw), -math.cos(yaw), 0.0])
    down = np.cross(fwd, right)
    cr, sr = math.cos(roll), math.sin(roll)
    right, down = cr * right + sr * down, -sr * right + cr * down
    return np.vstack([right, down, fwd])          # Zeilen = Kamera-Achsen in Welt


class CameraModel:
    def __init__(self, path):
        self.path = Path(path)
        self.data = json.loads(json.dumps(DEFAULT))
        self.fit = None          # Ergebnis der letzten Berechnung
        self._overlay = None
        try:
            stored = json.loads(self.path.read_text())
            self.data.update({k: v for k, v in stored.items() if k != "fit"})
            self.fit = stored.get("fit")
        except (OSError, ValueError):
            pass

    def save(self):
        out = dict(self.data, fit=self.fit)
        self.path.write_text(json.dumps(out, indent=1))
        self._overlay = None

    # -- Modell ---------------------------------------------------------------
    def _K(self, f):
        return np.float64([[f, 0, W / 2], [0, f, H / 2], [0, 0, 1]])

    def _parts(self, p=None, mirror=None):
        """R, C, K, dist im "physischen" System: Welt-y mal mirror (+1/-1),
        falls das Weltsystem gegenueber der Wirklichkeit gespiegelt ist."""
        p = self.fit["p"] if p is None else p
        self._m = float(self.fit["mirror"] if mirror is None else mirror)
        f = float(self.data["lens_mm"]) / PIXEL_MM
        R = _rot(p[0], p[1], p[2])
        dist = np.float64([p[3], p[4], 0, 0, 0])
        C = np.float64(self.data["cam"])
        if len(p) > 5:                    # Standort mitgerechnet (Hoehe bleibt)
            C = np.float64([p[5], p[6], C[2]])
        C = C * (1, self._m, 1)
        return R, C, self._K(f), dist

    def _ground(self, uv, p=None, mirror=None):
        """Normierte Bildpunkte -> Bodenpunkte (mm); NaN-Zeilen = ueber Horizont."""
        R, C, K, dist = self._parts(p, mirror)
        px = np.float64(uv).reshape(-1, 1, 2) * (W, H)
        und = cv2.undistortPoints(px, K, dist).reshape(-1, 2)
        rays = np.c_[und, np.ones(len(und))] @ R      # = R^T * ray (Welt)
        t = -C[2] / rays[:, 2]
        P = C[:2] + rays[:, :2] * t[:, None]
        P[t <= 0] = np.nan
        P[:, 1] *= self._m
        return P

    def valid(self, flips):
        return bool(self.fit) and self.fit.get("flips") == flips

    def to_world(self, u, v):
        P = self._ground([[u, v]])[0]
        if np.isnan(P[0]):
            return None
        return int(round(P[0])), int(round(P[1]))

    def _img(self, pts):
        """Weltpunkte (mm, Boden) -> normierte Bildpunkte; None hinter der
        Kamera oder ausserhalb des Bereichs, in dem die Verzeichnung gilt."""
        R, C, K, dist = self._parts()
        pts = np.float64(pts).reshape(-1, 2) * (1, self._m)
        cam = (np.c_[pts, np.zeros(len(pts))] - C) @ R.T
        out = [None] * len(pts)
        ok = cam[:, 2] > 100
        if not ok.any():
            return out
        rv, _ = cv2.Rodrigues(R)
        uv, _ = cv2.projectPoints(np.c_[pts[ok], np.zeros(ok.sum())], rv, -R @ C, K, dist)
        uv = uv.reshape(-1, 2)
        # Polynom-Verzeichnung klappt weit ausserhalb des Bildes um: nur bis
        # etwas ueber die Bildecke hinaus trauen
        rmax = 1.15 * math.hypot(W / 2, H / 2) / K[0, 0]
        n = cam[ok, :2] / cam[ok, 2:3]
        good = np.hypot(n[:, 0], n[:, 1]) < rmax
        for i, j in enumerate(np.flatnonzero(ok)):
            u, v = uv[i, 0] / W, uv[i, 1] / H
            if good[i] and -0.2 <= u <= 1.2 and -0.2 <= v <= 1.2:
                out[j] = [round(float(u), 4), round(float(v), 4)]
        return out

    # -- Berechnen ----------------------------------------------------------
    def residuals(self, p, mirror):
        refs = [r for r in self.data["refs"] if r.get("uv")]
        res = []
        if refs:
            P = self._ground([r["uv"] for r in refs], p, mirror)
            for r, q in zip(refs, P):
                res += [q[0] - r["x"], q[1] - r["y"]] if not np.isnan(q[0]) else [1e5, 1e5]
        if self.data["zero"]:
            P = self._ground(self.data["zero"], p, mirror)
            res += [q[1] if not np.isnan(q[1]) else 1e5 for q in P]
        # Leichte Bremse fuer die Verzeichnung (Weitwinkel: |k| deutlich < 1);
        # sonst biegt sie bei wenigen Punkten alles zurecht.
        res += [K_PRIOR_MM * p[3], K_PRIOR_MM * p[4]]
        # Standort darf wandern, aber nicht beliebig weit (Messung ist ein Anhalt)
        if len(p) > 5:
            c = self.data["cam"]
            res += [(p[5] - c[0]) * CAM_PRIOR, (p[6] - c[1]) * CAM_PRIOR]
        return np.float64(res)

    def calibrate(self, flips):
        if cv2 is None:
            raise ValueError("OpenCV fehlt")
        n_ref = sum(1 for r in self.data["refs"] if r.get("uv"))
        n_eq = 2 * n_ref + len(self.data["zero"])
        if n_ref < 2 or n_eq < 5:
            raise ValueError("zu wenig Punkte: mind. 2 Referenzen und zusammen 5 Gleichungen")
        # Erst der Standort (7+ Gleichungen), Verzeichnung erst bei deutlich mehr
        # Punkten: mit 8 Gleichungen bog k1 frei alles zurecht (k1 = +0,93,
        # Kissen statt Tonne) und verdrehte die Karte zwischen den Punkten.
        free = [0, 1, 2] + ([5, 6] if n_eq >= 7 else []) + ([3] if n_eq >= 12 else [])             + ([4] if n_eq >= 16 else [])
        C = np.float64(self.data["cam"])
        tgt = [r for r in self.data["refs"] if r.get("uv")]
        cx = np.mean([r["x"] for r in tgt]); cy = np.mean([r["y"] for r in tgt])
        best = None
        for mirror in (1, -1):
            yaw0 = math.atan2(mirror * (cy - C[1]), cx - C[0])
            for dyaw in np.radians([-40, -20, 0, 20, 40]):
                for pitch in np.radians([15, 25, 35, 50, 65]):
                    p = np.float64([yaw0 + dyaw, pitch, 0, 0, 0, C[0], C[1]])
                    p, cost = self._lm(p, free, mirror)
                    if best is None or cost < best[1]:
                        best = (p, cost, mirror)
        p, _, mirror = best
        r = self.residuals(p, mirror)
        per = []
        i = 0
        for ref in self.data["refs"]:
            if ref.get("uv"):
                per.append({"name": ref["name"], "err_mm": round(float(math.hypot(r[i], r[i + 1])))})
                i += 2
        r = r[:-4]                                   # ohne Bremsen (k, Standort)
        zero = [round(float(abs(v))) for v in r[i:]]
        self.fit = {"p": [float(v) for v in p], "mirror": mirror, "fitted": time.time(), "flips": flips,
                    "free": free, "refs": per, "zero_mm": zero,
                    "cam": [round(float(p[5])), round(float(p[6])), float(C[2])],
                    "rms_mm": round(float(np.sqrt(np.mean(r ** 2))))}
        self.save()
        return self.fit

    def _lm(self, p, free, mirror, iters=60):
        lam = 1e-2
        r = self.residuals(p, mirror)
        cost = float(r @ r)
        for _ in range(iters):
            J = np.empty((len(r), len(free)))
            for k, j in enumerate(free):
                dp = np.zeros_like(p)
                dp[j] = 1e-6
                J[:, k] = (self.residuals(p + dp, mirror) - r) / 1e-6
            A = J.T @ J
            g = J.T @ r
            while True:
                step = np.linalg.solve(A + lam * np.diag(np.diag(A) + 1e-9), -g)
                q = p.copy()
                q[free] += step
                rq = self.residuals(q, mirror)
                cq = float(rq @ rq)
                if cq < cost:
                    p, r, cost, lam = q, rq, cq, max(lam / 3, 1e-7)
                    break
                lam *= 4
                if lam > 1e8:
                    return p, cost
            if np.abs(step).max() < 1e-9:
                break
        return p, cost

    # -- Radar-Vergleich ----------------------------------------------------
    def radar_offsets(self, src_uv, dst, senders, near_mm=1500):
        """Wie weit liegt jedes Sensor-Weltsystem neben der Kamera? Je Sender
        die mittlere Abweichung (Radar - Kamera) der Paare, die naeher als
        near_mm zusammenliegen (Rest sind Fehlpaare)."""
        if not len(src_uv):
            return []
        P = self._ground(src_uv)
        out = []
        for s in sorted(set(senders.tolist())):
            m = (senders == s) & ~np.isnan(P[:, 0])
            if not m.any():
                continue
            d = dst[m] - P[m]
            e = np.hypot(d[:, 0], d[:, 1])
            near = e < near_mm
            if near.sum() < 20:
                out.append({"sender": int(s), "n": int(m.sum()), "near": int(near.sum())})
                continue
            out.append({"sender": int(s), "n": int(m.sum()), "near": int(near.sum()),
                        "dx": round(float(np.median(d[near, 0]))),
                        "dy": round(float(np.median(d[near, 1]))),
                        "median_mm": round(float(np.median(e[near])))})
        return out

    # -- Einblendung ----------------------------------------------------------
    def overlay(self, maps):
        if not self.fit:
            return None
        if self._overlay is None:
            out = {}
            for name, poly in maps.items():
                if poly:
                    out[name] = self._project(poly + poly[:1])
            pts = [q for poly in maps.values() for q in poly]
            if pts:
                x0 = math.floor(min(q[0] for q in pts) / GRID_MM) * GRID_MM
                x1 = math.ceil(max(q[0] for q in pts) / GRID_MM) * GRID_MM
                y0 = min(0, math.floor(min(q[1] for q in pts) / GRID_MM) * GRID_MM)
                y1 = math.ceil(max(q[1] for q in pts) / GRID_MM) * GRID_MM
                grid = []
                for x in range(x0, x1 + 1, GRID_MM):
                    grid += self._project([(x, y0), (x, y1)])
                for y in range(y0, y1 + 1, GRID_MM):
                    grid += self._project([(x0, y), (x1, y)])
                out["grid"] = grid
                out["zero"] = self._project([(x0, 0), (x1, 0)])
                lab = [(x, y) for x in range(x0, x1 + 1, 2 * GRID_MM)
                       for y in range(y0, y1 + 1, 2 * GRID_MM)]
                out["labels"] = [[uv[0], uv[1], "%d/%d" % (x // 1000, y // 1000)]
                                 for (x, y), uv in zip(lab, self._img(lab)) if uv]
            refs = [r for r in self.data["refs"]]
            out["refs"] = [[uv[0], uv[1], r["name"]] for r, uv in
                           zip(refs, self._img([(r["x"], r["y"]) for r in refs])) if uv]
            self._overlay = out
        return self._overlay

    def _project(self, poly):
        dense = []
        for (ax, ay), (bx, by) in zip(poly, poly[1:]):
            n = max(1, int(math.hypot(bx - ax, by - ay) / STEP_MM))
            dense += [(ax + (bx - ax) * i / n, ay + (by - ay) * i / n) for i in range(n)]
        dense.append(poly[-1])
        lines, cur = [], []
        for uv in self._img(dense):
            if uv is None:
                if len(cur) > 1:
                    lines.append(cur)
                cur = []
            elif not cur or cur[-1] != uv:
                cur.append(uv)
        if len(cur) > 1:
            lines.append(cur)
        return lines
