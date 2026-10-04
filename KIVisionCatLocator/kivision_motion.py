"""Bewegungserkennung fuer KIVision (2026-10-04).

Warum: Die COCO-/ImageNet-Modelle erkennen die Katze erst, wenn sie ganz vorn
ist. Am hinteren Rasenende, wo sie fast immer hereinkommt, ist sie im
1640er-Bild nur ~25x45 Pixel gross - nachgemessen an den Radar-Bildern vom
2026-10-04 mit sieben Modellen (SSD-MobileNet, MobileDet, EfficientDet-Lite2/3,
MobileNet/EfficientNet/Inception als Klassifizierer auf Ausschnitten): keines
nennt sie dort "cat", und ein Blumentopf bekommt dieselben Werte. Das Etikett
traegt dort nicht. Was ein so kleines Ziel aber zuverlaessig verraet, ist, dass
es sich *bewegt* - und zwar ueber den Rasen, nicht im Wind auf der Stelle.

Darum hier ein klassischer Hintergrundabzug (MOG2) auf dem kleinen Graubild
(lores-Strom, Y-Ebene) plus ein einfacher Verfolger. Ergebnis sind Spuren mit
Rahmen, Alter und zurueckgelegtem Weg. Ein stehender Maeher erzeugt keine
Spur, eine wehende Hecke keine *bewegte* (sie zittert auf der Stelle).

Der Detector nutzt die bewegten Spuren, um gezielt dort einen Ausschnitt aus
dem grossen Bild zu nehmen - statt das ganze Bild in Kacheln zu zerlegen.
"""

import threading
import time

import numpy as np

try:
    import cv2
except ImportError:                      # ohne OpenCV: keine Bewegungserkennung
    cv2 = None

# Spur gilt als "bewegt", wenn sie mindestens so weit gewandert ist (Anteil der
# Bildbreite bzw. ihrer eigenen Groesse - das Groessere zaehlt).
MIN_TRAVEL = 0.012
MIN_TRAVEL_REL = 0.6
MIN_HITS = 3
LOST_S = 2.0                # so lange ungesehen -> Spur weg
MAX_GLOBAL = 0.20           # mehr Vordergrund als das: Licht hat sich geaendert


class Track:
    __slots__ = ("id", "t0", "t_last", "box", "c0", "c", "hits", "travel",
                 "moving", "t_moving", "cls", "path", "snapped")

    def __init__(self, tid, box, now):
        self.id = tid
        self.t0 = self.t_last = now
        self.box = box                       # x, y, w, h normiert
        self.c0 = self.c = _center(box)
        self.hits = 1
        self.travel = 0.0
        self.moving = False
        self.t_moving = None
        # Was die KI im Ausschnitt sah: Kategorie -> (Score, Etikett)
        self.cls = {}
        self.path = [self.c]
        self.snapped = False

    def size(self):
        return max(self.box[2], self.box[3])

    def as_dict(self, now):
        best = self.best()
        return {
            "id": self.id,
            "x": round(self.box[0], 4), "y": round(self.box[1], 4),
            "w": round(self.box[2], 4), "h": round(self.box[3], 4),
            "age": round(now - self.t0, 1),
            "moving": self.moving,
            "hits": self.hits,
            "travel": round(self.travel, 3),
            "cls": {k: [round(v[0], 2), v[1]] for k, v in self.cls.items()},
            "best": best,
            # Fusspunkt (Mitte unten) = wo das Ziel den Boden beruehrt; das
            # ist der Punkt, den eine Homographie in Weltkoordinaten abbildet.
            "foot": [round(self.box[0] + self.box[2] / 2, 4),
                     round(self.box[1] + self.box[3], 4)],
            "path": [[round(p[0], 4), round(p[1], 4)] for p in self.path[::3]],
        }

    def best(self):
        """Kurzer Text fuer Bilder/Anzeige: staerkste KI-Kategorie."""
        if not self.cls:
            return ""
        cat, (score, label) = max(self.cls.items(), key=lambda kv: kv[1][0])
        return "%s %d%%" % (label, round(score * 100))


def _center(b):
    return (b[0] + b[2] / 2.0, b[1] + b[3] / 2.0)


