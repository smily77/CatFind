"""Zielmodell "Sitzende Katze" - VPS-Nachbau des CatIdent-Moduls sitModel.ino.

Konzept: GesamtKonzeptCatFinder.md, Abschnitt "Zielmodell Sitzende Katze".
Das Normalmodell (catmodel.py / catTrack.ino) bleibt unberuehrt. Dieses Modell
meldet nur Tiere, die sich HINSETZEN - ausserhalb des Katzenkorridors nach sit_s,
im Korridor erst nach sit_corr_s. Es arbeitet mit SITZPLAETZEN statt Tracks: die
Radarspur einer sitzenden Katze reisst immer wieder ab (83472: 17,5 s Luecke), ein
Sitzplatz verzeiht Luecken bis sit_gap_s.

Ablauf je Meldung (sender, x, y, t) - identisch in sitModel.ino:
  1. Verlauf (Ring) merken (fuer die Ankunft).
  2. Sitzplaetze ohne Meldung seit sit_gap_s schliessen.
  3. Ausgeloeste Plaetze desselben Sensors schliessen, wenn die Meldung mehr als
     leave_mm entfernt ist (Tier ist weg).
  4. Meldung dem naechsten Platz desselben Sensors zuordnen (<= sit_r_mm vom
     Schwerpunkt), sonst neuen Platz eroeffnen. Beim Eroeffnen: Ankunft gesehen,
     wenn derselbe Sensor in den letzten arrive_window_s eine Meldung
     arrive_mm..arrive_max_mm entfernt hatte.
  5. Ausloesen, wenn: Ankunft gesehen, Dauer >= sit_s (bzw. sit_corr_s im
     Korridor), Streuung <= sit_sd_mm, Rate >= sit_min_rate, keine Sperrstelle.
  6. Ausgeloeste Plaetze: Nachfuehrung (targetTracked) hoechstens alle
     track_period_s, bis track_max_s nach dem Ausloesen.
Maeherzeit (quiet_t0/t1 aus den Modell-Parametern) und die No-Shot-Karte filtern
VOR dem Modell, wie im CatIdent.

Bewusst nur Listen/Ganzzahlen, kein numpy: 1:1 nach C portierbar.
"""
import math

DEFAULT_SIT = {
    "sit_enabled": 1,
    "sit_r_mm": 600,          # Radius eines Sitzplatzes um seinen Schwerpunkt
    "sit_tau_s": 8.0,         # Gedaechtnis fuer Schwerpunkt/Streuung (gleitend): der
                              # Anlauf soll die Streuung nicht verderben (81320)
    "sit_s": 20.0,            # Sitzdauer bis zum Ausloesen ausserhalb des Korridors
    "sit_corr_s": 90.0,       # ... im Katzenkorridor (0 = dort nie)
    "sit_gap_s": 25.0,        # so lange darf die Meldung ausbleiben
    "sit_sd_mm": 150,         # max. Streuung (Standardabweichung x und y)
    "sit_sd_min_mm": 10,      # min. Streuung JE Achse: bitgenau eingefrorene Radarziele raus
    "sit_min_rate": 1.0,      # Meldungen/s im Mittel ...
    "sit_max_rate": 7.0,      # ... und hoechstens: eine sitzende Katze verliert das Radar
                              # immer wieder (81320: 4,8/s, 83472: 5,3/s), eingefrorene
                              # Ziele und laufende Tiere melden voll (~10/s)
    "arrive_mm": 500,         # Ankunft: vorher so weit weg gewesen ...
    "arrive_max_mm": 4000,    # ... aber nicht weiter (anderes Ziel)
    "arrive_window_s": 10.0,  # ... innerhalb dieser Zeit vor dem Platz
    "leave_mm": 1000,         # weiter weg = Tier ist gegangen
    "track_period_s": 0.5,    # targetTracked hoechstens so oft
    "track_max_s": 120.0,     # so lange nach dem Ausloesen nachfuehren
    "sit_hlk_only": 1,        # nur HLK-Radare (Lidar ist ein schlechter Katzendetektor)
    "log_s": 10.0,            # (nur VPS) Sitzplaetze ab dieser Dauer aufzeichnen
}
MAX_CAND = 16
HIST = 64
PRE = 8                       # Punkte VOR dem Platz (Ankunft), je Platz gemerkt
FLAG_STATIONARY = 0x01        # == catDetFlagStationary
FLAG_CORRIDOR = 0x04          # == catDetFlagCorridor: im Korridor ausgeloest


def merged_sit(p):
    out = dict(DEFAULT_SIT)
    for k, v in (p or {}).items():
        if k in DEFAULT_SIT:
            out[k] = v
    return out


