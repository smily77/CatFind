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
  POST /localize_carrier  Body: {"lp":[[x,y]..], "ld":[[x,y]..]} (mm, Traeger-System)
                   -> Traegerpose + accept/reason (PowerActor2, siehe dort)
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


# --- Traegerpose (/localize_carrier) ----------------------------------------
# Der PowerActor2 (mobiler Traeger) schickt einen Rundum-Schwenk seines Drehturm-
# Lidars (LP40B) und den Scan des mitgefuehrten LD06, beide schon im Traeger-System
# (Ursprung Drehachse, x rechts, y = 0-Grad-Linie nach vorn, mm). Die Pose kommt
# GLOBAL aus dem LP40B: er misst bis ~16 m und sieht den Rasenrand wie der Pan-Tilt-
# Kopf, mit dem die Rasenkarte vermessen wurde. Der LD06 allein ist gegen den reinen
# Rasenumriss mehrdeutig (er sieht v.a. Haus/Veranda, <= ~5 m) - er prueft dafuer,
# ob der Turm-Schwenk stimmt: beide sind fest im Traeger kalibriert und muessen im
# Nahbereich dieselben Konturen sehen (Servo verrutscht / Turm falsch genullt /
# jemand im Strahl -> Abweichung). Test 2026-10-30: richtig 0,79, 5 Grad verdreht
# 0,65, 10 Grad 0,40.
CAR_MAX_UNIQ     = float(os.environ.get("CAR_MAX_UNIQ", "0.80"))    # zweitbeste ANDERE Loesung / beste
CAR_MIN_INLIER   = float(os.environ.get("CAR_MIN_INLIER", "0.45"))  # LP40B-Punkte nahe der Karte
CAR_MIN_MUTUAL   = float(os.environ.get("CAR_MIN_MUTUAL", "0.60"))  # LD06-Punkte nahe LP40B-Punkten
CAR_DISTINCT_M   = 1.0     # so weit (oder CAR_DISTINCT_DEG) weg gilt eine Loesung als ANDERE
CAR_DISTINCT_DEG = 20.0
CAR_THIN_M       = 0.15    # LP40B-Punkte ausduennen (gleichmaessige Gewichtung)


def _thin(P, d):
    out = []
    for p in P:
        if not out or math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) >= d:
            out.append(p)
    return np.asarray(out, dtype=float).reshape(-1, 2)


def _mutual(lp, ld):
    """Anteil der LD06-Punkte (0,5..5 m) mit einem LP40B-Punkt naeher als 15 cm."""
    r = np.hypot(ld[:, 0], ld[:, 1])
    q = ld[(r > 0.5) & (r < 5.0)]
    if q.shape[0] < 20 or lp.shape[0] == 0:
        return None
    d = np.sqrt(((q[:, None, :] - lp[None, :, :]) ** 2).sum(-1)).min(1)
    return float((d < 0.15).mean())


