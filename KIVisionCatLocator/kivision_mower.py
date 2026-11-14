"""Steht der Rasenmaeher in seinem Dock? (2026-11-14)

Die Coral-KI (COCO) erkennt den Maeher bei Tag zuverlaessig als "car"
(72-98 %, Bilder 08.11.). Liegt ein solcher Rahmen mit seiner Mitte im
Dock-Kasten (Bildanteile, Default um den Maeher an der Hecke rechts), steht er
dort. Nachts maeht er nie (Igel) - dann meldet die Kamera nur "Nacht" und
keine Dock-Aussage; der Cat Identifier wertet das als "maeht nicht".

Zustand mit Entprellung:
  home  - Maeher im Dock seit home_s (er steht still, wenn er laedt; faehrt er
          beim Maehen nur am Dock vorbei, reicht das nicht)
  away  - im Dock seit away_s kein Maeher mehr
  unknown - Nacht, KI aus oder noch keine Aussage
"Dock leer" heisst NICHT "maeht" (Winterquartier, steht woanders) - das
entscheidet der Cat Identifier zusammen mit seinen eigenen langen Spuren.

Zusaetzlich ("lawn"): die KI sah in den letzten 60 s ausserhalb des Docks
mindestens 3x einen fahrenden Maeher (Rahmen wandert) - Zusatzindiz "maeht".
"""

import threading
import time

CAR_LABELS = ("car", "truck", "bus")
DEFAULTS = {
    "enabled": True,
    "box": [0.80, 0.47, 0.96, 0.69],   # x0, y0, x1, y1 als Bildanteil
    "car_min": 0.35,                   # KI-Sicherheit ab der ein Rahmen zaehlt
    "home_s": 60,
    "min_lux": 150,                    # darunter Daemmerung -> keine Aussage (Tag ~1000-20000)
    "away_s": 20,
    "lawn_window_s": 60,
    "lawn_min_hits": 3,
    "lawn_min_move": 0.04,             # Rahmenmitte muss so weit wandern (Bildanteil)
}
STALE_S = 10.0                         # so lange ohne KI-Runde -> unknown


class DockWatch:
    def __init__(self, cfg_get):
        self._cfg = cfg_get              # -> dict mit Werten aus DEFAULTS
        self.lock = threading.Lock()
        self.state = "unknown"
        self.since = time.time()
        self.night = False
        self.lux = None
        self.valid_since = None          # monotonic: seit wann eine Aussage moeglich ist
        self.ai_at = 0.0                 # monotonic der letzten KI-Runde
        self.seen_at = 0.0               # monotonic: Maeher zuletzt im Dock gesehen
        self.seen_first = None           # Beginn der laufenden Im-Dock-Serie
        self.conf = 0
        self.lawn = []                   # (monotonic, cx, cy) Maeher ausserhalb des Docks
        self.changes = []                # warten auf den VPS

    def cfg(self):
        c = dict(DEFAULTS)
        c.update(self._cfg() or {})
        return c

    def feed(self, raw):
        """Nach jeder KI-Runde: alle Rahmen (Bildanteile x/y/w/h, label, score)."""
        c = self.cfg()
        x0, y0, x1, y1 = c["box"]
        now = time.monotonic()
        best = 0.0
        lawn = []
        for b in raw or []:
            if b.get("label", "").lower() not in CAR_LABELS or b["score"] < c["car_min"]:
                continue
            cx, cy = b["x"] + b["w"] / 2, b["y"] + b["h"] / 2
            if x0 <= cx <= x1 and y0 <= cy <= y1:
                best = max(best, b["score"])
            else:
                lawn.append((now, cx, cy))
        with self.lock:
            self.ai_at = now
            if best > 0:
                if self.seen_first is None or now - self.seen_at > 15.0:
                    self.seen_first = now           # Serie neu (Luecke > 15 s)
                self.seen_at = now
                self.conf = round(best * 100)
            win = float(c["lawn_window_s"])
            self.lawn = [p for p in self.lawn + lawn if now - p[0] <= win]

    def lawn_moving(self):
        c = self.cfg()
        with self.lock:
            pts = list(self.lawn)
        if len(pts) < int(c["lawn_min_hits"]):
            return False
        spread = max(((a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5
                     for a in pts for b in pts)
        return spread >= float(c["lawn_min_move"])

    def update(self, night, lux=None):
        """Zustand neu bestimmen (alle paar Sekunden). True = gewechselt.
        Daemmerung (lux < min_lux) zaehlt wie Nacht: die KI erkennt den
        Maeher dann schlechter, ein falsches "Dock leer" waere die Folge."""
        c = self.cfg()
        now = time.monotonic()
        with self.lock:
            self.night = bool(night)
            self.lux = lux
            dim = lux is not None and lux < float(c["min_lux"])
            if not c["enabled"] or night or dim or now - self.ai_at > STALE_S:
                self.valid_since = None
                new = "unknown"
            else:
                if self.valid_since is None:
                    self.valid_since = now
                seen = self.seen_at and now - self.seen_at <= 15.0
                if seen and now - self.seen_first >= float(c["home_s"]):
                    new = "home"
                elif (now - self.valid_since >= float(c["away_s"])
                      and now - (self.seen_at or 0.0) >= float(c["away_s"])):
                    new = "away"
                else:
                    new = self.state             # dazwischen: alten Zustand halten
            if new == self.state:
                return False
            old, self.state, self.since = self.state, new, time.time()
            self.changes = (self.changes + [{"t": round(self.since, 1), "state": new,
                                              "old": old, "conf": self.conf,
                                              "night": self.night,
                                              "lux": round(lux) if lux is not None else None}])[-50:]
            return True

    def take_changes(self):
        with self.lock:
            out, self.changes = self.changes, []
        return out

    def status(self):
        lawn = self.lawn_moving()
        with self.lock:
            return {"state": self.state, "since": round(self.since, 1),
                    "night": self.night, "conf": self.conf, "lux": self.lux,
                    "lawn": lawn,
                    "seen_age": (round(time.monotonic() - self.seen_at, 1)
                                 if self.seen_at else None)}
