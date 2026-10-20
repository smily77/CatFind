"""Leuchtende Augen nachts (Phase 5C, 2026-10-18; Paarsuche 2026-10-19).

Warum: Nachts ist die Katze ausserhalb des IR-Lichts kaum zu sehen und auch
darin fuer die KI-Modelle zu klein. Ihre Augen werfen das IR-Licht aber fast
genau zur Kamera zurueck (Tapetum lucidum) und leuchten als kleine helle
Flecken - im 2592er-Bild am hinteren Rasenende je ~5..7 px gross, Mitte zu
Mitte ~8..10 px auseinander, fast genau waagrecht (gemessen an den Tracks
81603/81605/81607/81624/81628). Darum laeuft das hier auf dem grossen Bild,
nur nachts und nur in der gewaehlten Zone.

Die erste Fassung suchte EINZELNE helle Punkte. Nachts (Regen, IR-Licht am
Dome-C1) rauscht das Bild aber so stark, dass fast jedes Bild ueber 40 solche
Punkte hatte und als "verrauscht" verworfen wurde (98 % der Nachtbilder) -
die Augen der Katzen kamen nie an. Jetzt:

  * Gruenkanal leicht weichzeichnen: Einzelpixel-Rauschen verschwindet, ein
    5-px-Augenfleck bleibt.
  * Top-Hat: kleine helle Flecken auf dunklem Grund bleiben, grosse helle
    Flaechen (Maeher-Scheinwerfer, ausgeleuchteter Rasen) fallen weg.
  * Ein Fleck zaehlt, wenn er klar heller ist als seine Umgebung.
  * AUGE = ZWEI solche Flecken waagrecht nebeneinander, aehnlich gross und
    aehnlich hell, mit dunklerer Luecke dazwischen. Der Abstand passt zur
    Fleckgroesse (getrennte Flecken, nicht ein langgezogener).
  * Eine Augen-Spur entsteht nur aus einem Paar. Dreht die Katze den Kopf,
    sieht man oft nur noch ein Auge - eine bestehende Spur laeuft dann mit
    einem einzelnen Fleck in der Naehe weiter.
  * Ortsfeste Lichter (Lampen, Maeher-LED, Reflektoren) lernt eine langsam
    vergessende Waermekarte - wo dauernd etwas leuchtet, zaehlt nichts.
  * Ein Auge gilt nach MIN_PAIRS Paar-Treffern derselben Spur; Einzelflecken
    halten eine Spur nur am Leben, bestaetigen sie aber nicht.
"""

import math
import threading
import time

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None