def localize_carrier(mapx, mapy, lp_mm, ld_mm, mirror=1):
    """Traegerpose global aus dem LP40B-Schwenk, LD06 als Gegenprobe des Schwenks.
    Rueckgabe dict mit accept/reason + Pose (Konvention wie /localize)."""
    lp = _thin([(x / 1000.0, y / 1000.0) for x, y in lp_mm], CAR_THIN_M)
    ld = np.asarray([(x / 1000.0, y / 1000.0) for x, y in ld_mm], dtype=float).reshape(-1, 2)
    if lp.shape[0] < 30:
        return {"accept": False, "reason": "zu wenige LP40B-Punkte (%d)" % lp.shape[0]}
    lx, ly = lp[:, 0], mirror * lp[:, 1]
    res = RES_COARSE
    Rmax = float(np.hypot(lx, ly).max()) + RANGE_PAD
    mg = 1.0
    txmin, txmax = mapx.min() - mg, mapx.max() + mg
    tymin, tymax = mapy.min() - mg, mapy.max() + mg
    fxmin, fymin = txmin - Rmax, tymin - Rmax
    nx = int(math.ceil((txmax + Rmax - fxmin) / res)) + 1
    ny = int(math.ceil((tymax + Rmax - fymin) / res)) + 1
    GX, GY = np.meshgrid(fxmin + np.arange(nx) * res, fymin + np.arange(ny) * res)
    d2 = np.full((ny, nx), np.inf)
    for mx, my in zip(mapx, mapy):
        np.minimum(d2, (GX - mx) ** 2 + (GY - my) ** 2, out=d2)
    L = np.exp(-d2 / (2.0 * SIGMA * SIGMA))
    txs = np.arange(txmin, txmax + res, res)
    tys = np.arange(tymin, tymax + res, res)
    ix0 = np.round((txs - fxmin) / res).astype(int)
    iy0 = np.round((tys - fymin) / res).astype(int)

    cand = []                                 # je Heading die 3 staerksten getrennten Maxima
    for th in np.arange(0.0, 360.0, HEAD_COARSE_STEP):
        ct, st = math.cos(math.radians(th)), math.sin(math.radians(th))
        dx = np.round((ct * lx - st * ly) / res).astype(int)
        dy = np.round((st * lx + ct * ly) / res).astype(int)
        S = np.zeros((iy0.size, ix0.size))
        for k in range(dx.size):
            S += L[np.ix_(iy0 + dy[k], ix0 + dx[k])]
        taken = []
        for fi in np.argsort(S.ravel())[::-1][:30]:
            yy, xx = np.unravel_index(fi, S.shape)
            if any(abs(yy - a) < 6 and abs(xx - b) < 6 for a, b in taken):
                continue
            taken.append((yy, xx))
            cand.append((float(S[yy, xx]), txs[xx], tys[yy], th))
            if len(taken) >= 3:
                break
    cand.sort(reverse=True)
    top = []
    for c in cand:
        if any(math.hypot(c[1] - o[1], c[2] - o[2]) < CAR_DISTINCT_M and
               abs((c[3] - o[3] + 180) % 360 - 180) < CAR_DISTINCT_DEG for o in top):
            continue
        top.append(c)
        if len(top) >= 6:
            break

    def fscore(P, tx, ty, th):
        ct, st = math.cos(math.radians(th)), math.sin(math.radians(th))
        wx = tx + ct * P[:, 0] - st * mirror * P[:, 1]
        wy = ty + st * P[:, 0] + ct * mirror * P[:, 1]
        dd = ((wx[:, None] - mapx[None]) ** 2 + (wy[:, None] - mapy[None]) ** 2).min(1)
        return float(np.exp(-dd / (2.0 * SIGMA * SIGMA)).sum()), float((dd < INLIER_M ** 2).mean())

    refined = []
    for _, tx, ty, th in top:
        best = (fscore(lp, tx, ty, th)[0], tx, ty, th)
        for span, step, hspan, hstep in ((0.30, 0.05, 6.0, 1.0), (0.06, 0.01, 1.0, 0.1)):
            _, bx, by, bh = best
            for h in np.arange(bh - hspan, bh + hspan + 1e-9, hstep):
                for x in np.arange(bx - span, bx + span + 1e-9, step):
                    for y in np.arange(by - span, by + span + 1e-9, step):
                        f = fscore(lp, x, y, h)[0]
                        if f > best[0]:
                            best = (f, x, y, h)
        refined.append(best)
    refined.sort(reverse=True)
    sc, bx, by, bh = refined[0]
    other = [r for r in refined[1:]
             if math.hypot(r[1] - bx, r[2] - by) >= CAR_DISTINCT_M or
             abs((r[3] - bh + 180) % 360 - 180) >= CAR_DISTINCT_DEG]
    uniq = other[0][0] / sc if other else 0.0
    inlier = fscore(lp, bx, by, bh)[1]
    mutual = _mutual(lp, ld)
    reasons = []
    if uniq > CAR_MAX_UNIQ:
        reasons.append("mehrdeutig (%.2f > %.2f)" % (uniq, CAR_MAX_UNIQ))
    if inlier < CAR_MIN_INLIER:
        reasons.append("LP40B passt schlecht (%.2f < %.2f)" % (inlier, CAR_MIN_INLIER))
    if mutual is None:
        reasons.append("LD06-Scan fehlt")
    elif mutual < CAR_MIN_MUTUAL:
        reasons.append("LD06 und LP40B widersprechen sich (%.2f < %.2f)" % (mutual, CAR_MIN_MUTUAL))
    out = {"accept": not reasons, "reason": "; ".join(reasons) or "ok",
           "x_mm": int(round(bx * 1000)), "y_mm": int(round(by * 1000)),
           "heading_deg": round(bh % 360.0, 2), "mirror": int(mirror),
           "uniq": round(uniq, 3), "inlier_ratio": round(inlier, 3),
           "mutual": None if mutual is None else round(mutual, 3),
           "lp_points": int(lp.shape[0]), "ld_points": int(ld.shape[0])}
    if other:
        # eigene Schluesselnamen: jsonify sortiert alphabetisch ("second" vor "x_mm"),
        # der einfache Parser im Geraet nimmt das erste "x_mm" (2026-10-30 so passiert)
        out["second"] = {"x2_mm": int(round(other[0][1] * 1000)),
                         "y2_mm": int(round(other[0][2] * 1000)),
                         "heading2_deg": round(other[0][3] % 360.0, 1)}
    return out


