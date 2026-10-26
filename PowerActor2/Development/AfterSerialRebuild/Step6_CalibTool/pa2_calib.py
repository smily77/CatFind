"""
pa2_calib.py - Winkel-Kalibrierung LD06 + Compact Dome gegen Servo/LP40B
(PowerActor2 Umbau, Schritt 6)

Gegenstuecke:
  PowerActor2  : ../Step6_Calib/Step6_Calib.ino   (UDP 4310/4311, Magic 0x56)
  LD06         : CF3_LD06_Lidar/LD06_6_3_0        (Rohdaten-Stream UDP 4320/4321)
  Compact Dome : Radar6_3_0 unveraendert          (catObserved per xCom-Multicast)

Prinzip: Eine Person stellt sich nacheinander an verschiedene Stellen vor den
PowerActor2. Der Servo schwenkt den LP40B ueber den Sektor, das Tool findet
die Person darin (Vordergrund gegenueber dem vorher aufgenommenen Hintergrund)
und schwenkt dann langsam hin und her ueber sie: die Mitte aus beiden
Richtungen ist die Referenzrichtung (Servo-/Messverzug hebt sich auf). Zur
gleichen Zeit misst der LD06 (Punktwolke) und der Compact Dome (Radarziel).
Aus allen Standpunkten wird je Sensor die Drehung (und Spiegelung) zwischen
Sensor- und PA-Koordinaten gefittet.

PA-Koordinaten: Ursprung Drehachse, y = 0-Grad-Linie (Servo 2048, nach vorn),
x = rechts. Servo-Winkel theta = (pos - 2048) * 360 / 4096, positiv = rechts
(Servo links = 0, rechts = 4095). Die Sensoren sitzen auf der 0-Grad-Linie vor
der Achse: LD06 bei y = 135 mm, Compact Dome bei y = 185 mm.

Waehrend des Laufs sendet das Tool alle 2 s eine Feuersperre (xCom fireInhibit,
Absender Simulator), weil der Compact Dome catObserved sendet und die
PowerActors auf jedes catObserved feuern.

Start:  python pa2_calib.py      ->  http://localhost:8077/  (auch vom Handy)
Nur Standardbibliothek.
"""

import json
import math
import os
import socket
import struct
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ── Netz ─────────────────────────────────────────────────────────────────────

PA2_IP = "192.168.0.181"
PA2_CMD_PORT, PA2_STATE_PORT, PA2_MAGIC = 4310, 4311, 0x56
LD06_REQ_PORT, LD06_DATA_PORT = 4320, 4321
MC_IP, MC_PORT = "239.0.0.57", 8266
HTTP_PORT = 8077

XCOM_VERSION = 0x63
CAT_OBSERVED, SETTINGS_REPORT, FIRE_INHIBIT = 2, 12, 17
ID_COMPACT_DOME, ID_LD06, ID_SIM = 3, 7, 14
INHIBIT_PERSON = 1

C_POLL, C_RELAYS, C_GOTO, C_SWEEP, C_STOP = 0, 1, 2, 3, 4
RELAY_LD06, RELAY_RADAR = 0x02, 0x04

# ── Geometrie / Erkennung ────────────────────────────────────────────────────

LP40B_OFFSET_MM = 15          # LP40B 1,5 cm vor der Drehachse (auf dem Strahl)
FG_MARGIN_MM = 250            # Vordergrund = so viel naeher als der Hintergrund
LP_BIN_DEG = 0.5
LD_BIN_DEG = 1.0
LD_MIN_MM, LD_MAX_MM = 400, 8000   # naeher: Turm und Traeger (drehen mit)
CLUSTER_GAP_MM = 300          # Punkte einer Person (ein Bein)
CLUSTER_MERGE_MM = 500        # zweites Bein / Koerper dazunehmen

HERE = os.path.dirname(os.path.abspath(__file__))


def pos_to_deg(pos):
    return (pos - 2048) * 360.0 / 4096.0


def deg_to_pos(deg):
    return int(round(2048 + deg * 4096.0 / 360.0))


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


def local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((PA2_IP, PA2_CMD_PORT))
        return s.getsockname()[0]
    finally:
        s.close()


# ── Gemeinsamer Zustand ──────────────────────────────────────────────────────

