"""KIVisionCatLocator als Busgeraet (Phase 3).

Der Pi verhaelt sich auf dem xCom-Bus wie ein ESP32-Sensor:
  * HB alle 10 s (hbPayload, IP-Oktett + Periode)
  * settingsReport beim Start, auf settingsRequest und nach jeder Aenderung
  * poseReport auf poseRequest (bis Phase 4 ohne gueltige Pose)
  * Kommandos: cmdSetSetting (stgCamAi = KI-Erkennung, stgActive = Ruhemodus),
    cmdReboot (startet das Programm neu, systemd holt es zurueck)
  * Debug-Text auf den Text-Multicast -> VPS-Debugfenster

Settings wie beim CatCam (hwDef.h dort): stgCamAi persistiert (steckt in der
Web-Konfiguration als detect.enabled), stgActive bewusst nicht - nach einem
Neustart ist das Geraet immer aktiv, cmdReboot ist also zugleich "zurueck auf
normal".
"""

import os
import threading
import time

from xcom import Bus

HB_PERIOD_MS = 10000     # == periodeForHB der CatCam; VPS erwartet 10 s fuer Kameras
REJOIN_S = 60            # Multicast-Mitgliedschaft auffrischen (WLAN-Abrisse)


class BusNode:
    def __init__(self, defs, get_ai, set_ai, on_active):
        """get_ai()/set_ai(bool): KI-Erkennung lesen/setzen (persistiert die
        Web-Konfiguration). on_active(bool): Ruhemodus umschalten."""
        self.x = defs
        self.id = defs.KIVision
        self.bus = Bus(defs, self.id)
        self._get_ai, self._set_ai, self._on_active = get_ai, set_ai, on_active
        self.active = True
        self.supported = (1 << defs.stgCamAi) | (1 << defs.stgActive)
        self.actions = 0
        self.last_cmd = ""
        self.started = time.time()
        self._lock = threading.Lock()
        self._stop = threading.Event()

    # -- Zustand -----------------------------------------------------------
    def values(self):
        v = 0
        if self._get_ai():
            v |= 1 << self.x.stgCamAi
        if self.active:
            v |= 1 << self.x.stgActive
        return v

    def status(self):
        return {
            "id": self.id,
            "ip": self.bus.ip,
            "active": self.active,
            "ai": bool(self._get_ai()),
            "tx": self.bus.stats["tx"],
            "rx": self.bus.stats["rx"],
            "bad": self.bus.stats["bad"],
            "rx_age": (round(time.time() - self.bus.stats["last_rx"], 1)
                       if self.bus.stats["last_rx"] else None),
            "peers": len(self.bus.device_ips),
            "last_cmd": self.last_cmd,
        }

    # -- Senden ------------------------------------------------------------
    def send_hb(self):
        self.bus.broadcast(self.x.HB, self.x.pack("hbPayload", ip=self.bus.octet,
                                                   HBperiode=HB_PERIOD_MS))

    def send_settings(self):
        self.bus.broadcast(self.x.settingsReport, self.x.pack(
            "settingsPayload", supported=self.supported, values=self.values(),
            actions=self.actions))

    def send_pose(self):
        # Phase 4 (Homographie) liefert die Pose; bis dahin ehrlich "ungueltig".
        self.bus.broadcast(self.x.poseReport, self.x.pack(
            "worldPosePayload", validWorldPose=0, worldX=0, worldY=0, heading=0.0, mirror=1))

    def text(self, line):
        self.bus.text("KIVision: " + line)

    # -- Aenderungen aus der Weboberflaeche --------------------------------
    def settings_changed(self):
        self.send_settings()

    # -- Empfang -----------------------------------------------------------
    def _handle(self, m):
        x = self.x
        if m.code == x.settingsRequest:
            self.send_settings()
        elif m.code == x.poseRequest:
            self.send_pose()
        elif m.code == x.commandMsg:
            c = x.unpack("cmdPayload", m.payload)
            if c:
                self._command(m, c["cmd"], c["info"])

    def _command(self, m, cmd, info):
        x = self.x
        with self._lock:
            self.last_cmd = "%s cmd=%d info=%d von #%d" % (
                time.strftime("%H:%M:%S"), cmd, info, m.sender)
        if cmd == x.cmdSetSetting:
            idx, on = (info & 0xFFFFFFFF) >> 1, bool(info & 1)
            if idx < 16 and (self.supported >> idx) & 1:
                if idx == x.stgCamAi:
                    self._set_ai(on)
                    self.text("KI-Erkennung " + ("an" if on else "aus"))
                elif idx == x.stgActive:
                    self.active = on
                    self._on_active(on)
                    self.text("aktiv" if on else "Ruhemodus (nur HB)")
                self.send_settings()
        elif cmd == x.cmdReboot:
            self.text("Neustart per Kommando (cmdReboot) ...")
            threading.Thread(target=self._restart, daemon=True).start()

    def _restart(self):
        time.sleep(0.3)
        # Prozess beenden - systemd (Restart=always) startet ihn neu. Kamera und
        # Coral werden dabei sauber freigegeben, der Pi selbst bootet nicht.
        os._exit(0)

    # -- Lauf ----------------------------------------------------------------
    def start(self):
        self.bus.start(self._handle)
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        self.send_hb()
        self.send_settings()
        self.send_pose()
        self.text("online, IP %s, KI %s" % (self.bus.ip, "an" if self._get_ai() else "aus"))
        last_hb = last_join = time.monotonic()
        while not self._stop.wait(0.5):
            now = time.monotonic()
            if now - last_hb >= HB_PERIOD_MS / 1000.0:
                self.send_hb()
                last_hb = now
            if now - last_join >= REJOIN_S:
                self.bus.rejoin()
                last_join = now