# --- Co-Observation-Kalibrierung (/calibrate) -------------------------------
# Trajektorien-Registrierung: eine Eigen-Spur (Radar, relativ mm) gegen eine
# Welt-Spur (welt-posierter Sensor, mm) ausrichten. Korrespondenz kommt aus der
# BAHNFORM (ICP mit Nearest-Neighbour), nicht aus der Zeit. Gesucht ist die starre
# Transformation  welt = R(heading)*[x, mirror*y] + (tx,ty)  (konsistent mit
# localToWorld in xComProc6_3.h: Drehsinn = mirror an der lokalen y-Achse).
CAL_INLIER_MM   = float(os.environ.get("CAL_INLIER_MM", "300"))   # Welt-Residuum fuer Inlier
CAL_MIN_PTS     = int(os.environ.get("CAL_MIN_PTS", "12"))        # Mindestpunkte je Spur
CAL_RESAMPLE_N  = int(os.environ.get("CAL_RESAMPLE_N", "80"))     # Bogenlaengen-Resampling
CAL_ICP_ITERS   = int(os.environ.get("CAL_ICP_ITERS", "40"))
CAL_MIN_LINEAR  = float(os.environ.get("CAL_MIN_LINEAR", "0.01")) # PCA-Verhaeltnis: darunter = (fast) gerade Linie -> mehrdeutig


def _resample_arc(pts, n):
    """Polyline auf n bogenlaengen-aequidistante Punkte resampeln (gleichmaessige
    Punktdichte -> robustere ICP-Korrespondenzen, unabhaengig vom Gehtempo)."""
    pts = np.asarray(pts, dtype=float)
    if len(pts) <= 2:
        return pts
    seg = np.sqrt(((pts[1:] - pts[:-1]) ** 2).sum(1))
    s = np.concatenate([[0.0], np.cumsum(seg)])
    if s[-1] <= 0:
        return pts[:1]
    want = np.linspace(0.0, s[-1], min(n, len(pts)))
    x = np.interp(want, s, pts[:, 0])
    y = np.interp(want, s, pts[:, 1])
    return np.column_stack([x, y])


def _kabsch2d(P, Q):
    """Starre 2D-Transformation (echte Rotation, keine Skalierung) mit q ~ R*p + t."""
    Pc, Qc = P.mean(0), Q.mean(0)
    H = (P - Pc).T @ (Q - Qc)
    U, _, Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(Vt.T @ U.T))
    R = Vt.T @ np.diag([1.0, d]) @ U.T
    t = Qc - R @ Pc
    return R, t