class Calib:
    def __init__(self):
        self.lock = threading.RLock()
        self.cfg = {
            "sector_deg": 60.0,       # Suche: +/- um 0 Grad
            "search_speed": 1000,     # Servo-Einheiten (Schritte/s)
            "focus_half_deg": 12.0,
            "focus_speed": 300,
            "focus_s": 5.0,
            "bg_s": 12.0,
            "ld06_mm": 135.0,
            "radar_mm": 185.0,
            "inhibit": True,
            "auto": False,
        }
        self.relays = RELAY_LD06 | RELAY_RADAR
        self.mode = "idle"            # idle / bg / search / focus
        self.mode_since = time.time()
        self.msg = "Bereit. Zuerst Hintergrund aufnehmen (niemand vor dem Turm)."
        # PowerActor2
        self.pa2 = {}
        self.pa2_at = 0.0
        self.lp = deque(maxlen=6000)  # (t_pc, deg, mm, err, dir)
        self.lp_dir = 0
        self.lp_last_pos = None
        self.passes = deque(maxlen=80)  # (t_ende, richtung, [(deg, mm, err)], t_anfang)
        self.cur_pass = []
        self.cur_pass_t0 = 0.0
        self.lp_bg = {}               # bin -> mm
        self.lp_bg_raw = {}
        self.lp_count = 0
        self.lp_rate = 0.0
        # LD06
        self.ld_ip = None
        self.ld_at = 0.0
        self.ld_scan = []             # laufende Umdrehung (deg, mm, q)
        self.ld_last_scan = []
        self.ld_last_deg = None
        self.ld_bg = {}
        self.ld_bg_raw = {}
        self.ld_fg = []               # Vordergrund-Punkte der letzten Umdrehung (x, y)
        self.ld_cent = deque(maxlen=400)  # (t, x, y, n)
        self.ld_pts = 0
        self.ld_rate = 0.0
        # Compact Dome
        self.radar_ip = None
        self.radar = deque(maxlen=2000)   # (t, x, y, idx)
        self.radar_rate = 0.0
        self.radar_n = 0
        # Ablauf
        self.cand = deque(maxlen=10)  # (t, deg, mm)
        self.focus_deg = None
        self.focus_t0 = 0.0
        self.points = []
        self.result = {}
        self.inhibit_sent = 0
        self.run_dir = os.path.join(HERE, "runs", time.strftime("%Y%m%d_%H%M%S"))
        os.makedirs(self.run_dir, exist_ok=True)
        self.log_f = open(os.path.join(self.run_dir, "raw.jsonl"), "a", encoding="utf-8")

    def log(self, kind, **kw):
        kw["k"] = kind
        kw["t"] = round(time.time(), 3)
        self.log_f.write(json.dumps(kw, separators=(",", ":")) + "\n")

    def set_mode(self, m, msg=None):
        self.mode = m
        self.mode_since = time.time()
        if msg:
            self.msg = msg
        self.log("mode", mode=m, msg=msg)

    # ── LP40B ────────────────────────────────────────────────────────────────

    def lp_bg_at(self, deg):
        b = int(math.floor(deg / LP_BIN_DEG))
        vals = [self.lp_bg[i] for i in (b - 1, b, b + 1) if i in self.lp_bg]
        return min(vals) if vals else None

    def lp_is_fg(self, deg, mm, err):
        if err != 0 or mm < 150:
            return False
        bg = self.lp_bg_at(deg)
        if bg is None:
            return False
        return mm < bg - max(FG_MARGIN_MM, 0.08 * bg)

    def add_lp(self, pos, mm, err):
        now = time.time()
        deg = pos_to_deg(pos)
        if self.lp_last_pos is not None:
            d = pos - self.lp_last_pos
            nd = 1 if d > 2 else (-1 if d < -2 else self.lp_dir)
            if nd != self.lp_dir and self.cur_pass:
                self.passes.append((now, self.lp_dir, self.cur_pass, self.cur_pass_t0))
                self.cur_pass = []
                self.cur_pass_t0 = now
            self.lp_dir = nd
        self.lp_last_pos = pos
        self.lp.append((now, deg, mm, err, self.lp_dir))
        self.cur_pass.append((deg, mm, err))
        self.lp_count += 1
        if self.mode == "bg" and err == 0 and mm > 150:
            self.lp_bg_raw.setdefault(int(math.floor(deg / LP_BIN_DEG)), []).append(mm)

    def runs_in(self, samples):
        """Vordergrund-Laeufe einer Schwenkbewegung: [(mitte_deg, mm, breite_deg, n)]"""
        fg = sorted((d, mm) for d, mm, e in samples if self.lp_is_fg(d, mm, e))
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

    # ── LD06 ─────────────────────────────────────────────────────────────────

    def ld_bg_at(self, deg):
        b = int(math.floor(deg / LD_BIN_DEG)) % 360
        vals = [self.ld_bg[i % 360] for i in (b - 1, b, b + 1) if i % 360 in self.ld_bg]
        return min(vals) if vals else None

    def add_ld_point(self, deg, mm, q):
        if self.ld_last_deg is not None and abs(deg - self.ld_last_deg) > 180:
            self.finish_ld_scan()
        self.ld_last_deg = deg
        self.ld_scan.append((deg, mm, q))
        self.ld_pts += 1

    def finish_ld_scan(self):
        scan, self.ld_scan = self.ld_scan, []
        if len(scan) < 50:
            return
        self.ld_last_scan = scan
        if self.mode == "bg":
            for d, mm, q in scan:
                if LD_MIN_MM <= mm <= LD_MAX_MM and q > 0:
                    self.ld_bg_raw.setdefault(int(math.floor(d / LD_BIN_DEG)) % 360, []).append(mm)
            return
        if not self.ld_bg:
            return
        fg = []
        for d, mm, q in scan:
            if not (LD_MIN_MM <= mm <= LD_MAX_MM) or q == 0:
                continue
            bg = self.ld_bg_at(d)
            if bg is not None and mm > bg - max(FG_MARGIN_MM, 0.08 * bg):
                continue
            a = math.radians(d)
            fg.append((mm * math.cos(a), mm * math.sin(a), d))
        self.ld_fg = [(x, y) for x, y, _ in fg]
        cl = cluster(fg)
        if cl:
            now = time.time()
            x = sum(p[0] for p in cl) / len(cl)
            y = sum(p[1] for p in cl) / len(cl)
            self.ld_cent.append((now, x, y, len(cl)))
            self.log("ld", x=round(x), y=round(y), n=len(cl))

    def ld_window(self, t0, t1):
        return [(x, y) for t, x, y, n in self.ld_cent if t0 <= t <= t1]

    # ── Ablauf ───────────────────────────────────────────────────────────────

    def start_bg(self):
        self.lp_bg_raw, self.ld_bg_raw = {}, {}
        self.set_mode("bg", "Hintergrund wird aufgenommen - bitte ausserhalb des Sektors bleiben ...")

    def finish_bg(self):
        self.lp_bg = {b: median(v) for b, v in self.lp_bg_raw.items() if len(v) >= 2}
        self.ld_bg = {b: median(v) for b, v in self.ld_bg_raw.items() if len(v) >= 3}
        self.log("bg", lp={str(k): v for k, v in self.lp_bg.items()},
                 ld={str(k): v for k, v in self.ld_bg.items()})
        self.set_mode("search", "Hintergrund: LP40B %d Felder, LD06 %d Grad. Jetzt an eine Stelle "
                      "vor den Turm stellen und ~6 s still stehen." % (len(self.lp_bg), len(self.ld_bg)))

    def tick(self):
        """10 Hz: Ablaufsteuerung"""
        now = time.time()
        cfg = self.cfg
        if self.mode == "bg" and now - self.mode_since > cfg["bg_s"]:
            self.finish_bg()
        elif self.mode == "search":
            self.search_step(now)
        elif self.mode == "focus" and now - self.focus_t0 > cfg["focus_s"]:
            self.finish_focus()

    def search_step(self, now):
        recent = [p for p in self.passes if now - p[0] < 4.0]
        if not recent:
            return
        last = recent[-1]
        runs = self.runs_in(last[2])
        if runs:
            prev = self.cand[-1] if self.cand else None
            if prev:
                best = min(runs, key=lambda r: abs(r[0] - prev[1]))
            else:
                best = max(runs, key=lambda r: r[3])
            if not self.cand or self.cand[-1][0] != last[0]:
                self.cand.append((last[0], best[0], best[1]))
        if not self.cfg["auto"] or len(self.cand) < 2:
            return
        c1, c2 = self.cand[-2], self.cand[-1]
        if now - c2[0] > 2.0 or now - c1[0] > 5.0:
            return
        if abs(c1[1] - c2[1]) > 3.0 or abs(c1[2] - c2[2]) > 250:
            return
        ld = self.ld_window(now - 1.5, now)
        if ld and spread(ld) > 80:
            return                                   # Person bewegt sich noch
        if self.points:
            lp = self.points[-1]
            if abs(c2[1] - lp["deg"]) < 6.0 and abs(c2[2] - lp["lp_mm"]) < 400:
                return                               # gleiche Stelle wie zuletzt
        self.start_focus(c2[1])

    def start_focus(self, deg):
        self.focus_deg = deg
        self.focus_t0 = time.time()
        self.set_mode("focus", "Standpunkt bei %.1f Grad wird vermessen - still stehen ..." % deg)

    def finish_focus(self):
        t0, t1 = self.focus_t0 + 0.6, time.time()
        fd = self.focus_deg
        cen = {1: [], -1: []}
        mms = []
        for t, d, samples, ts in self.passes:
            if ts < t0 or d == 0:
                continue
            runs = [r for r in self.runs_in(samples) if r[3] >= 3]
            if not runs:
                continue
            r = min(runs, key=lambda r: abs(r[0] - fd))
            if abs(r[0] - fd) > self.cfg["focus_half_deg"]:
                continue
            cen[d].append(r[0])
            mms.append(r[1])
        nf, nb = len(cen[1]), len(cen[-1])
        if nf + nb < 3 or not mms:
            self.set_mode("search", "Standpunkt verworfen: LP40B fand die Person nur %d mal." % (nf + nb))
            return
        if nf and nb:
            mf, mb = sum(cen[1]) / nf, sum(cen[-1]) / nb
            deg, lag = 0.5 * (mf + mb), 0.5 * (mf - mb)
        else:
            deg, lag = sum(cen[1] + cen[-1]) / (nf + nb), None
        lp_mm = median(mms)
        r = lp_mm + LP40B_OFFSET_MM
        px, py = r * math.sin(math.radians(deg)), r * math.cos(math.radians(deg))
        pt = {"n": len(self.points) + 1, "t": round(t1, 1), "deg": round(deg, 2),
              "lag": None if lag is None else round(lag, 2), "lp_mm": round(lp_mm),
              "px": round(px), "py": round(py), "passes": [nf, nb]}
        ld = self.ld_window(t0, t1)
        if ld:
            pt["ld06"] = {"x": round(sum(p[0] for p in ld) / len(ld)),
                          "y": round(sum(p[1] for p in ld) / len(ld)),
                          "std": round(spread(ld)), "n": len(ld)}
        rd = [(x, y) for t, x, y, i in self.radar if t0 <= t <= t1]
        if rd:
            mx, my = median([p[0] for p in rd]), median([p[1] for p in rd])
            near = [p for p in rd if math.hypot(p[0] - mx, p[1] - my) < 400]
            if near:
                pt["radar"] = {"x": round(sum(p[0] for p in near) / len(near)),
                               "y": round(sum(p[1] for p in near) / len(near)),
                               "std": round(spread(near)), "n": len(near), "all": len(rd)}
        if "ld06" in pt and pt["ld06"]["std"] > 120:
            self.set_mode("search", "Standpunkt verworfen: Person hat sich bewegt (LD06 %d mm)."
                          % pt["ld06"]["std"])
            return
        self.points.append(pt)
        self.log("point", **pt)
        self.fit()
        self.set_mode("search", "Punkt %d: %.1f Grad, %.2f m%s. Naechste Stelle (andere Richtung/"
                      "Entfernung) ..." % (pt["n"], deg, r / 1000,
                                           "" if lag is None else ", Verzug %.2f Grad" % lag))

    # ── Fit ──────────────────────────────────────────────────────────────────

    def fit(self):
        res = {}
        for key, off in (("ld06", self.cfg["ld06_mm"]), ("radar", self.cfg["radar_mm"])):
            pairs = [((p["px"], p["py"] - off), (p[key]["x"], p[key]["y"]))
                     for p in self.points if key in p]
            if not pairs:
                continue
            best = None
            for s in (1, -1):
                diffs = [wrap180(math.degrees(math.atan2(ry, rx)) -
                                 math.degrees(math.atan2(my, s * mx)))
                         for (rx, ry), (mx, my) in pairs]
                rot = circ_mean_deg(diffs)
                rms = math.sqrt(sum(wrap180(d - rot) ** 2 for d in diffs) / len(diffs))
                if best is None or rms < best["rms_deg"]:
                    best = {"mirror": s, "rot_deg": round(rot, 2), "rms_deg": round(rms, 2),
                            "res_deg": [round(wrap180(d - rot), 2) for d in diffs]}
            s = best["mirror"]
            best["n"] = len(pairs)
            best["range_bias_mm"] = round(sum(math.hypot(mx, my) - math.hypot(rx, ry)
                                              for (rx, ry), (mx, my) in pairs) / len(pairs))
            if len(pairs) >= 3:
                best["free"] = procrustes([(rx, ry + off) for (rx, ry), _ in pairs],
                                          [(s * mx, my) for _, (mx, my) in pairs])
            res[key] = best
        self.result = res
        self.log("fit", **res)
        with open(os.path.join(self.run_dir, "result.json"), "w", encoding="utf-8") as f:
            json.dump({"cfg": self.cfg, "points": self.points, "result": res}, f, indent=1)

    def transform(self, key, x, y):
        """Sensor-lokal -> PA-Koordinaten mit aktuellem Fit (sonst ungedreht)"""
        r = self.result.get(key)
        off = self.cfg["ld06_mm"] if key == "ld06" else self.cfg["radar_mm"]
        s, rot = (r["mirror"], r["rot_deg"]) if r else (1, 0.0)
        a = math.radians(rot)
        x = s * x
        return (x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a) + off)

    # ── Anzeige ──────────────────────────────────────────────────────────────

    def snapshot(self):
        now = time.time()
        lp = []
        for t, d, samples, ts in list(self.passes)[-2:]:
            for deg, mm, e in samples[::2]:
                if e == 0 and mm < 12000:
                    r = mm + LP40B_OFFSET_MM
                    lp.append((round(r * math.sin(math.radians(deg))),
                               round(r * math.cos(math.radians(deg))),
                               1 if self.lp_is_fg(deg, mm, e) else 0))
        ld = []
        for d, mm, q in self.ld_last_scan[::3]:
            if LD_MIN_MM <= mm <= LD_MAX_MM:
                a = math.radians(d)
                x, y = self.transform("ld06", mm * math.cos(a), mm * math.sin(a))
                ld.append((round(x), round(y)))
        ldc = None
        if self.ld_cent and now - self.ld_cent[-1][0] < 1.0:
            x, y = self.transform("ld06", self.ld_cent[-1][1], self.ld_cent[-1][2])
            ldc = (round(x), round(y))
        rd = []
        for t, x, y, i in self.radar:
            if now - t < 0.6:
                x, y = self.transform("radar", x, y)
                rd.append((round(x), round(y)))
        pts = []
        for p in self.points:
            q = {"n": p["n"], "px": p["px"], "py": p["py"]}
            for key in ("ld06", "radar"):
                if key in p:
                    q[key] = [round(v) for v in self.transform(key, p[key]["x"], p[key]["y"])]
            pts.append(q)
        pa = self.pa2
        return {
            "mode": self.mode, "msg": self.msg, "cfg": self.cfg, "relays": self.relays,
            "pa2": {"online": now - self.pa2_at < 1.5, "servoOk": pa.get("servoOk"),
                    "pos": pa.get("servoPos"), "deg": None if pa.get("servoPos") is None
                    else round(pos_to_deg(pa["servoPos"]), 1), "temp": pa.get("tempC"),
                    "relays": pa.get("relays"), "lidarMm": pa.get("lidarMm"),
                    "lidarErr": pa.get("lidarErr"), "rate": round(self.lp_rate)},
            "ld06": {"online": now - self.ld_at < 1.5, "ip": self.ld_ip, "rate": round(self.ld_rate),
                     "bg": len(self.ld_bg)},
            "radar": {"online": bool(self.radar) and now - self.radar[-1][0] < 2.0,
                      "ip": self.radar_ip, "rate": round(self.radar_rate, 1)},
            "lp_bg": len(self.lp_bg), "inhibits": self.inhibit_sent,
            "cand": list(self.cand)[-1][1:] if self.cand and now - self.cand[-1][0] < 3 else None,
            "focus": self.focus_deg if self.mode == "focus" else None,
            "lp": lp, "ld": ld, "ldc": ldc, "rd": rd, "points": pts,
            "table": self.points, "result": self.result, "run": os.path.relpath(self.run_dir, HERE),
        }