class Corridor:
    """Katzenkorridor als Rasterzellen (cell_mm), Welt-mm."""
    def __init__(self, cell_mm=500, cells=()):
        self.cell = int(cell_mm)
        self.cells = {(int(a), int(b)) for a, b in cells}

    def inside(self, x, y):
        return (int(math.floor(x / self.cell)), int(math.floor(y / self.cell))) in self.cells


class _Cand:
    __slots__ = ("sender", "t_first", "t_last", "n", "sx", "sy", "sxx", "syy", "arrived",
                 "arrive_mm", "fired", "t_fire", "t_track", "done", "in_corr", "run", "frz",
                 "lx", "ly", "maxgap", "x0", "y0", "pre", "w", "tu", "uid", "last_reason")
    # sx/sy/sxx/syy/w sind GLEITENDE Summen: bei jeder Meldung mit
    # exp(-dt/sit_tau_s) abgewertet (C: expf), n zaehlt alle Meldungen.

    def cx(self):
        return self.sx / self.w

    def cy(self):
        return self.sy / self.w

    def sd(self):
        vx = max(0.0, self.sxx / self.w - (self.sx / self.w) ** 2)
        vy = max(0.0, self.syy / self.w - (self.sy / self.w) ** 2)
        return math.sqrt(vx), math.sqrt(vy)