def _dtw(A, B):
    """Reihenfolge-erhaltende Zuordnung zweier geordneter Punktzuege (Dynamic Time
    Warping ueber euklidische Distanz). Gibt korrespondierende Indexarrays (ai, bj)
    zurueck. Anders als Nearest-Neighbour erzwingt das eine MONOTONE Korrespondenz und
    bestraft so falschen Drehsinn / falsches Heading, die als Menge zwar ueberlappen
    koennen, aber die Bahn nicht in konsistenter Reihenfolge durchlaufen."""
    C = np.sqrt(((A[:, None, :] - B[None, :, :]) ** 2).sum(-1))
    n, m = C.shape
    D = np.full((n + 1, m + 1), np.inf); D[0, 0] = 0.0
    for i in range(1, n + 1):
        Ci = C[i - 1]; Dp = D[i - 1]; Di = D[i]
        for j in range(1, m + 1):
            Di[j] = Ci[j - 1] + min(Dp[j], Di[j - 1], Dp[j - 1])
    i, j, ai, bj = n, m, [], []
    while i > 0 and j > 0:
        ai.append(i - 1); bj.append(j - 1)
        up, left, diag = D[i - 1, j], D[i, j - 1], D[i - 1, j - 1]
        mmin = min(up, left, diag)
        if diag == mmin:   i -= 1; j -= 1
        elif up == mmin:   i -= 1
        else:              j -= 1
    return np.array(ai[::-1]), np.array(bj[::-1])


def _linearity(pts):
    """PCA-Eigenwertverhaeltnis (klein/gross). 0 = perfekt gerade (mehrdeutig)."""
    c = pts - pts.mean(0)
    w = np.linalg.eigvalsh((c.T @ c) / max(len(pts), 1))
    return float(w[0] / w[1]) if w[1] > 1e-9 else 0.0


def register_pair(radar_pts, world_pts):
    """Eine Eigen-Spur gegen eine Welt-Spur registrieren (beide mm). ICP mit
    reihenfolge-erhaltender DTW-Korrespondenz, ueber mirror = +-1 und mehrere
    Start-Headings. Auswahl nach Inlier-Anteil auf den DTW-Paaren. None bei zu wenig."""
    P0 = _resample_arc(radar_pts, CAL_RESAMPLE_N)
    Q  = _resample_arc(world_pts, CAL_RESAMPLE_N)
    if len(P0) < CAL_MIN_PTS or len(Q) < CAL_MIN_PTS:
        return None
    best = None
    for mirror in (1.0, -1.0):
        Pm = P0.copy(); Pm[:, 1] *= mirror
        for th0 in range(0, 360, 60):
            a = math.radians(th0)
            R = np.array([[math.cos(a), -math.sin(a)], [math.sin(a), math.cos(a)]])
            t = Q.mean(0) - R @ Pm.mean(0)
            ai = bj = None
            for _ in range(CAL_ICP_ITERS):
                T = Pm @ R.T + t
                ai, bj = _dtw(T, Q)
                R, t = _kabsch2d(Pm[ai], Q[bj])
            T = Pm @ R.T + t
            ai, bj = _dtw(T, Q)
            d = np.sqrt(((T[ai] - Q[bj]) ** 2).sum(1))
            inlier_ratio = float(np.mean(d < CAL_INLIER_MM))
            resid = float(np.mean(d))
            if best is None or inlier_ratio > best["inlier_ratio"] or \
               (inlier_ratio == best["inlier_ratio"] and resid < best["_resid"]):
                heading = math.degrees(math.atan2(R[1, 0], R[0, 0])) % 360.0
                best = {"tx_mm": int(round(t[0])), "ty_mm": int(round(t[1])),
                        "heading_deg": round(heading, 3), "mirror": int(mirror),
                        "inlier_ratio": round(inlier_ratio, 4), "_resid": resid}
    if best is not None:
        best.pop("_resid", None)
        # Linearitaet ueber beide Bahnen: eine (fast) gerade Linie ist mehrdeutig
        best["linearity"] = round(min(_linearity(P0), _linearity(Q)), 4)
    return best


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