def spread(pts):
    """mittlere Abweichung vom Schwerpunkt (mm)"""
    if len(pts) < 2:
        return 0.0
    mx = sum(p[0] for p in pts) / len(pts)
    my = sum(p[1] for p in pts) / len(pts)
    return math.sqrt(sum((p[0] - mx) ** 2 + (p[1] - my) ** 2 for p in pts) / len(pts))


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


CAL = Calib()

# ── Netz-Threads ─────────────────────────────────────────────────────────────

ST_FMT = "<BBBBHHBBHHHBIII"
ST_KEYS = ("magic", "kind", "servoOk", "tempC", "servoGoal", "servoPos", "relays", "sweeping",
           "sweepLo", "sweepHi", "sweepSpeed", "lidarErr", "lidarMm", "lidarCount", "uptimeMs")
SAMPLE_FMT = "<IHHB"
SAMPLE_LEN = struct.calcsize(SAMPLE_FMT)

pa2_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
pa2_sock.bind(("", PA2_STATE_PORT))


def pa2_cmd(typ, a=0, b=0, c=0):
    pa2_sock.sendto(struct.pack("<BBHHH", PA2_MAGIC, typ, a, b, c), (PA2_IP, PA2_CMD_PORT))


def pa2_rx():
    while True:
        data, _ = pa2_sock.recvfrom(2048)
        if len(data) < 3 or data[0] != PA2_MAGIC:
            continue
        with CAL.lock:
            if data[1] == 1 and len(data) == struct.calcsize(ST_FMT):
                CAL.pa2 = dict(zip(ST_KEYS, struct.unpack(ST_FMT, data)))
                CAL.pa2_at = time.time()
            elif data[1] == 2:
                n = data[2]
                for i in range(n):
                    t, pos, mm, err = struct.unpack_from(SAMPLE_FMT, data, 3 + i * SAMPLE_LEN)
                    CAL.add_lp(pos, mm, err)
                    CAL.log_f.write('{"k":"lp","d":%d,"p":%d,"mm":%d,"e":%d}\n' % (t, pos, mm, err))


