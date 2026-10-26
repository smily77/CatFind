"""
calib_core.py - Rechenteil der PA2-Winkel-Kalibrierung (ohne Netz)

Wird von pa2_calib.py (live) benutzt und wertet als Gesamtauswertung eine
ganze Aufnahme (runs/<zeit>/raw.jsonl) nachtraeglich aus:

  python calib_core.py runs/20261026_224258

Koordinaten wie in pa2_calib.py: PA-System mit Ursprung Drehachse, y = 0-Grad-
Linie nach vorn, x = rechts, Servo-Winkel positiv = rechts.
"""

import json
import math
import os
import sys
import time

LP40B_OFFSET_MM = 15          # LP40B 1,5 cm vor der Drehachse (auf dem Strahl)
FG_MARGIN_MM = 250            # Vordergrund = so viel naeher als der Hintergrund
LP_BIN_DEG = 0.5
PERSON_HALF_MM = 200          # halbe Breite einer Person fuer die erwartete Winkelbreite

# Gesamtauswertung
STAND_MM = 60                 # LD06-Schwerpunkt bleibt so nah beisammen = Person steht
STAND_S = 3.0                 # mindestens so lange
ASSOC_DEG = 5.0               # LP40B-Lauf passt zur LD06-Vorhersage
ASSOC_MM = 400
RADAR_MIN = 5                 # so viele Radarmeldungen je Standphase mindestens
RADAR_GHOST_MM = 10           # Streuung darunter = festes Geisterziel


def pos_to_deg(pos):
    return (pos - 2048) * 360.0 / 4096.0


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


def median(v):
    s = sorted(v)
    n = len(s)
    if not n:
        return None
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def circ_mean_deg(v):
    sx = sum(math.cos(math.radians(a)) for a in v)
    sy = sum(math.sin(math.radians(a)) for a in v)
    return math.degrees(math.atan2(sy, sx))


def spread(pts):
    """mittlere Abweichung vom Schwerpunkt (mm)"""
    if len(pts) < 2:
        return 0.0
    mx = sum(p[0] for p in pts) / len(pts)
    my = sum(p[1] for p in pts) / len(pts)
    return math.sqrt(sum((p[0] - mx) ** 2 + (p[1] - my) ** 2 for p in pts) / len(pts))


# ── LP40B ────────────────────────────────────────────────────────────────────

def lp_bg_at(bg, deg):
    b = int(math.floor(deg / LP_BIN_DEG))
    vals = [bg[i] for i in (b - 1, b, b + 1) if i in bg]
    return min(vals) if vals else None


def lp_is_fg(bg, deg, mm, err):
    if err != 0 or mm < 150:
        return False
    b = lp_bg_at(bg, deg)
    if b is None:
        return False
    return mm < b - max(FG_MARGIN_MM, 0.08 * b)


def find_runs(bg, samples):
    """Vordergrund-Laeufe einer Schwenkbewegung: [(mitte_deg, mm, breite_deg, n)]"""
    fg = sorted((d, mm) for d, mm, e in samples if lp_is_fg(bg, d, mm, e))
    runs, cur = [], []
    for d, mm in fg:
        if cur and (d - cur[-1][0] > 2.5 or abs(mm - cur[-1][1]) > 400):
            runs.append(cur)
            cur = []
        cur.append((d, mm))
    if cur:
        runs.append(cur)
    out = []
    for r in runs:
        if len(r) < 2:
            continue
        out.append((0.5 * (r[0][0] + r[-1][0]), median([m for _, m in r]),
                    r[-1][0] - r[0][0], len(r)))
    return out