BLUR = 5                  # Weichzeichner-Kern (px): schluckt Einzelpixel-Rauschen
KERNEL = 21               # Top-Hat-Kern (px im 2592er-Bild): groesser = nur noch Flaechen
MIN_TOPHAT = 28           # so viel heller als die Umgebung muss ein Fleck sein
MIN_PEAK = 45             # und absolut so hell (Gruenkanal 0..255, weichgezeichnet)
MIN_RATIO = 2.0           # Spitze / (Umgebung + 15)
BRIGHT_PEAK = 235         # fast gesaettigt: zaehlt auch auf hellem Grund
LAMP_PEAK = 200           # grosse helle Flaeche (Scheinwerfer des Maehers) ...
LAMP_AREA = 400           # ... ab so vielen px; Flecken bis LAMP_GROW px daneben
LAMP_GROW = 60            #     sind Teil der Lampe, nicht ein Auge
PEAK_WIN = 5              # Spitze = hellster Punkt in diesem Fenster (< Augenabstand)
MIN_AREA = 2              # px (Breite x Hoehe auf halber Hoehe); kleiner ist Rauschen
MAX_AREA = 200            # px; laengere Belichtung zieht ein bewegtes Auge zum Strich
MAX_SIDE = 24
MAX_SPOTS = 400           # mehr Flecken: Bild ist Matsch (Regen direkt vor der Linse)
# Paar: Mittenabstand waagrecht (hinten am Rasen ~9 px, nah an der Kamera mehr)
PAIR_DX = (6, 60)
PAIR_DY = 0.3             # |dy| <= PAIR_DY * dx + 2: fast waagrecht
PAIR_SEP = 1.0            # dx >= PAIR_SEP * Fleckbreite: zwei getrennte Flecken ...
PAIR_SPAN = 7.0           # ... und dx <= PAIR_SPAN * Fleckbreite: nicht weit auseinander
PAIR_AREA = 4.0           # Flaechen hoechstens so verschieden
PAIR_PEAK = 2.5           # Helligkeit (ueber Grund) hoechstens so verschieden
PAIR_GAP = 0.65           # Luecke dazwischen dunkler als Grund + 65 % des schwaecheren Flecks
MAX_PAIRS = 12            # mehr Paare in einem Bild: Rauschen -> Bild verwerfen
HEAT_TAU = 300.0          # s, so schnell vergisst die Waermekarte
HEAT_STATIC = 6.0         # ab so viel "Dauerleuchten" an einer Stelle: ortsfestes Licht
HEAT_CELL = 16            # px je Zelle der Waermekarte
LINK_PX = 150             # Verfolgung: naechster Treffer hoechstens so weit weg
SINGLE_PX = 60            # Fortsetzung mit nur einem Auge: so nah an der Spur
LOST_S = 2.0
MIN_PAIRS = 2


def _run(line, i, level):
    """Wie viele Pixel um line[i] herum liegen am Stueck ueber level."""
    a = b = i
    while a > 0 and line[a - 1] >= level:
        a -= 1
    while b < len(line) - 1 and line[b + 1] >= level:
        b += 1
    return b - a + 1


