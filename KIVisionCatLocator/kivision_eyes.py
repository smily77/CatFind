"""Leuchtende Augen nachts (Phase 5C, 2026-10-18).

Warum: Nachts ist die Katze ausserhalb des IR-Lichts kaum zu sehen und auch
darin fuer die KI-Modelle zu klein. Ihre Augen werfen das IR-Licht aber fast
genau zur Kamera zurueck (Tapetum lucidum) und leuchten als winzige helle
Punkte - im 2592er-Bild nur ~3..8 px gross, im kleinen Graubild der
Bewegungserkennung gar nicht mehr da. Darum laeuft das hier auf dem grossen
Bild, nur nachts und nur auf dem Rasen.

Ablauf je Bild (ca. 2/s):
  * Gruenkanal, Top-Hat: kleine helle Punkte auf dunklem Grund bleiben, grosse
    helle Flaechen (Maeher-Scheinwerfer, ausgeleuchteter Rasen) fallen weg.
  * Kandidat ist ein Fleck mit 2..MAX_AREA px, der ein Vielfaches heller ist
    als seine Umgebung (MIN_RATIO). Im ausgeleuchteten Rasen glitzern Halme -
    die sind nur wenig heller als der helle Grund; dort zaehlen nur fast
    gesaettigte Punkte (BRIGHT_PEAK).
  * Ortsfeste Lichter (Lampen, Reflektoren, heisse Pixel) lernt eine langsam
    vergessende Waermekarte - wo dauernd etwas leuchtet, zaehlt nichts.
  * Kandidaten werden ueber die Bilder verfolgt; ein Auge gilt erst nach
    MIN_HITS Treffern. Ein Partnerpunkt daneben (zweites Auge) wird vermerkt.
"""

import math
import threading
import time

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None

EVERY = 0.5               # s zwischen zwei ausgewerteten Bildern
KERNEL = 15               # Top-Hat-Kern (px im 2592er-Bild): groesser = nur noch Flaechen
MIN_TOPHAT = 45           # so viel heller als die Umgebung muss ein Punkt sein
MIN_PEAK = 70             # und absolut so hell (Gruenkanal 0..255)
MIN_RATIO = 2.5           # Spitze / (Umgebung + 15)
BRIGHT_PEAK = 235         # fast gesaettigt: zaehlt auch auf hellem Grund
LAMP_PEAK = 200           # grosse helle Flaeche (Scheinwerfer des Maehers) ...
LAMP_AREA = 400           # ... ab so vielen px; Punkte bis LAMP_GROW px daneben
LAMP_GROW = 60            #     sind Teil der Lampe, nicht ein Auge
MAX_AREA = 120            # px; laengere Belichtung zieht ein bewegtes Auge zum Strich
MAX_SIDE = 24
MAX_SPOTS = 40            # mehr Kandidaten in einem Bild: Rauschen/Regen -> Bild verwerfen
HEAT_TAU = 300.0          # s, so schnell vergisst die Waermekarte
HEAT_STATIC = 6.0         # ab so viel "Dauerleuchten" an einer Stelle: ortsfestes Licht
HEAT_CELL = 16            # px je Zelle der Waermekarte
LINK_PX = 90              # Verfolgung: naechster Treffer hoechstens so weit weg
LOST_S = 2.0
MIN_HITS = 3
PAIR_PX = (3, 45)         # Abstand zweier Augen im Bild (nah .. fern)


class EyeTrack:
    __slots__ = ("id", "t0", "t_last", "uv", "px", "hits", "pair", "peak", "path",
                 "reported", "t_sent")

    def __init__(self, tid, spot, now):
        self.id = tid
        self.t0 = self.t_last = now
        self.px = spot["px"]
        self.uv = spot["uv"]
        self.hits = 1
        self.pair = spot["pair"]
        self.peak = spot["peak"]
        self.path = [spot["px"]]
        self.reported = False
        self.t_sent = 0.0