def person_runs(bg, samples):
    """nur Laeufe, die so breit wie eine Person sind und nicht am Schwenkende abbrechen
    (schmale Stuecke sind Kanten, an denen der Messfleck halb trifft)"""
    if not samples:
        return []
    lo = min(a for a, _, _ in samples)
    hi = max(a for a, _, _ in samples)
    out = []
    for a, mm, w, n in find_runs(bg, samples):
        exp = 2 * math.degrees(math.atan(PERSON_HALF_MM / mm))
        if w < max(3.0, 0.5 * exp):
            continue
        if a - w / 2 < lo + 1.0 or a + w / 2 > hi - 1.0:
            continue
        out.append((a, mm, w, n))
    return out


# ── LD06 ─────────────────────────────────────────────────────────────────────

CLUSTER_GAP_MM = 300          # Punkte einer Person (ein Bein)
CLUSTER_MERGE_MM = 500        # zweites Bein / Koerper dazunehmen


def cluster(fg):
    """groesste Punktgruppe (Person) aus Vordergrund-Punkten (x, y, deg)"""
    if not fg:
        return None
    fg = sorted(fg, key=lambda p: p[2])
    groups, cur = [], [fg[0]]
    for p in fg[1:]:
        if math.hypot(p[0] - cur[-1][0], p[1] - cur[-1][1]) < CLUSTER_GAP_MM:
            cur.append(p)
        else:
            groups.append(cur)
            cur = [p]
    groups.append(cur)
    if len(groups) > 1 and math.hypot(groups[0][0][0] - groups[-1][-1][0],
                                      groups[0][0][1] - groups[-1][-1][1]) < CLUSTER_GAP_MM:
        groups[0] = groups.pop() + groups[0]          # ueber 0/360 Grad hinweg
    groups.sort(key=len, reverse=True)
    main = groups[0]
    if len(main) < 3:
        return None
    cx = sum(p[0] for p in main) / len(main)
    cy = sum(p[1] for p in main) / len(main)
    for g in groups[1:]:
        gx = sum(p[0] for p in g) / len(g)
        gy = sum(p[1] for p in g) / len(g)
        if math.hypot(gx - cx, gy - cy) < CLUSTER_MERGE_MM:
            main = main + g
    return main


# ── Fit ──────────────────────────────────────────────────────────────────────

def procrustes(ref, meas):
    """Drehung + Verschiebung (ohne Massstab): ref ~ R*meas + t"""
    n = len(ref)
    rx, ry = sum(p[0] for p in ref) / n, sum(p[1] for p in ref) / n
    mx, my = sum(p[0] for p in meas) / n, sum(p[1] for p in meas) / n
    sxx = sxy = 0.0
    for (ax, ay), (bx, by) in zip(ref, meas):
        ax, ay, bx, by = ax - rx, ay - ry, bx - mx, by - my
        sxx += bx * ax + by * ay
        sxy += bx * ay - by * ax
    a = math.atan2(sxy, sxx)
    c, s = math.cos(a), math.sin(a)
    tx, ty = rx - (c * mx - s * my), ry - (s * mx + c * my)
    err = math.sqrt(sum((ax - (c * bx - s * by + tx)) ** 2 + (ay - (s * bx + c * by + ty)) ** 2
                        for (ax, ay), (bx, by) in zip(ref, meas)) / n)
    return {"rot_deg": round(math.degrees(a), 2), "tx_mm": round(tx), "ty_mm": round(ty),
            "rms_mm": round(err)}


def fit_sensor(points, key, off):
    """Drehung (+ Spiegelung) Sensor-lokal -> PA, Sensor auf der 0-Grad-Linie bei y = off.
    Punkte mit p[key]['out'] zaehlen nicht."""
    used = [p for p in points if key in p and not p[key].get("out")]
    pairs = [((p["px"], p["py"] - off), (p[key]["x"], p[key]["y"])) for p in used]
    if not pairs:
        return None
    best = None
    for s in (1, -1):
        diffs = [wrap180(math.degrees(math.atan2(ry, rx)) - math.degrees(math.atan2(my, s * mx)))
                 for (rx, ry), (mx, my) in pairs]
        rot = circ_mean_deg(diffs)
        rms = math.sqrt(sum(wrap180(d - rot) ** 2 for d in diffs) / len(diffs))
        if best is None or rms < best["rms_deg"]:
            best = {"mirror": s, "rot_deg": round(rot, 2), "rms_deg": round(rms, 2),
                    "res_deg": {p["n"]: round(wrap180(d - rot), 2) for p, d in zip(used, diffs)}}
    s = best["mirror"]
    best["n"] = len(pairs)
    best["range_bias_mm"] = round(sum(math.hypot(mx, my) - math.hypot(rx, ry)
                                      for (rx, ry), (mx, my) in pairs) / len(pairs))
    if len(pairs) >= 3:
        best["free"] = procrustes([(rx, ry + off) for (rx, ry), _ in pairs],
                                  [(s * mx, my) for _, (mx, my) in pairs])
    return best


