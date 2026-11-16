"""Serieller Testzugang zur PanTiltKarte_WM_V2 (Tab5).

    from karte import Karte
    k = Karte("COM19"); k.cmd("btn act"); k.wait_status("Origin OK"); pts = k.dump()

Oeffnet den Port ohne DTR/RTS (sonst Neustart des Boards).
"""
import re
import time

import serial


class Karte:
    def __init__(self, port="COM19", echo=True):
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.05
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        self.echo = echo
        self.lines = []
        self.buf = b""

    def _read(self):
        data = self.s.read(4096)
        if data:
            self.buf += data
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                t = line.decode("utf-8", "replace").rstrip("\r")
                self.lines.append(t)
                if self.echo and not t.startswith("PT ") and not t.startswith("DUMP") and t != "ok":
                    print("   [karte]", t)

    def reset(self, wait_pattern="Head verbunden", timeout=40):
        """Neustart per Befehl 'restart', wartet bis die Karte bereit ist."""
        self.s.write(b"restart\n")
        time.sleep(0.5)
        try:
            self.s.close()
        except Exception:
            pass
        mark = len(self.lines)
        for _ in range(40):
            time.sleep(0.5)
            try:
                self.s.open()
                break
            except Exception:
                pass
        return self.wait_status(wait_pattern, timeout, since=mark)

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            self._read()

    def cmd(self, text):
        """Sendet einen Befehl; gibt den Zeilenindex davor zurueck (fuer wait_status(since=...))."""
        mark = len(self.lines)
        self.s.write((text + "\n").encode())
        self.pump(0.15)
        return mark

    def wait_status(self, pattern, timeout=30.0, since=None):
        """Wartet auf eine STATUS-Zeile, die pattern (regex) enthaelt. Gibt sie zurueck oder None."""
        start = len(self.lines) if since is None else since
        end = time.time() + timeout
        while time.time() < end:
            self._read()
            for t in self.lines[start:]:
                if t.startswith("STATUS:") and re.search(pattern, t):
                    return t
        return None

    def dump(self):
        mark = self.cmd("dump")
        end = time.time() + 15          # USB-Seriell des Tab5 stockt manchmal einige Sekunden
        while time.time() < end and "DUMP end" not in self.lines[mark:]:
            self._read()
        info = {"points": []}
        lines = self.lines[mark:]
        if "DUMP end" in lines:
            lines = lines[:lines.index("DUMP end")]
        for t in lines:
            if t.startswith("PT "):
                _, i, pan, x, y = t.split()
                info["points"].append((float(pan), float(x), float(y)))
            elif t.startswith("DUMP calib="):
                m = re.search(r"calib=(\d) O=\(([-\d.]+),([-\d.]+)\) head=\(([-\d.]+),([-\d.]+)\) phi=([-\d.]+)", t)
                if m:
                    info.update(calib=int(m[1]), oPan=float(m[2]), oDist=float(m[3]),
                                hx=float(m[4]), hy=float(m[5]), phi=float(m[6]))
            elif t.startswith("DUMP state="):
                info["raw_state"] = t
                m = re.search(r"state=(\d) meas=(\d)", t)
                info["state"], info["meas"] = int(m[1]), int(m[2])
                info["conn"] = int(re.search(r"conn=(\d)", t)[1])
            elif t.startswith("DUMP points="):
                info["undo"] = int(re.search(r"undo=(\d)", t)[1])
        return info
