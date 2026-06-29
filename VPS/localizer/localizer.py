#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CatFinder VPS-Lokalisierungsdienst.

HTTP-Service, der einen 360-Bin Lidar-Scan (mm) gegen die Rasenkarte
(`Map/RasenKarte.csv`, aus dem GitHub-Repo geladen) global mit dem
korrelativen Scan-Matcher aus `lidar_localize.py` zur Deckung bringt und die
wahrscheinlichste Pose (X, Y, Heading, Drehsinn) + Konfidenz zurueckgibt.

Endpunkte:
  POST /localize   Body: {"scan":[360 ints mm]}  -> Pose-JSON
  GET  /health     -> {"ok":true, "map_points":N, "map_src":...}
  POST /reload     -> Karte neu vom Repo laden

Die Karte wird beim Start (und periodisch / per /reload) aus dem Repo geladen,
damit Kartenaenderungen KEINEN Eingriff am VPS erfordern. Faellt der Abruf aus,
wird die zuletzt geladene bzw. die ins Image gebackene Fallback-Karte benutzt.
"""
import math
import os
import threading
import time
import urllib.request

import numpy as np
from flask import Flask, request, jsonify

# --- Karten-Quelle ----------------------------------------------------------
MAP_URL      = os.environ.get(
    "MAP_URL",
    "https://raw.githubusercontent.com/smily77/CatFind/main/Map/RasenKarte.csv")
FALLBACK_MAP = os.environ.get("FALLBACK_MAP", "/app/RasenKarte.csv")
MAP_REFRESH_S = int(os.environ.get("MAP_REFRESH_S", "600"))   # max. Kartenalter

# --- Matcher-Parameter (identisch zu lidar_localize.py) ---------------------
RES_COARSE = 0.10
RES_FINE   = 0.02
SIGMA      = 0.30
HEAD_COARSE_STEP = 4.0
NEAR_MIN_M = 0.12
RANGE_PAD  = 0.5
INLIER_M   = 2.0 * SIGMA

_map_lock = threading.Lock()
_mapx = np.empty(0)
_mapy = np.empty(0)
_map_ts = 0.0
_map_src = "none"


def parse_map(text):
    xs, ys = [], []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.replace(";", ",").split(",")
        try:
            xs.append(float(parts[0])); ys.append(float(parts[1]))
        except (ValueError, IndexError):
            continue
    return np.asarray(xs), np.asarray(ys)


def load_map(force=False):
    """Karte aus dem Repo laden; bei Fehler Fallback-Image-Karte. Thread-sicher."""
    global _mapx, _mapy, _map_ts, _map_src
    with _map_lock:
        if not force and _mapx.size and (time.time() - _map_ts) < MAP_REFRESH_S:
            return
        text = None; src = None
        try:
            with urllib.request.urlopen(MAP_URL, timeout=10) as r:
                text = r.read().decode("utf-8", "replace"); src = MAP_URL
        except Exception as e:                                  # noqa: BLE001
            print("map fetch failed (%s): %s" % (MAP_URL, e), flush=True)
        if text is None and os.path.exists(FALLBACK_MAP):
            text = open(FALLBACK_MAP, "r", encoding="utf-8", errors="replace").read()
            src = FALLBACK_MAP + " (fallback)"
        if text is None:
            print("no map available!", flush=True); return
        xs, ys = parse_map(text)
        if xs.size:
            _mapx, _mapy, _map_ts, _map_src = xs, ys, time.time(), src
            print("map loaded: %d points from %s" % (xs.size, src), flush=True)


def localize(mapx, mapy, ranges_mm):
    """Globale Pose-Schaetzung (Position + Heading + Drehsinn). Gibt dict zurueck
    oder None bei zu wenigen Strahlen. Algorithmus = lidar_localize.py."""
    ranges = np.asarray(ranges_mm, dtype=float) / 1000.0
    if ranges.size != 360:
        raise ValueError("scan must have 360 bins, got %d" % ranges.size)

    bins = np.arange(360)
    valid = (ranges > NEAR_MIN_M) & (ranges < 30.0)
    alpha = np.radians(bins[valid].astype(float))
    rv = ranges[valid]
    if rv.size < 8:
        return None
    lx = rv * np.cos(alpha)
    ly_base = rv * np.sin(alpha)
    Rmax = float(rv.max()) + RANGE_PAD

    margin = 1.0
    txmin, txmax = mapx.min() - margin, mapx.max() + margin
    tymin, tymax = mapy.min() - margin, mapy.max() + margin
    fxmin, fymin = txmin - Rmax, tymin - Rmax
    fxmax, fymax = txmax + Rmax, tymax + Rmax
    res = RES_COARSE
    nx = int(math.ceil((fxmax - fxmin) / res)) + 1
    ny = int(math.ceil((fymax - fymin) / res)) + 1

    gx = fxmin + np.arange(nx) * res
    gy = fymin + np.arange(ny) * res
    GX, GY = np.meshgrid(gx, gy)
    d2 = np.full((ny, nx), np.inf)
    for mx, my in zip(mapx, mapy):
        np.minimum(d2, (GX - mx) ** 2 + (GY - my) ** 2, out=d2)
    L = np.exp(-d2 / (2.0 * SIGMA * SIGMA))

    txs = np.arange(txmin, txmax + res, res)
    tys = np.arange(tymin, tymax + res, res)
    ix0 = np.round((txs - fxmin) / res).astype(int)
    iy0 = np.round((tys - fymin) / res).astype(int)
    headings = np.arange(0.0, 360.0, HEAD_COARSE_STEP)

    best = {"score": -1.0}
    for mirror in (+1.0, -1.0):
        ly = mirror * ly_base
        for th in headings:
            ct, st = math.cos(math.radians(th)), math.sin(math.radians(th))
            wx = ct * lx - st * ly
            wy = st * lx + ct * ly
            dxk = np.round(wx / res).astype(int)
            dyk = np.round(wy / res).astype(int)
            score = np.zeros((iy0.size, ix0.size))
            for k in range(dxk.size):
                score += L[np.ix_(iy0 + dyk[k], ix0 + dxk[k])]
            bi = int(np.argmax(score))
            sc = float(score.flat[bi])
            if sc > best["score"]:
                by, bx = np.unravel_index(bi, score.shape)
                best = {"score": sc, "tx": float(txs[bx]), "ty": float(tys[by]),
                        "theta": float(th), "mirror": mirror}

    def score_pose(tx, ty, th, mirror):
        ct, st = math.cos(math.radians(th)), math.sin(math.radians(th))
        ly = mirror * ly_base
        wx = tx + ct * lx - st * ly
        wy = ty + st * lx + ct * ly
        ix = np.round((wx - fxmin) / res).astype(int)
        iy = np.round((wy - fymin) / res).astype(int)
        ok = (ix >= 0) & (ix < nx) & (iy >= 0) & (iy < ny)
        return float(L[iy[ok], ix[ok]].sum())

    mir = best["mirror"]
    bx, by, bth, bsc = best["tx"], best["ty"], best["theta"], best["score"]
    for span_t, step_t, span_h, step_h in ((0.30, RES_FINE, 6.0, 0.5),
                                           (0.06, RES_FINE, 1.0, 0.1)):
        for th in np.arange(bth - span_h, bth + span_h + 1e-9, step_h):
            for tx in np.arange(bx - span_t, bx + span_t + 1e-9, step_t):
                for ty in np.arange(by - span_t, by + span_t + 1e-9, step_t):
                    sc = score_pose(tx, ty, th, mir)
                    if sc > bsc:
                        bsc, bx, by, bth = sc, tx, ty, th

    heading = bth % 360.0
    ct, st = math.cos(math.radians(bth)), math.sin(math.radians(bth))
    ly = mir * ly_base
    wx = bx + ct * lx - st * ly
    wy = by + st * lx + ct * ly
    ix = np.round((wx - fxmin) / res).astype(int)
    iy = np.round((wy - fymin) / res).astype(int)
    ok = (ix >= 0) & (ix < nx) & (iy >= 0) & (iy < ny)
    Lv = np.zeros(wx.size); Lv[ok] = L[iy[ok], ix[ok]]
    inlier_thresh = math.exp(-(INLIER_M ** 2) / (2.0 * SIGMA * SIGMA))
    n_inlier = int(np.count_nonzero(Lv >= inlier_thresh))
    n_total = int(rv.size)
    inlier_ratio = n_inlier / n_total
    quality = ("HOCH" if inlier_ratio >= 0.55 else
               "MITTEL" if inlier_ratio >= 0.35 else "NIEDRIG")

    return {
        "x_m": round(bx, 4), "y_m": round(by, 4), "heading_deg": round(heading, 3),
        "x_mm": int(round(bx * 1000.0)), "y_mm": int(round(by * 1000.0)),
        "mirror": int(mir), "score": round(bsc, 3),
        "inliers": n_inlier, "beams": n_total,
        "inlier_ratio": round(inlier_ratio, 4), "confidence": quality,
    }


app = Flask(__name__)


@app.get("/health")
def health():
    return jsonify(ok=True, map_points=int(_mapx.size), map_src=_map_src,
                   map_age_s=round(time.time() - _map_ts, 1) if _map_ts else None)


@app.post("/reload")
def reload_map():
    load_map(force=True)
    return jsonify(ok=True, map_points=int(_mapx.size), map_src=_map_src)


@app.post("/localize")
def do_localize():
    load_map()                                   # ggf. frische Karte
    if _mapx.size == 0:
        return jsonify(error="no map loaded"), 503
    data = request.get_json(force=True, silent=True) or {}
    scan = data.get("scan")
    if not isinstance(scan, list) or len(scan) != 360:
        return jsonify(error="body needs {'scan':[360 ints mm]}"), 400
    try:
        with _map_lock:
            mapx, mapy = _mapx.copy(), _mapy.copy()
        pose = localize(mapx, mapy, scan)
    except ValueError as e:
        return jsonify(error=str(e)), 400
    if pose is None:
        return jsonify(error="too few valid beams"), 422
    nz = sum(1 for v in scan if v)               # belegte Bins (Hintergrund-Qualitaet)
    print("localize from %s: x=%d y=%d head=%.1f mirror=%+d conf=%s inlier=%.2f beams=%d bins=%d"
          % (request.remote_addr, pose["x_mm"], pose["y_mm"], pose["heading_deg"],
             pose["mirror"], pose["confidence"], pose["inlier_ratio"], pose["beams"], nz),
          flush=True)
    return jsonify(pose)


load_map(force=True)

if __name__ == "__main__":
    from waitress import serve
    port = int(os.environ.get("PORT", "8080"))
    print("CatFinder localizer on :%d" % port, flush=True)
    serve(app, host="0.0.0.0", port=port, threads=2)