class SitModel:
    """Streaming-Modell. feed() liefert eine Liste von Ereignissen:
    ("fire", info) / ("track", info) / ("end", info)."""

    def __init__(self, params=None, corridor=None, blocks=()):
        self.p = merged_sit(params)
        self.corr = corridor or Corridor()
        self.blocks = [(float(b["x"]), float(b["y"]), float(b["r"])) for b in blocks or ()]
        self.cands = []
        self.hist = []                 # [(t, sender, x, y)] max HIST
        self._uid = 0

    def _info(self, c, t):
        sdx, sdy = c.sd()
        dur = c.t_last - c.t_first
        return {"uid": c.uid, "sender": c.sender, "t_first": c.t_first, "t_last": c.t_last, "t": t,
                "x": round(c.cx()), "y": round(c.cy()), "sit_s": round(dur, 1),
                "n": c.n, "sdx": round(sdx), "sdy": round(sdy),
                "rate": round(c.n / max(dur, 0.1), 2), "maxgap": round(c.maxgap, 1),
                "arrived": c.arrived, "arrive_mm": round(c.arrive_mm),
                "fired": c.fired, "t_fire": c.t_fire, "in_corr": c.in_corr, "frz": c.frz,
                "first_x": c.x0, "first_y": c.y0,
                "reason": "" if c.fired else (getattr(c, "last_reason", "") or "")}

    def _arrival(self, c):
        """Weitester Abstand eines Ankunftspunkts (Verlauf vor dem Platz + erster
        eigener Punkt) zum AKTUELLEN Schwerpunkt. Die Katze beginnt ihren Platz oft
        schon mit dem Anlauf (83472: erster Punkt 0,7 m vor dem Sitzort)."""
        cx, cy = c.cx(), c.cy()
        best = math.hypot(c.x0 - cx, c.y0 - cy)
        for hx, hy in c.pre:
            d = math.hypot(hx - cx, hy - cy)
            if d <= self.p["arrive_max_mm"]:
                best = max(best, d)
        return best

    def _blocked(self, x, y):
        return any(math.hypot(x - bx, y - by) <= br for bx, by, br in self.blocks)

    def reason(self, c):
        """Warum (noch) nicht ausgeloest? (fuer die Auswertung im Tab)"""
        p = self.p
        dur = c.t_last - c.t_first
        sdx, sdy = c.sd()
        incorr = self.corr.inside(c.cx(), c.cy())
        need = p["sit_corr_s"] if incorr else p["sit_s"]
        c.arrive_mm = self._arrival(c)
        c.arrived = c.arrive_mm >= p["arrive_mm"]
        if not c.arrived:
            return "keine Ankunft gesehen"
        if self._blocked(c.cx(), c.cy()):
            return "Sperrstelle"
        if incorr and p["sit_corr_s"] <= 0:
            return "im Korridor"
        if dur < need:
            return ("im Korridor, nur %.0f s < %.0f s" if incorr else "nur %.0f s < %.0f s") % (dur, need)
        if max(sdx, sdy) > p["sit_sd_mm"]:
            return "Streuung %.0f mm" % max(sdx, sdy)
        if min(sdx, sdy) < p["sit_sd_min_mm"]:
            return "eingefroren (Streuung %.0f mm)" % min(sdx, sdy)
        rate = c.n / max(dur, 0.1)
        if rate < p["sit_min_rate"]:
            return "zu wenige Meldungen"
        if rate > p["sit_max_rate"]:
            return "volle Melderate %.1f/s" % rate
        return ""

    def feed(self, t, sender, x, y):
        p = self.p
        out = []
        # 1. Verlauf
        self.hist.append((t, sender, x, y))
        if len(self.hist) > HIST:
            self.hist.pop(0)
        # 2. abgelaufene Plaetze
        keep = []
        for c in self.cands:
            if t - c.t_last > p["sit_gap_s"]:
                out.append(("end", self._info(c, t)))
            else:
                keep.append(c)
        self.cands = keep
        # 3. Tier hat einen ausgeloesten Platz verlassen
        keep = []
        for c in self.cands:
            if c.fired and c.sender == sender and math.hypot(x - c.cx(), y - c.cy()) > p["leave_mm"]:
                out.append(("end", self._info(c, t)))
            else:
                keep.append(c)
        self.cands = keep
        # 4. zuordnen oder eroeffnen
        best, bd = None, None
        for c in self.cands:
            if c.sender != sender:
                continue
            d = math.hypot(x - c.cx(), y - c.cy())
            if d <= p["sit_r_mm"] and (bd is None or d < bd):
                best, bd = c, d
        if best is None:
            if len(self.cands) >= MAX_CAND:            # aeltesten verdraengen
                old = min(self.cands, key=lambda c: c.t_last)
                out.append(("end", self._info(old, t)))
                self.cands.remove(old)
            c = _Cand()
            self._uid += 1
            c.uid = self._uid
            c.sender, c.t_first, c.t_last, c.n = sender, t, t, 1
            c.sx, c.sy, c.sxx, c.syy = float(x), float(y), float(x) * x, float(y) * y
            c.w, c.tu = 1.0, t
            c.fired, c.t_fire, c.t_track, c.done, c.in_corr = False, None, 0.0, False, False
            c.run, c.frz, c.lx, c.ly, c.maxgap, c.x0, c.y0 = 1, 1, x, y, 0.0, x, y
            c.arrived, c.arrive_mm, c.last_reason = False, 0.0, ""
            c.pre = []                                 # juengste Punkte desselben Sensors davor
            for ht, hs, hx, hy in reversed(self.hist[:-1]):
                if len(c.pre) >= PRE or t - ht > p["arrive_window_s"]:
                    break
                if hs == sender:
                    c.pre.append((hx, hy))
            self.cands.append(c)
        else:
            c = best
            c.maxgap = max(c.maxgap, t - c.t_last)
            c.t_last = t
            c.n += 1
            a = math.exp(-(t - c.tu) / p["sit_tau_s"])
            c.w = c.w * a + 1.0
            c.sx = c.sx * a + x
            c.sy = c.sy * a + y
            c.sxx = c.sxx * a + float(x) * x
            c.syy = c.syy * a + float(y) * y
            c.tu = t
            c.run = c.run + 1 if (x, y) == (c.lx, c.ly) else 1
            c.frz = max(c.frz, c.run)
            c.lx, c.ly = x, y
        # 5. ausloesen
        r = "" if c.fired else self.reason(c)
        if not c.fired:
            c.last_reason = r
        if not c.fired and r == "":
            c.fired, c.t_fire, c.t_track = True, t, t
            c.in_corr = self.corr.inside(c.cx(), c.cy())
            out.append(("fire", self._info(c, t)))
        # 6. nachfuehren
        elif c.fired and not c.done:
            if t - c.t_fire > p["track_max_s"]:
                c.done = True
            elif t - c.t_track >= p["track_period_s"]:
                c.t_track = t
                out.append(("track", self._info(c, t)))
        return out

    def why(self, c_info):
        """Grund aus einem info-dict (Platz schon beendet) - fuer die Liste."""
        return c_info.get("reason", "")

    def flush(self, t):
        """Alle offenen Plaetze als beendet melden (Ende einer Auswertung)."""
        out = [("end", self._info(c, t)) for c in self.cands]
        self.cands = []
        return out


def payload_flags(info):
    return FLAG_STATIONARY | (FLAG_CORRIDOR if info.get("in_corr") else 0)


def learn_corridor(tracks, cell_mm=500, thr=0.08):
    """Korridor aus den als Katze bewerteten Tracks: Zellen, durch die mindestens
    thr (Anteil) der Tracks laufen. tracks = Liste von Punktlisten [(t,x,y,...)]."""
    use = {}
    n = 0
    for pts in tracks:
        if len(pts) < 5:
            continue
        n += 1
        for k in {(int(math.floor(q[1] / cell_mm)), int(math.floor(q[2] / cell_mm))) for q in pts}:
            use[k] = use.get(k, 0) + 1
    cells = sorted(k for k, v in use.items() if n and v >= thr * n)
    return {"cell_mm": cell_mm, "cells": [list(k) for k in cells], "n_tracks": n, "thr": thr,
            "density": [[k[0], k[1], round(v / n, 3)] for k, v in sorted(use.items())] if n else []}