ld_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ld_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
ld_sock.bind(("", LD06_DATA_PORT))


def ld_rx():
    while True:
        data, addr = ld_sock.recvfrom(2048)
        if len(data) < 5 or data[:2] != b"L6":
            continue
        n = data[4]
        with CAL.lock:
            CAL.ld_ip, CAL.ld_at = addr[0], time.time()
            for i in range(n):
                a, d, q = struct.unpack_from("<HHB", data, 5 + i * 5)
                CAL.add_ld_point(a / 100.0, d, q)


def mc_socket(lip):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", MC_PORT))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                 socket.inet_aton(MC_IP) + socket.inet_aton(lip))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(lip))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    return s


def mc_rx(s):
    while True:
        data, addr = s.recvfrom(2048)
        if len(data) < 12 or data[0] != XCOM_VERSION:
            continue
        sender, code, plen = data[1], data[2], data[3]
        if len(data) != 12 + plen:
            continue
        with CAL.lock:
            if sender == ID_LD06 and not CAL.ld_ip:
                CAL.ld_ip = addr[0]
            if sender == ID_COMPACT_DOME:
                CAL.radar_ip = addr[0]
                if code == CAT_OBSERVED and plen == 34:
                    x, y, radius, angle, speed, res, wx, wy, wv, idx = \
                        struct.unpack_from("<iiffiiiiBB", data, 12)
                    CAL.radar.append((time.time(), x, y, idx))
                    CAL.radar_n += 1
                    CAL.log("rd", x=x, y=y, i=idx)