def fit_points(points, ld06_mm, radar_mm):
    res = {}
    for key, off in (("ld06", ld06_mm), ("radar", radar_mm)):
        r = fit_sensor(points, key, off)
        if r:
            res[key] = r
    return res


def transform(fit, off, x, y):
    """Sensor-lokal -> PA-Koordinaten (ohne Fit: ungedreht)"""
    s, rot = (fit["mirror"], fit["rot_deg"]) if fit else (1, 0.0)
    a = math.radians(rot)
    x = s * x
    return (x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a) + off)


def drop_outliers(points, key, off, limit_deg=3.0):
    """einmal: Punkte mit Rest > max(limit, 3*Median) als Ausreisser markieren, neu fitten"""
    f = fit_sensor(points, key, off)
    if not f or f["n"] < 4:
        return f
    res = f["res_deg"]
    lim = max(limit_deg, 3 * median([abs(v) for v in res.values()]))
    for p in points:
        if key in p and abs(res.get(p["n"], 0)) > lim:
            p[key]["out"] = True
    return fit_sensor(points, key, off)


# ── Gesamtauswertung einer Aufnahme ──────────────────────────────────────────

def load_run(path):
    lines = []
    with open(path, encoding="utf-8") as f:
        for l in f:
            try:
                lines.append(json.loads(l))
            except ValueError:
                pass                                   # letzte Zeile evtl. halb geschrieben
    return lines


def build_passes(lines):
    """LP40B-Schwenks: (t_anfang, t_ende, richtung, [(deg, mm, err)], hintergrund)"""
    passes, cur = [], []
    t = None
    last = None
    dr = 0
    t0 = None
    bg = {}
    for d in lines:
        if "t" in d:
            t = d["t"]
        if d["k"] == "bg":
            bg = {int(k): v for k, v in d["lp"].items()}
        elif d["k"] == "lp" and t is not None:
            p = d["p"]
            if last is not None:
                dd = p - last
                nd = 1 if dd > 2 else (-1 if dd < -2 else dr)
                if nd != dr and cur:
                    passes.append((t0, t, dr, cur, bg))
                    cur = []
                dr = nd
            last = p
            if not cur:
                t0 = t
            cur.append((pos_to_deg(p), d["mm"], d["e"]))
    return passes


def stand_segments(ld):
    segs = []
    i = 0
    while i < len(ld):
        j = i
        while (j + 1 < len(ld) and ld[j + 1]["t"] - ld[j]["t"] < 1.0 and
               math.hypot(ld[j + 1]["x"] - ld[i]["x"], ld[j + 1]["y"] - ld[i]["y"]) < STAND_MM):
            j += 1
        if ld[j]["t"] - ld[i]["t"] >= STAND_S:
            segs.append((ld[i]["t"], ld[j]["t"]))
            i = j + 1
        else:
            i += 1
    return segs