class Eyes:
    def __init__(self):
        self.lock = threading.Lock()
        self.tracks = []
        self.next_id = 1
        self.heat = None
        self.heat_t = None
        self.mask = None         # Rasen (+Rand) im 2592er-Bild, uint8
        self.box = None          # x0, y0, x1, y1 des Rasens
        self.night = False
        self.stats = {"frames": 0, "spots": 0, "static": 0, "noisy": 0, "eyes": 0,
                      "ms": 0.0}
        # Rechteckkern: OpenCV rechnet ihn zerlegt und viel schneller als eine
        # Ellipse (2592er-Bild: ~290 -> ~60 ms gemessen auf dem Pi 4)
        self.kernel = (cv2.getStructuringElement(cv2.MORPH_RECT, (KERNEL, KERNEL))
                       if cv2 is not None else None)

    # -- Rasen-Maske ------------------------------------------------------
    def set_mask(self, uv_poly, shape, grow_px=40):
        """uv_poly: Rasenumriss in normierten Bildpunkten; der Rand waechst um
        grow_px, damit Augen am Beetrand (Katze im Gebuesch) noch zaehlen."""
        h, w = shape[:2]
        if cv2 is None or len(uv_poly) < 3:
            self.mask, self.box = None, None
            return
        m = np.zeros((h, w), np.uint8)
        pts = np.int32([[round(u * w), round(v * h)] for u, v in uv_poly])
        cv2.fillPoly(m, [pts], 255)
        m = cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * grow_px + 1,) * 2))
        ys, xs = np.nonzero(m)
        if not len(xs):
            self.mask, self.box = None, None
            return
        self.box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
        x0, y0, x1, y1 = self.box
        self.mask = m[y0:y1, x0:x1]

    # -- ein Bild -------------------------------------------------------------
    def update(self, frame, now=None):
        """Neue Augen-Spuren, die gerade bestaetigt wurden, und alle laufenden."""
        if cv2 is None or self.mask is None:
            return []
        now = time.monotonic() if now is None else now
        t = time.perf_counter()
        x0, y0, x1, y1 = self.box
        g = np.ascontiguousarray(frame[y0:y1, x0:x1, 1])
        th = cv2.morphologyEx(g, cv2.MORPH_TOPHAT, self.kernel)
        cand = ((th >= MIN_TOPHAT) & (g >= MIN_PEAK) & (self.mask > 0)).astype(np.uint8)
        # Lampen: grosse, fast weisse Flaechen samt Umgebung ausblenden
        # (auf dem 4x kleineren Bild gesucht - spart ~100 ms)
        small = cv2.resize(g, (g.shape[1] // 4, g.shape[0] // 4), interpolation=cv2.INTER_AREA)
        big = (small >= LAMP_PEAK).astype(np.uint8)
        if big.any():
            nb, _lb, sb, _cb = cv2.connectedComponentsWithStats(big, connectivity=8)
            for j in range(1, nb):
                if sb[j][4] * 16 >= LAMP_AREA:
                    x, y, w, h = (int(v) * 4 for v in sb[j][:4])
                    cand[max(0, y - LAMP_GROW):y + h + LAMP_GROW,
                         max(0, x - LAMP_GROW):x + w + LAMP_GROW] = 0
        n, lab, st, cen = cv2.connectedComponentsWithStats(cand, connectivity=8)
        spots = []
        for i in range(1, n):
            x, y, w, h, area = st[i]
            if area < 2 or area > MAX_AREA or w > MAX_SIDE or h > MAX_SIDE:
                continue
            peak = int(g[y:y + h, x:x + w].max())
            bg = int(g[y:y + h, x:x + w].max()) - int(th[y:y + h, x:x + w].max())
            if peak < BRIGHT_PEAK and peak < MIN_RATIO * (bg + 15):
                continue
            spots.append({"px": (float(cen[i][0]) + x0, float(cen[i][1]) + y0),
                          "peak": peak, "area": int(area), "pair": False})
        self._decay(now, frame.shape)
        self.stats["frames"] += 1
        if len(spots) > MAX_SPOTS:
            self.stats["noisy"] += 1
            return []
        self.stats["spots"] += len(spots)
        # ortsfeste Lichter: Waermekarte erst lesen, dann fuettern
        live = []
        for s in spots:
            cx, cy = int(s["px"][0] // HEAT_CELL), int(s["px"][1] // HEAT_CELL)
            hv = self.heat[max(0, cy - 1):cy + 2, max(0, cx - 1):cx + 2].max()
            self.heat[cy, cx] += 1.0
            if hv >= HEAT_STATIC:
                self.stats["static"] += 1
                continue
            live.append(s)
        # Paare: zweiter Punkt etwa waagrecht daneben
        for a in live:
            for b in live:
                if a is b:
                    continue
                dx, dy = b["px"][0] - a["px"][0], b["px"][1] - a["px"][1]
                if PAIR_PX[0] <= math.hypot(dx, dy) <= PAIR_PX[1] and abs(dy) <= 0.6 * abs(dx) + 3:
                    a["pair"] = True
        H, W = frame.shape[:2]
        for s in live:
            s["uv"] = (s["px"][0] / W, s["px"][1] / H)
        fresh = self._track(live, now)
        self.stats["ms"] = round((time.perf_counter() - t) * 1000.0, 1)
        return fresh

    def _decay(self, now, shape):
        gh, gw = shape[0] // HEAT_CELL + 1, shape[1] // HEAT_CELL + 1
        if self.heat is None or self.heat.shape != (gh, gw):
            self.heat = np.zeros((gh, gw), np.float32)
            self.heat_t = now
        dt = now - self.heat_t
        if dt > 0:
            self.heat *= math.exp(-dt / HEAT_TAU)
            self.heat_t = now

    def _track(self, spots, now):
        with self.lock:
            self.tracks = [t for t in self.tracks if now - t.t_last <= LOST_S]
            used = set()
            # helle Punkte zuerst, je Punkt die naechste Spur
            for s in sorted(spots, key=lambda s: -s["peak"]):
                best, bd = None, LINK_PX
                for tr in self.tracks:
                    if tr.id in used:
                        continue
                    d = math.hypot(s["px"][0] - tr.px[0], s["px"][1] - tr.px[1])
                    if d < bd:
                        best, bd = tr, d
                if best is None:
                    # zweites Auge einer bestehenden Spur? dann keine neue Spur
                    if s["pair"] and any(math.hypot(s["px"][0] - tr.px[0], s["px"][1] - tr.px[1])
                                         <= PAIR_PX[1] for tr in self.tracks):
                        continue
                    best = EyeTrack(self.next_id, s, now)
                    self.next_id += 1
                    self.tracks.append(best)
                else:
                    best.px, best.uv, best.t_last = s["px"], s["uv"], now
                    best.hits += 1
                    best.pair = best.pair or s["pair"]
                    best.peak = max(best.peak, s["peak"])
                    best.path.append(s["px"])
                    del best.path[:-40]
                used.add(best.id)
            fresh = []
            for tr in self.tracks:
                if not tr.reported and tr.hits >= MIN_HITS:
                    tr.reported = True
                    self.stats["eyes"] += 1
                    fresh.append(tr)
            return fresh

    def active(self):
        """Bestaetigte, gerade gesehene Augen (fuer Bus/Overlay)."""
        with self.lock:
            return [tr for tr in self.tracks if tr.reported]

    def status(self):
        with self.lock:
            return dict(self.stats, night=self.night, tracks=len(self.tracks),
                        masked=self.mask is not None)