class EyeTrack:
    __slots__ = ("id", "t0", "t_last", "uv", "px", "hits", "pair", "peak", "path",
                 "reported", "t_sent", "dx", "npairs")

    def __init__(self, tid, spot, now):
        self.id = tid
        self.t0 = self.t_last = now
        self.px = spot["px"]
        self.uv = spot["uv"]
        self.hits = 1
        self.pair = True
        self.peak = spot["peak"]
        self.dx = spot["dx"]
        self.npairs = 1
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
        self.dark = None         # Helligkeit oben/Mitte (kivision_web._dark_ratio)
        self.stats = {"frames": 0, "spots": 0, "pairs": 0, "static": 0, "noisy": 0,
                      "eyes": 0, "ms": 0.0}
        # Rechteckkern: OpenCV rechnet ihn zerlegt und viel schneller als eine
        # Ellipse (2592er-Bild: ~290 -> ~60 ms gemessen auf dem Pi 4)
        self.kernel = (cv2.getStructuringElement(cv2.MORPH_RECT, (KERNEL, KERNEL))
                       if cv2 is not None else None)
        self.peak_kernel = (cv2.getStructuringElement(cv2.MORPH_RECT, (PEAK_WIN, PEAK_WIN))
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
    def spots(self, g):
        """Helle Flecken im (Ausschnitt-)Graubild g: Liste von dicts mit px
        (relativ zu g), peak, bg, area, w. mask=None = ganzes Bild."""
        g = cv2.GaussianBlur(g, (BLUR, BLUR), 0)
        th = cv2.morphologyEx(g, cv2.MORPH_TOPHAT, self.kernel)
        cand = (th >= MIN_TOPHAT) & (g >= MIN_PEAK)
        if self.mask is not None and self.mask.shape == g.shape:
            cand &= self.mask > 0
        cand = cand.astype(np.uint8)
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
        # Flecken = oertliche Helligkeitsspitzen, nicht zusammenhaengende
        # Flaechen ueber einer Schwelle: zwei Augen ~9 px auseinander
        # verschmelzen an der Schwelle oft zu EINER Flaeche, ihre zwei Spitzen
        # bleiben aber getrennt.
        cand &= (g >= cv2.dilate(g, self.peak_kernel)).astype(np.uint8)
        # Spitze deutlich heller als ihr Grund (g - th) - fuer alle Pixel auf
        # einmal: im ausgeleuchteten Rasen ist jeder Grashalm eine Spitze,
        # Tausende davon einzeln in Python zu pruefen kostete ~2 s je Bild.
        gi = g.astype(np.int16)
        bright = (gi >= BRIGHT_PEAK) | (gi * 10 >= int(MIN_RATIO * 10) * (gi - th + 15))
        cand &= bright.astype(np.uint8)
        n, _lab, _st, cen = cv2.connectedComponentsWithStats(cand, connectivity=8)
        if n <= 1:
            return [], g
        if n - 1 > MAX_SPOTS:
            return None, g
        out = []
        for cx, cy in cen[1:]:
            x, y = int(round(cx)), int(round(cy))
            peak = int(g[y, x])
            bg = peak - int(th[y, x])
            # Groesse auf halber Hoehe ueber dem Grund (an der niedrigen
            # Schwelle gemessen waeren Flecken viel zu breit)
            half = bg + (peak - bg) / 2.0
            row, col = g[y, max(0, x - MAX_SIDE):x + MAX_SIDE + 1], g[max(0, y - MAX_SIDE):y + MAX_SIDE + 1, x]
            w = _run(row, min(x, MAX_SIDE), half)
            h = _run(col, min(y, MAX_SIDE), half)
            if w > MAX_SIDE or h > MAX_SIDE or w * h > MAX_AREA or w * h < MIN_AREA:
                continue
            out.append({"px": (float(cx), float(cy)), "peak": peak, "bg": bg,
                        "area": w * h, "w": w})
        return out, g

    @staticmethod
    def pairs(spots, g):
        """Augenpaare: je zwei Flecken waagrecht nebeneinander. Jeder Fleck in
        hoechstens einem Paar (das passendste gewinnt)."""
        cand = []
        order = sorted(range(len(spots)), key=lambda i: spots[i]["px"][0])
        for ii, i in enumerate(order):
            a = spots[i]
            for j in order[ii + 1:]:
                b = spots[j]
                dx = b["px"][0] - a["px"][0]
                if dx > PAIR_DX[1]:
                    break
                dy = abs(b["px"][1] - a["px"][1])
                if dx < PAIR_DX[0] or dy > PAIR_DY * dx + 2:
                    continue
                wmax = max(a["w"], b["w"])
                if dx < PAIR_SEP * wmax or dx > PAIR_SPAN * wmax:
                    continue
                if max(a["area"], b["area"]) > PAIR_AREA * min(a["area"], b["area"]):
                    continue
                ha, hb = a["peak"] - a["bg"], b["peak"] - b["bg"]
                if max(ha, hb) > PAIR_PEAK * max(1, min(ha, hb)):
                    continue
                # Luecke: auf der Verbindungslinie muss es dunkler werden
                n = max(3, int(dx))
                xs = np.linspace(a["px"][0], b["px"][0], n)[1:-1]
                ys = np.linspace(a["px"][1], b["px"][1], n)[1:-1]
                line = g[np.clip(np.round(ys).astype(int), 0, g.shape[0] - 1),
                         np.clip(np.round(xs).astype(int), 0, g.shape[1] - 1)]
                bg = max(a["bg"], b["bg"])
                if line.min() > bg + PAIR_GAP * (min(a["peak"], b["peak"]) - bg):
                    continue
                cost = dy / (dx + 1) + abs(ha - hb) / max(ha, hb) + abs(a["w"] - b["w"]) / wmax
                cand.append((cost, i, j, dx))
        used, out = set(), []
        for cost, i, j, dx in sorted(cand):
            if i in used or j in used:
                continue
            used.update((i, j))
            a, b = spots[i], spots[j]
            out.append({"px": ((a["px"][0] + b["px"][0]) / 2, (a["px"][1] + b["px"][1]) / 2),
                        "peak": max(a["peak"], b["peak"]), "dx": dx, "parts": (i, j)})
        return out, used

    def update(self, frame, now=None):
        """Neue Augen-Spuren, die gerade bestaetigt wurden."""
        if cv2 is None or self.mask is None:
            return []
        now = time.monotonic() if now is None else now
        t = time.perf_counter()
        x0, y0, x1, y1 = self.box
        g = np.ascontiguousarray(frame[y0:y1, x0:x1, 1])
        spots, gb = self.spots(g)
        self._decay(now, frame.shape)
        self.stats["frames"] += 1
        if spots is None:
            self.stats["noisy"] += 1
            self.stats["ms"] = round((time.perf_counter() - t) * 1000.0, 1)
            return []
        pairs, used = self.pairs(spots, gb)
        self.stats["spots"] += len(spots)
        if len(pairs) > MAX_PAIRS:
            self.stats["noisy"] += 1
            self.stats["ms"] = round((time.perf_counter() - t) * 1000.0, 1)
            return []
        H, W = frame.shape[:2]

        def to_frame(p):
            p["px"] = (p["px"][0] + x0, p["px"][1] + y0)
            p["uv"] = (p["px"][0] / W, p["px"][1] / H)
            return p

        # ortsfeste Lichter: Waermekarte erst lesen, dann fuettern
        live = []
        for p in pairs:
            to_frame(p)
            cx, cy = int(p["px"][0] // HEAT_CELL), int(p["px"][1] // HEAT_CELL)
            hv = self.heat[max(0, cy - 1):cy + 2, max(0, cx - 1):cx + 2].max()
            self.heat[cy, cx] += 1.0
            if hv >= HEAT_STATIC:
                self.stats["static"] += 1
                continue
            live.append(p)
        self.stats["pairs"] += len(live)
        singles = [to_frame(dict(s, dx=0)) for k, s in enumerate(spots) if k not in used]
        fresh = self._track(live, singles, now)
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

    @staticmethod
    def _follow(tr, s, now):
        tr.px, tr.uv, tr.t_last = s["px"], s["uv"], now
        tr.hits += 1
        tr.peak = max(tr.peak, s["peak"])
        tr.path.append(s["px"])
        del tr.path[:-40]

    def _track(self, pairs, singles, now):
        with self.lock:
            self.tracks = [t for t in self.tracks if now - t.t_last <= LOST_S]
            used = set()
            # Paare: naechste Spur fortsetzen oder neue Spur
            for p in sorted(pairs, key=lambda p: -p["peak"]):
                best, bd = None, LINK_PX
                for tr in self.tracks:
                    if tr.id in used:
                        continue
                    d = math.hypot(p["px"][0] - tr.px[0], p["px"][1] - tr.px[1])
                    if d < bd:
                        best, bd = tr, d
                if best is None:
                    best = EyeTrack(self.next_id, p, now)
                    self.next_id += 1
                    self.tracks.append(best)
                else:
                    self._follow(best, p, now)
                    best.dx = p["dx"]
                    best.npairs += 1
                used.add(best.id)
            # einzelnes Auge: nur eine bestehende Spur weiterfuehren (Kopf gedreht)
            for tr in self.tracks:
                if tr.id in used:
                    continue
                reach = max(SINGLE_PX, 4 * tr.dx)
                near = [s for s in singles
                        if math.hypot(s["px"][0] - tr.px[0], s["px"][1] - tr.px[1]) <= reach]
                if near:
                    self._follow(tr, max(near, key=lambda s: s["peak"]), now)
                    used.add(tr.id)
            fresh = []
            for tr in self.tracks:
                if not tr.reported and tr.npairs >= MIN_PAIRS:
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
            return dict(self.stats, night=self.night, dark=self.dark, tracks=len(self.tracks),
                        masked=self.mask is not None)