def send_inhibit(s):
    payload = struct.pack("<BIB", INHIBIT_PERSON, 10000, 100)
    hdr = struct.pack("<BBBBq", XCOM_VERSION, ID_SIM, FIRE_INHIBIT, len(payload), int(time.time()))
    s.sendto(hdr + payload, (MC_IP, MC_PORT))


def ticker(mc):
    """Keepalive, Servo-Steuerung, LD06-Anforderung, Feuersperre"""
    last_sweep = None
    t_ld = t_inh = t_rate = 0.0
    lp0 = ld0 = rd0 = 0
    while True:
        now = time.time()
        with CAL.lock:
            CAL.tick()
            cfg = CAL.cfg
            pa2_cmd(C_RELAYS, CAL.relays)
            if CAL.mode in ("bg", "search"):
                half = cfg["sector_deg"]
                want = (deg_to_pos(-half), deg_to_pos(half), int(cfg["search_speed"]))
            elif CAL.mode == "focus":
                h = cfg["focus_half_deg"]
                want = (deg_to_pos(CAL.focus_deg - h), deg_to_pos(CAL.focus_deg + h),
                        int(cfg["focus_speed"]))
            else:
                want = None
            if want != last_sweep:
                if want:
                    pa2_cmd(C_SWEEP, *want)
                else:
                    pa2_cmd(C_STOP)
                last_sweep = want
            if now - t_ld > 1.0:
                for ip in ([CAL.ld_ip] if CAL.ld_ip else []) + ["255.255.255.255"]:
                    ld_sock.sendto(b"LD06SCAN", (ip, LD06_REQ_PORT))
                t_ld = now
            if cfg["inhibit"] and now - t_inh > 2.0:
                send_inhibit(mc)
                CAL.inhibit_sent += 1
                t_inh = now
            if now - t_rate >= 2.0:
                dt = now - t_rate if t_rate else 2.0
                CAL.lp_rate = (CAL.lp_count - lp0) / dt
                CAL.ld_rate = (CAL.ld_pts - ld0) / dt
                CAL.radar_rate = (CAL.radar_n - rd0) / dt
                lp0, ld0, rd0, t_rate = CAL.lp_count, CAL.ld_pts, CAL.radar_n, now
                CAL.log_f.flush()
        time.sleep(0.1)