@app.post("/localize_carrier")
def do_localize_carrier():
    load_map()
    if _mapx.size == 0:
        return jsonify(error="no map loaded"), 503
    data = request.get_json(force=True, silent=True) or {}
    lp, ld = data.get("lp"), data.get("ld") or []
    if not isinstance(lp, list) or not isinstance(ld, list):
        return jsonify(error="body needs {'lp':[[x,y],..], 'ld':[[x,y],..]} (mm, Traeger-System)"), 400
    try:
        lp = [(float(p[0]), float(p[1])) for p in lp]
        ld = [(float(p[0]), float(p[1])) for p in ld]
    except (TypeError, ValueError, IndexError):
        return jsonify(error="Punkte als [x,y] in mm"), 400
    with _map_lock:
        mapx, mapy = _mapx.copy(), _mapy.copy()
    t0 = time.time()
    out = localize_carrier(mapx, mapy, lp, ld, int(data.get("mirror", 1)) or 1)
    out["secs"] = round(time.time() - t0, 1)
    print("localize_carrier from %s: %s x=%s y=%s head=%s uniq=%s inlier=%s mutual=%s "
          "lp=%s ld=%s (%s s) %s"
          % (request.remote_addr, "OK" if out["accept"] else "ABGELEHNT", out.get("x_mm"),
             out.get("y_mm"), out.get("heading_deg"), out.get("uniq"), out.get("inlier_ratio"),
             out.get("mutual"), out.get("lp_points"), out.get("ld_points"), out["secs"],
             out["reason"]), flush=True)
    return jsonify(out)


@app.post("/calibrate")
def do_calibrate():
    data = request.get_json(force=True, silent=True) or {}
    radar_tracks = data.get("radar_tracks") or []
    world_tracks = data.get("world_tracks") or []
    if not radar_tracks or not world_tracks:
        return jsonify(error="body needs {'radar_tracks':[[[x,y],..]], "
                             "'world_tracks':[{'id':N,'pts':[[x,y],..]}]}"), 400

    best, best_src = None, 0
    for wt in world_tracks:
        wpts = wt.get("pts") or []
        if len(wpts) < CAL_MIN_PTS:
            continue
        for rpts in radar_tracks:            # RANSAC-artig: passende Eigen-Spur waehlen
            if len(rpts) < CAL_MIN_PTS:
                continue
            res = register_pair(rpts, wpts)
            if res and (best is None or res["inlier_ratio"] > best["inlier_ratio"]):
                best, best_src = res, int(wt.get("id", 0))
    if best is None:
        return jsonify(ok=False, error="no registration (too few points)"), 422

    ir, lin = best["inlier_ratio"], best["linearity"]
    if   ir >= 0.60 and lin >= CAL_MIN_LINEAR: conf = "HOCH"
    elif ir >= 0.40:                           conf = "MITTEL"
    else:                                      conf = "NIEDRIG"
    out = dict(ok=True, source=best_src, confidence=conf, **best)
    print("calibrate from %s: src=%d conf=%s inlier=%.2f lin=%.3f x=%d y=%d head=%.1f mir=%+d"
          % (request.remote_addr, best_src, conf, ir, lin,
             best["tx_mm"], best["ty_mm"], best["heading_deg"], best["mirror"]), flush=True)
    return jsonify(out)


load_map(force=True)

if __name__ == "__main__":
    from waitress import serve
    port = int(os.environ.get("PORT", "8080"))
    print("CatFinder localizer on :%d" % port, flush=True)
    serve(app, host="0.0.0.0", port=port, threads=2)