class Motion:
    """Haelt Hintergrundmodell und Spuren. update() je Bild aufrufen."""

    def __init__(self):
        self.lock = threading.Lock()
        self.bg = None
        self.tracks = []
        self.next_id = 1
        self.ms = 0.0
        self.global_skip = 0
        self.frames = 0
        self.size = (0, 0)
        self.fg_frac = 0.0
        self.enabled = cv2 is not None

    def reset(self):
        with self.lock:
            self.bg = None
            self.tracks = []

    def _new_bg(self):
        # history ~40 s bei 8 fps: Wolken/Sonne zieht es nach, eine Katze, die
        # ein paar Sekunden still sitzt, verschwindet nicht gleich wieder.
        bg = cv2.createBackgroundSubtractorMOG2(history=300, varThreshold=36,
                                                detectShadows=True)
        bg.setShadowThreshold(0.6)
        return bg

    def update(self, gray, now=None):
        """gray: uint8 HxW (Y-Ebene des lores-Stroms)."""
        if cv2 is None:
            return []
        now = time.monotonic() if now is None else now
        started = time.monotonic()
        h, w = gray.shape[:2]
        if self.bg is None or self.size != (w, h):
            self.bg = self._new_bg()
            self.size = (w, h)
        g = cv2.GaussianBlur(gray, (5, 5), 0)
        fg = self.bg.apply(g)
        mask = (fg == 255).astype(np.uint8)              # 127 = Schatten
        frac = float(mask.mean())
        self.fg_frac = frac
        self.frames += 1
        blobs = []
        if self.frames < 15:
            pass                                         # Modell lernt noch
        elif frac > MAX_GLOBAL:
            # Sonne kam/ging, Belichtung sprang: alles ist "Vordergrund".
            # Schneller nachlernen und diese Bilder nicht als Bewegung werten.
            self.global_skip += 1
            self.bg.apply(g, learningRate=0.2)
        else:
            k3 = np.ones((3, 3), np.uint8)
            mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k3)
            # Katzenteile (Kopf, Rumpf, Schwanz) zu einem Fleck verbinden
            mask = cv2.dilate(mask, np.ones((7, 7), np.uint8))
            n, _lab, stats, _c = cv2.connectedComponentsWithStats(mask, connectivity=8)
            min_area = max(30, int(w * h * 0.00008))
            max_area = int(w * h * 0.25)
            for i in range(1, n):
                x, y, bw, bh, area = stats[i]
                if area < min_area or area > max_area:
                    continue
                blobs.append((x / w, y / h, bw / w, bh / h))
        with self.lock:
            self._track(blobs, now)
            self.ms = round((time.monotonic() - started) * 1000, 1)
            return [t.as_dict(now) for t in self.tracks if t.moving]

    def _track(self, blobs, now):
        free = list(range(len(blobs)))
        # Gierig: naechstgelegener Fleck je Spur (aelteste Spuren zuerst)
        for t in sorted(self.tracks, key=lambda t: t.t0):
            if not free:
                break
            tc = t.c
            gate = 0.04 + 1.5 * t.size()
            best, bd = None, None
            for i in free:
                c = _center(blobs[i])
                d = ((c[0] - tc[0]) ** 2 + (c[1] - tc[1]) ** 2) ** 0.5
                if d < gate and (bd is None or d < bd):
                    best, bd = i, d
            if best is None:
                continue
            free.remove(best)
            t.box = blobs[best]
            t.c = _center(t.box)
            t.t_last = now
            t.hits += 1
            t.path.append(t.c)
            if len(t.path) > 200:
                t.path.pop(0)
            d0 = ((t.c[0] - t.c0[0]) ** 2 + (t.c[1] - t.c0[1]) ** 2) ** 0.5
            t.travel = max(t.travel, d0)
            if (not t.moving and t.hits >= MIN_HITS
                    and t.travel >= max(MIN_TRAVEL, MIN_TRAVEL_REL * t.size())):
                t.moving = True
                t.t_moving = now
        for i in free:
            self.tracks.append(Track(self.next_id, blobs[i], now))
            self.next_id += 1
        keep = []
        for t in self.tracks:
            if now - t.t_last <= LOST_S:
                keep.append(t)
            elif t.moving:
                # Fuers Nachjustieren: jede bewegte Spur einmal im Journal
                print("[motion] Spur B%d vorbei: %.1f s, Weg %.3f, Groesse %.3f, "
                      "Start %.2f/%.2f Ende %.2f/%.2f, KI %s" % (
                          t.id, t.t_last - t.t0, t.travel, t.size(), t.c0[0], t.c0[1],
                          t.c[0], t.c[1], t.best() or "-"))
        self.tracks = keep
        if len(self.tracks) > 40:                    # Notbremse (Regen, Schnee)
            self.tracks = sorted(self.tracks, key=lambda t: -t.hits)[:40]

    def moving(self):
        """Kopie der bewegten Spuren (fuer den Detector)."""
        now = time.monotonic()
        with self.lock:
            return [t.as_dict(now) for t in self.tracks if t.moving]

    def claim_snap(self, min_age):
        """Spuren, die seit min_age Sekunden bewegt sind und noch kein Bild
        bekommen haben - jede genau einmal."""
        now = time.monotonic()
        out = []
        with self.lock:
            for t in self.tracks:
                if t.moving and not t.snapped and now - t.t_moving >= min_age:
                    t.snapped = True
                    out.append(t.as_dict(now))
        return out

    def classify(self, tid, category, score, label):
        """Detector meldet, was er im Ausschnitt einer Spur gesehen hat."""
        with self.lock:
            for t in self.tracks:
                if t.id == tid:
                    old = t.cls.get(category)
                    if old is None or score > old[0]:
                        t.cls[category] = (score, label)
                    return

    def status(self):
        with self.lock:
            return {"enabled": self.enabled, "ms": self.ms, "frames": self.frames,
                    "fg": round(self.fg_frac, 4), "global_skip": self.global_skip,
                    "tracks": len(self.tracks),
                    "moving": sum(1 for t in self.tracks if t.moving)}