# ── Web ──────────────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype):
        b = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        if self.path.startswith("/state"):
            with CAL.lock:
                self.send(200, json.dumps(CAL.snapshot()), "application/json")
        elif self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), encoding="utf-8") as f:
                self.send(200, f.read(), "text/html; charset=utf-8")
        else:
            self.send(404, "nicht gefunden", "text/plain")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        req = json.loads(self.rfile.read(n) or b"{}")
        act = req.get("action")
        with CAL.lock:
            if act == "relay":
                CAL.relays ^= int(req["bit"])
            elif act == "bg":
                CAL.start_bg()
            elif act == "auto":
                CAL.cfg["auto"] = bool(req["on"])
                if CAL.cfg["auto"] and CAL.mode == "idle":
                    CAL.set_mode("search", "Suche laeuft.")
            elif act == "search":
                CAL.set_mode("search", "Suche laeuft.")
            elif act == "stop":
                CAL.cfg["auto"] = False
                CAL.set_mode("idle", "Angehalten.")
            elif act == "point":
                c = CAL.cand[-1] if CAL.cand else None
                if c and time.time() - c[0] < 3:
                    CAL.start_focus(c[1])
                else:
                    CAL.msg = "LP40B sieht gerade niemanden im Sektor."
            elif act == "undo":
                if CAL.points:
                    CAL.log("undo", n=CAL.points.pop()["n"])
                    CAL.fit()
            elif act == "del":
                CAL.points = [p for p in CAL.points if p["n"] != int(req["n"])]
                CAL.log("del", n=int(req["n"]))
                CAL.fit()
            elif act == "cfg":
                for k, v in req["cfg"].items():
                    if k in CAL.cfg and k != "auto":
                        CAL.cfg[k] = type(CAL.cfg[k])(v)
                CAL.log("cfg", **CAL.cfg)
                if CAL.points:
                    CAL.fit()
            self.send(200, json.dumps({"ok": True}), "application/json")


def main():
    lip = local_ip()
    mc = mc_socket(lip)
    for fn, args in ((pa2_rx, ()), (ld_rx, ()), (mc_rx, (mc,)), (ticker, (mc,))):
        threading.Thread(target=fn, args=args, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    print("PA2-Kalibrierung: http://localhost:%d/  (Handy: http://%s:%d/)" % (HTTP_PORT, lip, HTTP_PORT))
    print("Rohdaten:", CAL.run_dir)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        pa2_cmd(C_STOP)
        pa2_cmd(C_RELAYS, 0)
        CAL.log_f.close()
        print("Beendet - Relais aus, Sweep gestoppt.")


if __name__ == "__main__":
    main()