def lp_candidates(passes, t0, t1):
    """Personen-Kandidaten des LP40B in einer Standphase (beide Schwenkrichtungen)"""
    runs = []
    for ps, pe, dr, smp, bg in passes:
        if ps >= t0 and pe <= t1 and dr and bg:
            runs += [(dr,) + r for r in person_runs(bg, smp)]
    cands, used = [], set()
    for i, r in sorted(enumerate(runs), key=lambda x: -x[1][4]):
        if i in used:
            continue
        grp = [j for j, q in enumerate(runs) if j not in used and abs(q[1] - r[1]) < 3.0 and
               abs(q[2] - r[2]) < 300]
        used.update(grp)
        f = [runs[j][1] for j in grp if runs[j][0] == 1]
        b = [runs[j][1] for j in grp if runs[j][0] == -1]
        if not f or not b or len(f) + len(b) < 3:
            continue
        mf, mb = sum(f) / len(f), sum(b) / len(b)
        cands.append({"deg": 0.5 * (mf + mb), "lag": 0.5 * (mf - mb),
                      "lp_mm": median([runs[j][2] for j in grp]), "passes": [len(f), len(b)]})
    return cands


def radar_candidates(rd, t0, t1):
    pts = [(d["x"], d["y"]) for d in rd if t0 <= d["t"] <= t1]
    out = []
    while len(pts) >= RADAR_MIN:
        mx, my = median([p[0] for p in pts]), median([p[1] for p in pts])
        near = [p for p in pts if math.hypot(p[0] - mx, p[1] - my) < 400]
        if len(near) < RADAR_MIN:
            break
        sd = spread(near)
        out.append({"x": round(sum(p[0] for p in near) / len(near)),
                    "y": round(sum(p[1] for p in near) / len(near)),
                    "std": round(sd), "n": len(near), "ghost": sd < RADAR_GHOST_MM})
        pts = [p for p in pts if p not in near]
    return out


def analyze(path, ld06_mm=135.0, radar_mm=185.0):
    lines = load_run(path)
    passes = build_passes(lines)
    ld = [d for d in lines if d["k"] == "ld"]
    rd = [d for d in lines if d["k"] == "rd"]
    segs = []
    for t0, t1 in stand_segments(ld):
        a, b = t0 + 0.3, t1 - 0.3
        w = [(d["x"], d["y"]) for d in ld if a <= d["t"] <= b]
        seg = {"t0": t0, "t1": t1, "cands": lp_candidates(passes, a, b),
               "radar": [r for r in radar_candidates(rd, a, b) if not r["ghost"]],
               "ld06": {"x": round(sum(p[0] for p in w) / len(w)),
                        "y": round(sum(p[1] for p in w) / len(w)),
                        "std": round(spread(w)), "n": len(w)}}
        segs.append(seg)

    def make_point(seg, c, n):
        r = c["lp_mm"] + LP40B_OFFSET_MM
        return {"n": n, "t": round(seg["t0"], 1), "t0": time.strftime("%H:%M:%S", time.localtime(seg["t0"])),
                "dur": round(seg["t1"] - seg["t0"], 1), "deg": round(c["deg"], 2),
                "lag": round(c["lag"], 2), "lp_mm": round(c["lp_mm"]), "passes": c["passes"],
                "px": round(r * math.sin(math.radians(c["deg"]))),
                "py": round(r * math.cos(math.radians(c["deg"]))), "ld06": dict(seg["ld06"])}

    # 1. eindeutige Standphasen (genau ein LP40B-Kandidat) -> erster LD06-Fit
    pts = [make_point(s, s["cands"][0], i + 1) for i, s in enumerate(segs) if len(s["cands"]) == 1]
    ldfit = fit_sensor(pts, "ld06", ld06_mm) if len(pts) >= 2 else None
    # 2. alle Standphasen ueber die LD06-Vorhersage zuordnen, zweimal
    for _ in range(2):
        if not ldfit:
            break
        pts = []
        for i, s in enumerate(segs):
            x, y = transform(ldfit, ld06_mm, s["ld06"]["x"], s["ld06"]["y"])
            pdeg, pmm = math.degrees(math.atan2(x, y)), math.hypot(x, y) - LP40B_OFFSET_MM
            ok = [c for c in s["cands"] if abs(wrap180(c["deg"] - pdeg)) < ASSOC_DEG and
                  abs(c["lp_mm"] - pmm) < ASSOC_MM]
            if ok:
                c = min(ok, key=lambda c: abs(wrap180(c["deg"] - pdeg)))
                pts.append(make_point(s, c, i + 1))
        ldfit = drop_outliers(pts, "ld06", ld06_mm)
    # 3. Compact Dome: Radarziel passend zur Entfernung vom Dome, dann ueber den Winkel
    for p in pts:
        s = segs[p["n"] - 1]
        want = math.hypot(p["px"], p["py"] - radar_mm)
        ok = [r for r in s["radar"] if abs(math.hypot(r["x"], r["y"]) - want) < ASSOC_MM]
        if len(ok) == 1:
            p["radar"] = dict(ok[0])
    rdfit = fit_sensor(pts, "radar", radar_mm)
    if rdfit:
        for p in pts:
            s = segs[p["n"] - 1]
            want = math.hypot(p["px"], p["py"] - radar_mm)
            pdeg = math.degrees(math.atan2(p["px"], p["py"] - radar_mm))
            best = None
            for r in s["radar"]:
                x, y = transform(rdfit, radar_mm, r["x"], r["y"])
                da = abs(wrap180(math.degrees(math.atan2(x, y - radar_mm)) - pdeg))
                if da < 3 * ASSOC_DEG and abs(math.hypot(r["x"], r["y"]) - want) < ASSOC_MM:
                    if best is None or da < best[0]:
                        best = (da, r)
            if best:
                p["radar"] = dict(best[1])
            else:
                p.pop("radar", None)
        rdfit = drop_outliers(pts, "radar", radar_mm)
    res = {}
    if ldfit:
        res["ld06"] = ldfit
    if rdfit:
        res["radar"] = rdfit
    return {"segments": len(segs), "ambiguous": sum(1 for s in segs if len(s["cands"]) > 1),
            "no_lp": sum(1 for s in segs if not s["cands"]), "points": pts, "result": res}


