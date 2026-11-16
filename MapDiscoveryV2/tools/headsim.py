"""Simulierter PanTiltHead_WM_V2 mit erfundenem Garten (fuer Tests der Karte).

Kopf steht an bekannter Stelle (HEAD_X, HEAD_Y) mit Nullrichtung HEAD_PHI.
Messbar sind: zwei Pfosten O=(0,0) und A=(A_X,0) und die Gartengrenze
GARDEN (offene Linie). Strahl ohne Treffer -> Messung FAILED.

Realistisch nachgebildet: Pan in Servo-Ticks gerundet (4096/360), Schraeg-
distanz = waagrecht / cos(Tilt), Antwortverzoegerung, Paketverlust in beide
Richtungen (DROP), Wiederholungen derselben seq werden nicht neu gemessen.

Als Modul: Sim(...).start() ; als Programm: python headsim.py
"""
import math
import random
import socket
import threading
import time

import mdv2

HEAD_X, HEAD_Y, HEAD_PHI_DEG = 3.2, 1.1, 75.0
A_X = 5.0
POLE_R = 0.02
GARDEN = [(-1.0, 0.0), (-1.6, 3.8), (1.5, 6.2), (5.8, 5.4), (7.4, 2.4), (7.0, 0.0)]
TICKS = 4096 / 360.0


def ray_hit(px, py, dx, dy):
    """Erster Treffer des Strahls (Einheitsvektor) mit Pfosten oder Grenze -> Distanz oder None."""
    best = None
    for cx, cy in ((0.0, 0.0), (A_X, 0.0)):
        t = (cx - px) * dx + (cy - py) * dy
        if t > 0:
            ex, ey = px + t * dx - cx, py + t * dy - cy
            if ex * ex + ey * ey <= POLE_R * POLE_R:
                best = t if best is None else min(best, t)
    for (x1, y1), (x2, y2) in zip(GARDEN, GARDEN[1:]):
        ex, ey = x2 - x1, y2 - y1
        den = dx * ey - dy * ex
        if abs(den) < 1e-12:
            continue
        t = ((x1 - px) * ey - (y1 - py) * ex) / den
        u = ((x1 - px) * dy - (y1 - py) * dx) / den
        if t > 0 and 0 <= u <= 1:
            best = t if best is None else min(best, t)
    return best


def pan_to(wx, wy):
    """Pan-Winkel (Grad), unter dem der Kopf den Weltpunkt sieht."""
    d = math.degrees(math.atan2(wy - HEAD_Y, wx - HEAD_X))
    p = HEAD_PHI_DEG - d
    return (p + 180) % 360 - 180


def dist_to_garden(x, y):
    best = 1e9
    for (x1, y1), (x2, y2) in zip(GARDEN, GARDEN[1:]):
        ex, ey = x2 - x1, y2 - y1
        u = max(0, min(1, ((x - x1) * ex + (y - y1) * ey) / (ex * ex + ey * ey)))
        best = min(best, math.hypot(x - x1 - u * ex, y - y1 - u * ey))
    return best


class Sim:
    def __init__(self, drop=0.2, meas_s=0.3, verbose=True):
        self.drop, self.meas_s, self.verbose = drop, meas_s, verbose
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", mdv2.PORT_HEAD))
        self.sock.settimeout(0.05)
        self.pan = self.tilt = 0.0
        self.flags = 0
        self.busy = None          # (seq, ende, ergebnis, addr)
        self.last = None          # (seq, packet)
        self.measures = 0
        self.rx = self.dropped = 0
        self.stop = False

    def start(self):
        threading.Thread(target=self.run, daemon=True).start()
        return self

    def tx(self, pkt, addr):
        if random.random() < self.drop:
            self.dropped += 1
            return
        self.sock.sendto(pkt, addr)

    def reply(self, seq, cmd, st, dist=0.0, pan=None, tilt=None):
        return mdv2.pack_reply(seq, cmd, st, dist, self.pan if pan is None else pan,
                               self.tilt if tilt is None else tilt, self.flags & 3, 1, 1)

    def measure(self, pan, tilt):
        pan_act = round(pan * TICKS) / TICKS
        a = math.radians(HEAD_PHI_DEG - pan_act)
        h = ray_hit(HEAD_X, HEAD_Y, math.cos(a), math.sin(a))
        if h is None:
            return mdv2.ST_FAILED, 0.0, pan_act
        return mdv2.ST_DONE, h / math.cos(math.radians(tilt)), pan_act

    def run(self):
        while not self.stop:
            if self.busy and time.time() >= self.busy[1]:
                seq, _, (st, d, pa), addr = self.busy
                pkt = self.reply(seq, mdv2.CMD_MEASURE, st, d, pa, self.tilt)
                self.last = (seq, pkt)
                self.busy = None
                self.tx(pkt, addr)
            try:
                data, addr = self.sock.recvfrom(64)
            except socket.timeout:
                continue
            except OSError:
                continue
            c = mdv2.unpack_cmd(data)
            if not c:
                continue
            self.rx += 1
            if random.random() < self.drop:
                self.dropped += 1
                continue
            self.flags = c["flags"]
            if c["cmd"] == mdv2.CMD_SET:
                if self.busy:
                    self.tx(self.reply(c["seq"], 1, mdv2.ST_BUSY), addr)
                else:
                    self.pan, self.tilt = c["pan"], c["tilt"]
                    self.tx(self.reply(c["seq"], 1, mdv2.ST_DONE), addr)
            elif c["cmd"] == mdv2.CMD_MEASURE:
                if self.busy:
                    self.tx(self.reply(c["seq"], 2, mdv2.ST_BUSY), addr)
                elif self.last and self.last[0] == c["seq"]:
                    self.tx(self.last[1], addr)
                else:
                    self.pan, self.tilt = c["pan"], c["tilt"]
                    self.measures += 1
                    res = self.measure(c["pan"], c["tilt"])
                    if self.verbose:
                        print(f"   [sim] Messung {c['seq']} pan={c['pan']:.3f} tilt={c['tilt']:.1f} "
                              f"-> {mdv2.ST_NAME[res[0]]} {res[1]:.4f}")
                    self.busy = (c["seq"], time.time() + self.meas_s, res, addr)
                    self.tx(self.reply(c["seq"], 2, mdv2.ST_ACCEPTED), addr)


if __name__ == "__main__":
    s = Sim().start()
    print("Simulator laeuft auf UDP", mdv2.PORT_HEAD, "- Strg+C beendet")
    while True:
        time.sleep(1)