def report(a):
    out = ["%d Standphasen, %d davon ohne LP40B-Treffer, %d mehrdeutig, %d Punkte" %
           (a["segments"], a["no_lp"], a["ambiguous"], len(a["points"]))]
    for p in a["points"]:
        q = "%2d %s %4.1fs  Servo %7.2f  %5d mm  Verzug %5.2f  Schwenks %s" % (
            p["n"], p["t0"], p["dur"], p["deg"], p["lp_mm"], p["lag"], p["passes"])
        for k in ("ld06", "radar"):
            if k in p:
                r = a["result"].get(k, {}).get("res_deg", {}).get(p["n"])
                q += "  %s %s" % (k, "Ausreisser" if p[k].get("out") else
                                  ("%+.2f" % r if r is not None else "-"))
        out.append(q)
    for k, name in (("ld06", "LD06"), ("radar", "Compact Dome")):
        f = a["result"].get(k)
        if f:
            out.append("%s: Drehung %.2f Grad%s, Streuung %.2f Grad, n=%d, Distanz %+d mm%s" % (
                name, f["rot_deg"], " gespiegelt" if f["mirror"] < 0 else "", f["rms_deg"], f["n"],
                f["range_bias_mm"],
                (", frei: %.2f Grad, Ort x %d / y %d mm, Rest %d mm" % (
                    f["free"]["rot_deg"], f["free"]["tx_mm"], f["free"]["ty_mm"], f["free"]["rms_mm"])
                 if "free" in f else "")))
    return "\n".join(out)


if __name__ == "__main__":
    run = sys.argv[1]
    path = os.path.join(run, "raw.jsonl") if os.path.isdir(run) else run
    a = analyze(path)
    print(report(a))
    with open(os.path.join(os.path.dirname(path), "analysis.json"), "w", encoding="utf-8") as f:
        json.dump(a, f, indent=1)
