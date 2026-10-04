#!/usr/bin/env python3
"""KIVisionCatLocator - Kamera-Webserver (Phase 2) und Busgeraet (Phase 3).

Liefert ein MJPEG-Livebild der OV5647 im Browser, erlaubt das Verstellen der
wichtigsten Kamera-Parameter, macht Schnappschuesse (Stream-Aufloesung oder
volle 5 MP) und kann optional den Coral zuschalten, um direkt am Montageort zu
pruefen, ob eine Katze von dort ueberhaupt erkannt wird.

Seit Phase 3 ist dasselbe Programm auch Geraet 20 ("KIVision") auf dem
CatFinder-Bus (kivision_bus.py / xcom.py): HB, settingsReport, Steuerung aus
dem VPS-Tab "Steuerung" (KI an/aus, Ruhemodus, Neustart).
"""

import io
import json
import os
import socket
import subprocess
import threading
import time
from datetime import datetime
from pathlib import Path

from flask import Flask, Response, jsonify, render_template, request, send_from_directory

from kivision_motion import Motion, cv2
from picamera2 import Picamera2
from picamera2.encoders import JpegEncoder
from picamera2.outputs import FileOutput

BASE = Path(__file__).resolve().parent
HOME = Path(os.environ.get("KIVISION_HOME", Path.home() / "kivision"))
SNAP_DIR = HOME / "snapshots"
HIT_DIR = HOME / "hits"
RADAR_DIR = HOME / "radar"
MODEL_DIR = HOME / "models"
CONFIG_FILE = HOME / "web_config.json"
MAX_SNAPSHOTS = 200
# Treffer-Bilder entstehen von selbst. Der Deckel ist deshalb knapp: 60 Bilder
# sind bei einer Minute Mindestabstand rund eine Stunde Rueckschau, mehr will
# beim Justieren niemand durchblaettern - das aelteste faellt danach raus.
MAX_HITS = 60
# Radar-Bilder: ausgeloest vom Bus (catObserved/catDetected), NICHT vom Modell.
# Sie zeigen, ob die Katze, die das Radar gerade verfolgt, ueberhaupt im Bild
# war und was das Modell dort sah - auch wenn es weit unter der Schwelle lag.
# Hoechstens WIT_SHOTS Bilder je Radar-Episode, im Abstand WIT_EVERY.
MAX_RADAR = 150
SAVE_WIDTH = 1640          # Treffer-/Radar-/Bewegungs-Bilder (KI-Bild ist groesser)
# Bewegungs-Bilder: eins je bewegter Spur (mit eingezeichnetem Weg), zum
# Nachjustieren der Bewegungserkennung. Hoechstens alle MOTION_GAP Sekunden,
# sonst fuellt ein Vogelschwarm die Karte.
MOTION_DIR = HOME / "motion"
MAX_MOTION = 150
MOTION_SNAP_AGE = 1.5
MOTION_GAP = 10.0
# Radar-Ziel <-> Kamera-Fusspunkt, gleichzeitig beobachtet: Rohstoff fuer die
# Homographie (Phase 4). Eine Zeile JSON je Radar-Meldung mit Bewegung.
PAIRS_FILE = HOME / "pairs.jsonl"
PAIRS_MAX_BYTES = 20 * 1024 * 1024
WIT_THRESHOLD = 0.2       # alles ab hier einzeichnen, egal welches Label
WIT_EP_GAP = 8.0          # so lange Funkstille -> naechste Meldung ist neue Episode
WIT_EP_MAX = 120.0        # Dauermeldung (Maeher, Geist) zaehlt danach als neue Episode
WIT_SHOTS = 4
WIT_EVERY = 2.0
WIT_HOLD = 3.0            # nur fotografieren, solange das Radar noch meldet
# Zwei getrennte Stroeme (seit 2026-10-04):
#   main  = KI-Bild in "ai_size" (Standard volle 2592x1944), geht nie ins WLAN
#   lores = Livebild in "size" (YUV420, der JPEG-Encoder kodiert es direkt) und
#           zugleich Quelle der Bewegungserkennung
# Frueher war main beides, und wer das Livebild wegen des WLANs klein hielt,
# hielt damit auch die KI klein.
#
# Die Bewegungserkennung (kivision_motion.py) rechnet auf hoechstens 820x616
# (Graubild, notfalls aus dem Livebild verkleinert): die Katze hinten am Rasen
# bleibt dort ~12x22 Pixel - genug fuer einen Fleck, ~40 ms je Bild.
MOTION_SIZE = (820, 616)
# Die OV5647 liefert volle 2592x1944 nur bis ~15 fps; darueber braucht es den
# 2x2-zusammengefassten Modus (1640x1232 oder kleiner als KI-Bild).
FULL_FPS_MAX = 15
# Ausschnitt um eine bewegte Spur: so viel Mal ihre Groesse, mindestens
# CROP_MIN_REL der Bildbreite. Nachgemessen: bei 2,5x sieht das Modell
# die Katze am ehesten als Tier; bei 4x und mehr verliert sie sich wieder.
CROP_FACTOR = 2.5
CROP_MIN_REL = 0.1         # Anteil der Bildbreite (160 px bei 1640, 259 bei 2592)
CROP_MAX_TRACKS = 3
# Was das COCO-Modell im Ausschnitt sagt, in Gruppen. "tier": auf kleinen oder
# grauen Katzen nennt es gern bear/sheep/dog/cow - zaehlt als Hinweis.
CATEGORY = {"cat": "katze", "dog": "tier", "bear": "tier", "teddy bear": "tier",
            "sheep": "tier", "horse": "tier", "cow": "tier",
            "person": "person", "car": "fahrzeug", "truck": "fahrzeug",
            "bus": "fahrzeug", "motorcycle": "fahrzeug"}
SENDER_NAMES = {0: "Manager", 1: "Dome", 2: "MiniDome", 3: "CompactDome",
                17: "LidarC1", 18: "CatIdent", 19: "CatCam"}

# Aufloesungen: OV5647 ist 4:3 (2592x1944 voll).
SIZES = [(640, 480), (1280, 960), (1640, 1232), (2048, 1536), (2592, 1944)]

# Kamera-Controls, die die Weboberflaeche anbietet. type: bool|int|float|enum
CONTROL_SPEC = [
    {"name": "AeEnable", "label": "Belichtungsautomatik", "type": "bool", "group": "Belichtung"},
    {"name": "ExposureTime", "label": "Belichtungszeit (us)", "type": "int", "group": "Belichtung",
     "needs": {"AeEnable": False}},
    {"name": "AnalogueGain", "label": "Verstaerkung (Gain)", "type": "float", "group": "Belichtung",
     "needs": {"AeEnable": False}},
    {"name": "ExposureValue", "label": "Belichtungskorrektur (EV)", "type": "float", "group": "Belichtung",
     "needs": {"AeEnable": True}},
    {"name": "AwbEnable", "label": "Weissabgleich automatisch", "type": "bool", "group": "Farbe"},
    {"name": "AwbMode", "label": "Weissabgleich-Modus", "type": "enum", "group": "Farbe",
     "options": ["Auto", "Gluehlampe", "Leuchtstoff", "Neonlicht", "Tageslicht", "Bewoelkt", "Custom"],
     "needs": {"AwbEnable": True}},
    # ColourGains ist ein Paar; libcamera meldet dafuer Tupel-Grenzen, mit denen
    # die Oberflaeche nichts anfangen kann. Darum zwei eigene Regler mit fest
    # angegebenem Bereich, die _build_controls wieder zum Paar zusammensetzt.
    {"name": "ColourGainRed", "label": "Rot-Verstaerkung (manuell)", "type": "float", "group": "Farbe",
     "needs": {"AwbEnable": False}, "range": [0.5, 4.0], "default": 1.8},
    {"name": "ColourGainBlue", "label": "Blau-Verstaerkung (manuell)", "type": "float", "group": "Farbe",
     "needs": {"AwbEnable": False}, "range": [0.5, 4.0], "default": 1.6},
    {"name": "Brightness", "label": "Helligkeit", "type": "float", "group": "Bild"},
    {"name": "Contrast", "label": "Kontrast", "type": "float", "group": "Bild"},
    {"name": "Saturation", "label": "Farbsaettigung", "type": "float", "group": "Bild"},
    {"name": "Sharpness", "label": "Schaerfe", "type": "float", "group": "Bild"},
    {"name": "NoiseReductionMode", "label": "Rauschunterdrueckung", "type": "enum", "group": "Bild",
     "options": ["Aus", "Schnell", "Hohe Qualitaet", "Minimal", "ZSL"]},
]

DEFAULTS = {
    # Nachgemessen (20-MB-Download vom Pi): das WLAN traegt rund 29 Mbit/s -
    # die frueher angenommenen 2-3 Mbit/s waren in Wirklichkeit der Stream
    # selbst, nicht die Leitung. Die Bildrate darf deshalb hoch: jede Stufe der
    # Kette (Sensor, Encoder, Browser) kostet ein *Bild*, bei 8 fps also je
    # 125 ms. Mehr fps ist hier das wirksamste Mittel gegen die Verzoegerung.
    "size": [640, 480],        # Livebild (lores)
    "ai_size": [2592, 1944],   # KI-Bild (main)
    "fps": 20,
    "quality": 60,
    "hflip": False,
    "vflip": False,
    "rotate": 0,          # nur Browser-Anzeige (der ISP kann kein 90-Grad)
    "grid": True,
    "tuning": "auto",     # auto | normal | noir (NoIR-Modul: Magenta bei Tag)
    "controls": {},
    "detect": {
        "enabled": False,
        "model": "",
        "threshold": 0.4,
        # Das ganze 1640er-Bild auf 300x300 geschrumpft macht eine Katze auf
        # dem Rasen 10-15 Pixel gross - weit unter dem, was SSD-MobileNet noch
        # findet (2026-09-30: keine einzige echte Katze erkannt). 4x3 Kacheln
        # holen sie auf ~40 Pixel; die Coral braucht dafuer ~0,4 s je Runde.
        "tiles": "4x3",
        # Rahmen, die mehr als diesen Anteil ihrer Kachel bedecken, verwerfen:
        # auf dem dunklen IR-Bild haelt das Modell gern die ganze Szene fuer
        # "cat"/"horse" (beide Fehlalarme vom 2026-09-29 waren bildfuellend).
        # Eine echte Katze fuellt selbst ganz vorn keine halbe Kachel.
        "max_area": 0.4,
        "interval": 0.3,
        "cat_only": True,
        # Beim Justieren laeuft niemand mit einer Katze durchs Bild: die Kamera
        # muss selbst festhalten, was sie gesehen hat. Ein Bild pro Minute
        # reicht dafuer und haelt Speicherkarte wie Galerie ueberschaubar.
        #
        # Eigener Filter, nicht der der Anzeige: nachts meldet das COCO-Modell
        # auf dem kontrastarmen Infrarotbild gern ein bildfuellendes "horse".
        # Wer sich das im Livebild ansehen will, soll sich davon nicht die
        # Galerie zumuellen lassen. off | cat | animal | all
        #
        # "animal": auf dem Graubild verwechselt COCO eine Katze leicht mit
        # dog/bear/sheep - zum Sammeln echter Katzenbilder zaehlt jedes Tier.
        "snap_mode": "animal",
        "snap_gap": 60,
    },
}


# COCO-Tierklassen, die eine Katze auf dem Rasen sein koennten (Vogel/Giraffe
# & Co. bewusst nicht). Dient nur dem Festhalten, nicht der Anzeige.
ANIMALS = ("cat", "katze", "dog", "bear", "teddy bear", "sheep", "horse", "cow")


def load_config():
    cfg = json.loads(json.dumps(DEFAULTS))
    try:
        stored = json.loads(CONFIG_FILE.read_text())
        for key, value in stored.items():
            if key in cfg and isinstance(cfg[key], dict) and isinstance(value, dict):
                cfg[key].update(value)
            elif key in cfg:
                cfg[key] = value
    except (OSError, ValueError):
        pass
    return cfg


def save_config(cfg):
    try:
        CONFIG_FILE.parent.mkdir(parents=True, exist_ok=True)
        CONFIG_FILE.write_text(json.dumps(cfg, indent=2))
    except OSError as exc:
        print("[web] Konfiguration nicht speicherbar: %s" % exc)


class StreamingOutput(io.BufferedIOBase):
    """Nimmt die JPEGs des Encoders entgegen und weckt die wartenden Clients.

    Gepuffert wird immer nur das *neueste* Bild. Ein Client, der nicht
    hinterherkommt (schwaches WLAN), ueberspringt damit Bilder, statt einen
    Rueckstau aufzubauen - Ruckeln ist besser als Verzoegerung.
    """

    def __init__(self):
        self.frame = None
        self.condition = threading.Condition()
        self.count = 0
        self.last_times = []
        self.last_bytes = []

    def write(self, buf):
        now = time.monotonic()
        with self.condition:
            self.frame = buf
            self.count += 1
            self.last_times.append(now)
            self.last_bytes.append(len(buf))
            if len(self.last_times) > 30:
                self.last_times.pop(0)
                self.last_bytes.pop(0)
            self.condition.notify_all()

    def _span(self):
        if len(self.last_times) < 2:
            return 0.0
        return self.last_times[-1] - self.last_times[0]

    def fps(self):
        with self.condition:
            span, n = self._span(), len(self.last_times)
        return round((n - 1) / span, 1) if span > 0 else 0.0

    def mbit(self):
        """Wieviel der Stream gerade erzeugen wuerde, in Mbit/s."""
        with self.condition:
            span, total = self._span(), sum(self.last_bytes[1:])
        return round(total * 8 / span / 1e6, 1) if span > 0 else 0.0

    def wait(self, last_seq, timeout=5.0):
        """Liefert (Bild, Nummer). Ist schon ein neueres da, sofort."""
        with self.condition:
            if self.count == last_seq:
                self.condition.wait(timeout)
            return self.frame, self.count


class Camera:
    """Haelt die Picamera2 und kapselt Start/Stop/Umkonfigurieren."""

    IDLE_STOP = 5.0          # Sekunden ohne Zuschauer, dann Encoder aus

    # Ein Modul ohne IR-Sperrfilter (NoIR) sieht bei Tageslicht magenta: das
    # Infrarot faellt vor allem auf die roten Pixel. Die normale Tuning-Datei
    # zwingt den Weissabgleich auf die Farbtemperatur-Kurve und kann das nicht
    # ausgleichen; die NoIR-Datei laesst ihm die noetige Freiheit.
    TUNINGS = {"auto": None, "normal": "ov5647.json", "noir": "ov5647_noir.json"}

    def __init__(self, cfg):
        self.cfg = cfg
        self.lock = threading.RLock()
        self.output = StreamingOutput()
        self.picam = self._open()
        self.limits = self._read_limits()
        self.running = False
        self.encoder = None
        self.viewers = 0
        self.idle_timer = None
        self.start()

    # -- intern ----------------------------------------------------------
    def _open(self):
        name = self.TUNINGS.get(self.cfg.get("tuning", "auto"))
        if not name:
            return Picamera2()
        try:
            cam = Picamera2(tuning=Picamera2.load_tuning_file(name))
            print("[web] Tuning-Datei: %s" % name)
            return cam
        except Exception as exc:
            print("[web] Tuning %s nicht ladbar (%s) - Standard" % (name, exc))
            return Picamera2()

    def set_tuning(self, name):
        """Tuning wird beim Oeffnen gelesen - also Kamera neu aufmachen."""
        with self.lock:
            if name not in self.TUNINGS or name == self.cfg.get("tuning"):
                return
            self.cfg["tuning"] = name
            self.stop()
            try:
                self.picam.close()
            except Exception:
                pass
            self.picam = self._open()
            self.limits = self._read_limits()
            self.start()

    def _read_limits(self):
        limits = {}
        for name, values in self.picam.camera_controls.items():
            try:
                low, high, default = values
            except (TypeError, ValueError):
                continue
            if isinstance(low, (list, tuple)) or isinstance(high, (list, tuple)):
                continue
            limits[name] = {"min": low, "max": high, "default": default}
        for spec in CONTROL_SPEC:
            if "range" in spec:                      # eigene Regler (s. o.)
                limits[spec["name"]] = {"min": spec["range"][0],
                                        "max": spec["range"][1],
                                        "default": spec["default"]}
        return limits

    def fps_max(self):
        ai = self.cfg.get("ai_size") or SIZES[-1]
        return FULL_FPS_MAX if ai[0] > 1640 else 30

    def fps(self):
        return max(1, min(int(self.cfg["fps"]), self.fps_max()))

    def _build_controls(self):
        ctrl = {}
        fps = self.fps()
        frame_us = int(1000000 / fps)
        ctrl["FrameDurationLimits"] = (frame_us, frame_us)
        for spec in CONTROL_SPEC:
            name = spec["name"]
            if name not in self.cfg["controls"] or name not in self.limits:
                continue
            value = self.cfg["controls"][name]
            if spec["type"] == "bool":
                ctrl[name] = bool(value)
            elif spec["type"] in ("int", "enum"):
                ctrl[name] = int(value)
            else:
                ctrl[name] = float(value)
        # Bei aktiver Automatik wuerde eine feste Zeit nur ignoriert - und
        # libcamera meckert, wenn beides zusammen kommt.
        if ctrl.get("AeEnable", True):
            ctrl.pop("ExposureTime", None)
            ctrl.pop("AnalogueGain", None)
        else:
            ctrl.pop("ExposureValue", None)
        red = ctrl.pop("ColourGainRed", None)
        blue = ctrl.pop("ColourGainBlue", None)
        if ctrl.get("AwbEnable", True):
            pass                                     # Automatik macht die Gains
        else:
            ctrl.pop("AwbMode", None)
            if red is not None and blue is not None:
                ctrl["ColourGains"] = (float(red), float(blue))
        return ctrl

    # -- oeffentlich ------------------------------------------------------
    def start(self):
        with self.lock:
            if self.running:
                return
            size = tuple(self.cfg.get("ai_size") or SIZES[-1])
            # Livebild = lores; darf nicht groesser als das KI-Bild sein.
            want = tuple(self.cfg["size"])
            lo = (min(want[0], size[0]), min(want[1], size[1]))
            self.lores_size = lo
            config = self.picam.create_video_configuration(
                main={"size": size, "format": "RGB888"},
                lores={"size": lo, "format": "YUV420"},
                transform=self._transform(),
                controls=self._build_controls(),
                buffer_count=3,   # weniger Puffer = weniger Verzoegerung
            )
            self.picam.configure(config)
            self.picam.start()
            self.running = True
            if self.viewers > 0:
                self._encoder_start()
            print("[web] Kamera laeuft: KI %dx%d, Livebild %dx%d @ %s fps"
                  % (size[0], size[1], lo[0], lo[1], self.fps()))

    # -- Encoder laeuft nur, solange jemand zuschaut ----------------------
    def _encoder_start(self):
        with self.lock:
            if self.encoder is not None or not self.running:
                return
            self.encoder = JpegEncoder(q=int(self.cfg["quality"]))
            self.picam.start_encoder(self.encoder, FileOutput(self.output), name="lores")

    def _encoder_stop(self):
        with self.lock:
            if self.encoder is None:
                return
            try:
                self.picam.stop_encoder()
            except Exception:
                pass
            self.encoder = None

    def add_viewer(self):
        with self.lock:
            self.viewers += 1
            if self.idle_timer is not None:
                self.idle_timer.cancel()
                self.idle_timer = None
            self._encoder_start()

    def remove_viewer(self):
        with self.lock:
            self.viewers = max(0, self.viewers - 1)
            if self.viewers == 0 and self.idle_timer is None:
                self.idle_timer = threading.Timer(self.IDLE_STOP, self._idle_stop)
                self.idle_timer.daemon = True
                self.idle_timer.start()

    def _idle_stop(self):
        with self.lock:
            self.idle_timer = None
            if self.viewers == 0:
                self._encoder_stop()
                print("[web] kein Zuschauer - Encoder aus")

    def _transform(self):
        from libcamera import Transform
        return Transform(hflip=1 if self.cfg["hflip"] else 0,
                         vflip=1 if self.cfg["vflip"] else 0)

    def stop(self):
        with self.lock:
            if not self.running:
                return
            self._encoder_stop()
            self.picam.stop()
            self.running = False

    def restart(self):
        with self.lock:
            self.stop()
            self.start()

    def apply_controls(self):
        with self.lock:
            if not self.running:
                return
            try:
                self.picam.set_controls(self._build_controls())
            except Exception as exc:
                print("[web] Controls abgelehnt: %s" % exc)

    def metadata(self):
        try:
            meta = dict(self.picam.capture_metadata())
        except Exception:
            return {}
        stamp = meta.get("SensorTimestamp")
        if stamp:
            # Wie alt ist das eben fertig gewordene Bild, wenn es hier ankommt?
            # Das ist die Verzoegerung Sensor -> Anwendung, ganz ohne WLAN und
            # ohne Browser - und damit die Antwort auf "haengt es am WLAN?".
            meta["_lag_ms"] = round((time.monotonic_ns() - stamp) / 1e6, 1)
        return meta

    def capture_array(self):
        """Aktuelles Bild als RGB-Array (picamera2 liefert RGB888 als BGR)."""
        with self.lock:
            if not self.running:
                return None
            frame = self.picam.capture_array("main")
        return frame[:, :, ::-1]

    def capture_pair(self):
        """Hauptbild (RGB) und Graubild des Zweitstroms aus *derselben*
        Aufnahme - so passen die Bewegungsrahmen exakt aufs grosse Bild."""
        with self.lock:
            if not self.running:
                return None, None
            req = self.picam.capture_request()
            try:
                main = req.make_array("main")
                lores = req.make_array("lores")
            finally:
                req.release()
        w, h = self.lores_size
        gray = lores[:h, :w]
        if w > MOTION_SIZE[0] and cv2 is not None:
            gray = cv2.resize(gray, MOTION_SIZE, interpolation=cv2.INTER_AREA)
        return main[:, :, ::-1], gray

    def snapshot(self, frame, full_res=False):
        """Schnappschuss aus dem KI-Bild (kommt von der Bildpumpe): "voll" in
        KI-Aufloesung, sonst auf Livebild-Groesse verkleinert. Der Strom muss
        dafuer nicht mehr umgeschaltet werden."""
        from PIL import Image
        if frame is None:
            raise RuntimeError("noch kein Kamerabild")
        SNAP_DIR.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        tag = "voll" if full_res else "stream"
        path = SNAP_DIR / ("%s_%s.jpg" % (stamp, tag))
        img = Image.fromarray(frame)
        if not full_res:
            img = img.resize(tuple(self.lores_size), Image.BILINEAR, reducing_gap=2.0)
        img.save(path, "JPEG", quality=90)
        prune_snapshots()
        return path


class FramePump:
    """Holt jedes Kamerabild genau einmal ab, fuettert die Bewegungserkennung
    mit dem Graubild und haelt das neueste grosse Bild samt Spuren bereit.

    Der Detector wartet hier auf das naechste Bild, statt selbst zu holen:
    so gehoeren Bewegungsrahmen und Bild sicher zur selben Aufnahme, und die
    Kamera wird nicht von zwei Threads gleichzeitig angezapft.
    """

    def __init__(self, camera, motion):
        self.camera = camera
        self.motion = motion
        self.cond = threading.Condition()
        self.frame = None
        self.tracks = []
        self.seq = 0
        self.error = ""
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        while True:
            try:
                frame, gray = self.camera.capture_pair()
                if frame is None:
                    time.sleep(0.2)
                    continue
                tracks = self.motion.update(gray) if self.motion.enabled else []
                self._maybe_snap(frame)
                with self.cond:
                    self.frame, self.tracks = frame, tracks
                    self.seq += 1
                    self.error = ""
                    self.cond.notify_all()
            except Exception as exc:                          # noqa: BLE001
                self.error = "%s: %s" % (type(exc).__name__, exc)
                print("[web] Bildpumpe: %s" % self.error)
                time.sleep(1.0)

    last_snap = 0.0

    def _maybe_snap(self, frame):
        new = self.motion.claim_snap(MOTION_SNAP_AGE)
        now = time.monotonic()
        if not new or now - self.last_snap < MOTION_GAP:
            return
        self.last_snap = now
        lux = (self.camera.metadata().get("Lux") or 0)
        note = "Bewegung %s  Lux %d" % (
            ", ".join("B%d %.1fs" % (t["id"], t["age"]) for t in new), round(lux))
        # Speichern dauert ~0,1 s - nicht in der Pumpe, sonst fehlt ein Bild.
        threading.Thread(target=self._snap, args=(frame, new, note), daemon=True).start()

    @staticmethod
    def _snap(frame, tracks, note):
        try:
            path = save_hit(frame, [], note, MOTION_DIR, MAX_MOTION, name_score=0.0,
                            tracks=tracks)
            print("[web] Bewegungs-Bild: %s" % path.name)
        except Exception as exc:                              # noqa: BLE001
            print("[web] Bewegungs-Bild fehlgeschlagen: %s" % exc)

    def wait(self, last_seq, timeout=2.0):
        """(Bild, Spuren, Nummer) - das naechste nach last_seq."""
        with self.cond:
            if self.seq == last_seq:
                self.cond.wait(timeout)
            return self.frame, self.tracks, self.seq

    def latest(self):
        with self.cond:
            return self.frame, self.tracks


class Detector:
    """Optionaler Coral-Check: laeuft nur, solange er eingeschaltet ist."""

    def __init__(self, camera, cfg):
        self.camera = camera
        self.cfg = cfg
        self.lock = threading.Lock()
        self.thread = None
        self.stop_event = threading.Event()
        self.result = {"boxes": [], "ms": 0.0, "ts": 0.0, "tiles": []}
        self.error = ""
        self.hit_at = None          # monotonic der letzten Aufnahme
        self.hit_error = ""         # bleibt stehen, bis es wieder klappt
        self.hit_count = 0          # zaehlt hoch: die Oberflaeche merkt daran,
        self.hit_last = ""          # dass sie die Treffer-Galerie neu laden muss
        # Radar-Episode (siehe MAX_RADAR), Zeiten in time.monotonic()
        self.ep_start = None
        self.ep_last = 0.0
        self.ep_shots = 0
        self.ep_next = 0.0
        self.ep_force = False
        self.ep_info = ""
        self.ep_busy = False
        self.radar_count = 0
        self.model_path = None
        self.interpreter = None
        self.labels = {}
        self.input_size = (300, 300)
        self.motion = None          # kivision_motion.Motion (vom FramePump)
        self.pump = None            # FramePump: liefert Bild + Bewegungsspuren
        self.loops = 0

    # -- Modell ----------------------------------------------------------
    @staticmethod
    def available_models():
        if not MODEL_DIR.is_dir():
            return []
        return sorted(p.name for p in MODEL_DIR.glob("*.tflite"))

    def _load_labels(self):
        labels = {}
        candidates = sorted(MODEL_DIR.glob("*label*.txt")) + sorted(MODEL_DIR.glob("*.txt"))
        for cand in candidates:
            try:
                for line in cand.read_text().splitlines():
                    line = line.strip()
                    if not line:
                        continue
                    parts = line.split(None, 1)
                    if len(parts) == 2 and parts[0].isdigit():
                        labels[int(parts[0])] = parts[1]
                    else:
                        labels[len(labels)] = line
            except OSError:
                continue
            if labels:
                break
        return labels

    def _ensure_model(self):
        name = self.cfg["detect"].get("model") or ""
        models = self.available_models()
        if not models:
            raise RuntimeError("Keine .tflite-Modelle in %s" % MODEL_DIR)
        if name not in models:
            name = models[0]
            self.cfg["detect"]["model"] = name
        path = MODEL_DIR / name
        if self.model_path == path and self.interpreter is not None:
            return
        from pycoral.utils.edgetpu import make_interpreter
        self.interpreter = make_interpreter(str(path))
        self.interpreter.allocate_tensors()
        shape = self.interpreter.get_input_details()[0]["shape"]
        self.input_size = (int(shape[2]), int(shape[1]))
        self.labels = self._load_labels()
        self.model_path = path
        print("[web] Modell geladen: %s (%dx%d)" % (name, self.input_size[0], self.input_size[1]))

    # -- Kacheln ---------------------------------------------------------
    @staticmethod
    def _tile_boxes(spec, width, height):
        try:
            cols, rows = (int(v) for v in spec.lower().split("x"))
        except ValueError:
            cols, rows = 1, 1
        cols = max(1, min(4, cols))
        rows = max(1, min(4, rows))
        if cols == 1 and rows == 1:
            return [(0, 0, width, height)]
        overlap = 0.15
        tw = width / (cols - overlap * (cols - 1))
        th = height / (rows - overlap * (rows - 1))
        tiles = []
        for r in range(rows):
            for c in range(cols):
                x0 = int(c * tw * (1 - overlap))
                y0 = int(r * th * (1 - overlap))
                tiles.append((x0, y0, min(int(tw), width - x0), min(int(th), height - y0)))
        return tiles

    @staticmethod
    def _resize(tile, size):
        try:
            from PIL import Image
            # reducing_gap: erst ganzzahlig verkleinern, dann fein skalieren -
            # beim 1640er-Vollbild ein Vielfaches schneller, kaum schlechter.
            return Image.fromarray(tile).resize(size, Image.BILINEAR, reducing_gap=2.0)
        except ImportError:
            import numpy as np
            ys = (np.linspace(0, tile.shape[0] - 1, size[1])).astype(int)
            xs = (np.linspace(0, tile.shape[1] - 1, size[0])).astype(int)
            return tile[ys][:, xs]

    def _detect_region(self, frame, tx, ty, tw, th, floor):
        """Ein Rechteck des grossen Bildes durchs Modell; Rahmen normiert aufs
        ganze Bild. Kacheln und Bewegungs-Ausschnitte laufen beide hierueber."""
        from pycoral.adapters import common, detect
        height, width = frame.shape[:2]
        crop = frame[ty:ty + th, tx:tx + tw]
        if crop.size == 0:
            return []
        common.set_input(self.interpreter, self._resize(crop, self.input_size))
        self.interpreter.invoke()
        # get_objects rechnet intern sx = Eingangsbreite / image_scale_x.
        # Der frueher uebergebene Massstab (Kachel/Tensor) war damit gerade
        # verkehrt herum und quetschte alle Rahmen in eine Ecke. Da die
        # Kachel den Tensor vollstaendig ausfuellt, ist image_scale hier
        # (1,1): die Rahmen kommen in Tensor-Pixeln, und wir rechnen sie
        # selbst auf die Kachel hoch und schieben sie an ihren Platz.
        sx = tw / self.input_size[0]
        sy = th / self.input_size[1]
        max_area = float(self.cfg["detect"].get("max_area", 0.4))
        tensor_area = float(self.input_size[0] * self.input_size[1])
        out = []
        for obj in detect.get_objects(self.interpreter, floor):
            label = self.labels.get(obj.id, str(obj.id))
            # Die Koordinaten sind Tensor-Pixel, der Tensor ist die Kachel.
            if (obj.bbox.width * obj.bbox.height) / tensor_area > max_area:
                continue
            x0 = clamp01((tx + obj.bbox.xmin * sx) / width)
            y0 = clamp01((ty + obj.bbox.ymin * sy) / height)
            x1 = clamp01((tx + obj.bbox.xmax * sx) / width)
            y1 = clamp01((ty + obj.bbox.ymax * sy) / height)
            if x1 <= x0 or y1 <= y0:
                continue
            out.append({"x": round(x0, 4), "y": round(y0, 4),
                        "w": round(x1 - x0, 4), "h": round(y1 - y0, 4),
                        "score": round(float(obj.score), 3), "label": label})
        return out

    @staticmethod
    def _crop_rect(track, width, height):
        """Quadratischer Ausschnitt (Pixel) um eine Bewegungsspur."""
        side = max(CROP_MIN_REL * width, CROP_FACTOR * max(track["w"] * width, track["h"] * height))
        side = int(min(side, width, height))
        cx = (track["x"] + track["w"] / 2) * width
        cy = (track["y"] + track["h"] / 2) * height
        x0 = int(min(max(0, cx - side / 2), width - side))
        y0 = int(min(max(0, cy - side / 2), height - side))
        return x0, y0, side, side

    def _infer(self, frame, full=True, tracks=()):
        height, width = frame.shape[:2]
        tiles = (self._tile_boxes(self.cfg["detect"].get("tiles", "1x1"), width, height)
                 if full else [])
        threshold = float(self.cfg["detect"].get("threshold", 0.4))
        cat_only = bool(self.cfg["detect"].get("cat_only", True))
        boxes = []
        raw = []                    # alles ab WIT_THRESHOLD, fuer Radar-Bilder
        floor = min(threshold, WIT_THRESHOLD)
        started = time.monotonic()

        def keep(box):
            raw.append(box)
            if box["score"] >= threshold and not (cat_only and "cat" not in box["label"].lower()):
                boxes.append(box)

        for (tx, ty, tw, th) in tiles:
            for box in self._detect_region(frame, tx, ty, tw, th, floor):
                keep(box)
        crops = []
        for tr in list(tracks)[:CROP_MAX_TRACKS]:
            rect = self._crop_rect(tr, width, height)
            crops.append(rect)
            # Wozu gehoert ein Rahmen im Ausschnitt? Zur Spur, wenn er ihren
            # Mittelpunkt (grosszuegig) enthaelt - der Ausschnitt zeigt ja
            # auch Umgebung, z. B. den Maeher neben der Katze.
            cx, cy = tr["x"] + tr["w"] / 2, tr["y"] + tr["h"] / 2
            for box in self._detect_region(frame, *rect, floor):
                mx, my = box["w"] * 0.25 + tr["w"] / 2, box["h"] * 0.25 + tr["h"] / 2
                if not (box["x"] - mx <= cx <= box["x"] + box["w"] + mx
                        and box["y"] - my <= cy <= box["y"] + box["h"] + my):
                    continue
                box["track"] = tr["id"]
                keep(box)
                cat = CATEGORY.get(box["label"].lower())
                if cat and self.motion is not None:
                    self.motion.classify(tr["id"], cat, box["score"], box["label"])
        ms = round((time.monotonic() - started) * 1000, 1)
        norm_tiles = [{"x": t[0] / width, "y": t[1] / height,
                       "w": t[2] / width, "h": t[3] / height} for t in tiles + crops]
        raw.sort(key=lambda b: b["score"], reverse=True)
        return {"boxes": boxes, "raw": raw[:12], "ms": ms, "ts": time.time(),
                "tiles": norm_tiles}

    # -- Treffer festhalten ------------------------------------------------
    @staticmethod
    def _is_cat(label):
        low = str(label).lower()
        return "cat" in low or "katze" in low

    @staticmethod
    def _is_animal(label):
        return str(label).lower() in ANIMALS

    def _maybe_hit(self, frame, result):
        """Speichert hoechstens alle snap_gap Sekunden ein Bild eines Treffers.

        Der Mindestabstand zaehlt ab der letzten Aufnahme, nicht ab dem letzten
        Treffer: eine Katze, die zehn Minuten im Bild sitzt, liefert zehn
        Bilder, keine tausend. Gezeichnet wird nur, was ausgeloest hat.
        """
        det = self.cfg["detect"]
        mode = det.get("snap_mode", "cat")
        if mode == "off" or not result["boxes"]:
            return
        keep = {"all": lambda label: True, "animal": self._is_animal}.get(mode, self._is_cat)
        boxes = [b for b in result["boxes"] if keep(b["label"])]
        if not boxes:
            return
        gap = max(5.0, float(det.get("snap_gap", 60)))
        now = time.monotonic()
        if self.hit_at is not None and now - self.hit_at < gap:
            return
        meta = self.camera.metadata()
        note = "Lux %s  Bel. %s ms  Gain %s" % (
            round(meta.get("Lux") or 0, 1),
            round((meta.get("ExposureTime") or 0) / 1000.0, 1),
            round(meta.get("AnalogueGain") or 0, 1))
        try:
            path = save_hit(frame, boxes, note)
        except Exception as exc:
            # Ein missgluecktes Treffer-Bild darf die Erkennung nicht anhalten.
            # Die Meldung bleibt aber stehen - sonst uebermalt sie der naechste
            # geglueckte Durchlauf, und niemand erfaehrt vom fehlenden Bild.
            with self.lock:
                self.hit_error = "Treffer-Bild: %s" % exc
                self.hit_at = now
            print("[web] Treffer-Bild fehlgeschlagen: %s" % exc)
            return
        with self.lock:
            self.hit_at = now
            self.hit_count += 1
            self.hit_last = path.name
            self.hit_error = ""
        print("[web] Treffer festgehalten: %s (%d Rahmen)" % (path.name, len(boxes)))

    # -- Radar-Bilder ------------------------------------------------------
    def radar_event(self, info, force=False):
        """Vom Bus: ein Sensor meldet ein Ziel (catObserved) bzw. das Modell
        hat eine Katze bestaetigt (catDetected, force=True -> sofort ein Bild).

        Laeuft die KI, macht der Erkennungs-Thread das Bild beim naechsten
        Durchlauf (mit allen Rahmen ab WIT_THRESHOLD). Ist sie aus, wird das
        nackte Bild direkt abgelegt - es zeigt dann wenigstens, ob die Katze
        im Bild war.
        """
        now = time.monotonic()
        with self.lock:
            if (self.ep_start is None or now - self.ep_last > WIT_EP_GAP
                    or now - self.ep_start > WIT_EP_MAX):
                self.ep_start = now
                self.ep_shots = 0
                self.ep_next = now
            self.ep_last = now
            self.ep_info = info
            if force:
                self.ep_force = True
            running = self.thread is not None and self.thread.is_alive()
            due = not running and not self.ep_busy and self._witness_due(now)
            if due:
                self.ep_busy = True
        if due:
            threading.Thread(target=self._witness_plain, daemon=True).start()

    def _witness_due(self, now):
        """Unter self.lock: soll jetzt ein Radar-Bild entstehen?"""
        if self.ep_start is None or now - self.ep_last > WIT_HOLD:
            return False
        if self.ep_force:
            return True
        return self.ep_shots < WIT_SHOTS and now >= self.ep_next

    def _witness_take(self, now):
        """Unter self.lock: Episode weiterzaehlen, Text der Fusszeile liefern."""
        self.ep_force = False
        self.ep_shots += 1
        self.ep_next = now + WIT_EVERY
        return self.ep_info, self.ep_shots

    def _witness_plain(self):
        try:
            # Naechstes Bild der Pumpe abwarten - das neueste kann schon eine
            # Weile alt sein, wenn die Meldung genau dazwischen kam.
            frame, tracks, _seq = self.pump.wait(self.pump.seq)
            if frame is not None:
                with self.lock:
                    info, shot = self._witness_take(time.monotonic())
                self._save_witness(frame, None, info, shot, tracks)
        finally:
            with self.lock:
                self.ep_busy = False

    def _maybe_witness(self, frame, raw, tracks=()):
        now = time.monotonic()
        with self.lock:
            if not self._witness_due(now):
                return
            info, shot = self._witness_take(now)
        self._save_witness(frame, raw, info, shot, tracks)

    def _save_witness(self, frame, raw, info, shot, tracks=()):
        cats = [b["score"] for b in (raw or []) if self._is_cat(b["label"])]
        best = max(cats) if cats else 0.0
        if raw is None:
            model = "KI aus"
        elif raw:
            model = "bestes: %s %d%%" % (raw[0]["label"], round(raw[0]["score"] * 100))
        else:
            model = "Modell: nichts ab %d%%" % round(WIT_THRESHOLD * 100)
        note = "%s  Bild %d  %s" % (info, shot, model)
        if tracks:
            note += "  Bew. %d" % len(tracks)
        try:
            # Der Name traegt die beste KATZEN-Sicherheit (0 = keine Katze),
            # nicht die des staerksten Rahmens - danach sortiert man hinterher.
            path = save_hit(frame, raw or [], note, RADAR_DIR, MAX_RADAR, name_score=best,
                            tracks=tracks)
        except Exception as exc:                              # noqa: BLE001
            print("[web] Radar-Bild fehlgeschlagen: %s" % exc)
            return
        with self.lock:
            self.radar_count += 1
        print("[web] Radar-Bild: %s (%s)" % (path.name, note))

    # -- Thread ----------------------------------------------------------
    def _run(self):
        seq = -1
        while not self.stop_event.is_set():
            pause = max(0.05, float(self.cfg["detect"].get("interval", 0.3)))
            try:
                self._ensure_model()
                frame, tracks, seq = self.pump.wait(seq)
                if frame is None:
                    time.sleep(0.2)
                    continue
                # Bewegt sich etwas, zaehlt Tempo: nur die Ausschnitte um die
                # Spuren (je ~25 ms), das ganze Kachelraster (~0,4 s) nur noch
                # jede vierte Runde. Sonst wie bisher das ganze Bild.
                self.loops += 1
                full = not tracks or self.loops % 4 == 0
                result = self._infer(frame, full, tracks)
                raw = result.pop("raw", [])
                result["motion"] = self.motion.moving() if self.motion else []
                with self.lock:
                    self.result = result
                    self.error = ""
                self._maybe_hit(frame, result)
                self._maybe_witness(frame, raw, result["motion"])
                if tracks:
                    pause = 0.0             # gleich aufs naechste Bild warten
            except Exception as exc:
                with self.lock:
                    self.error = "%s: %s" % (type(exc).__name__, exc)
                print("[web] Erkennung fehlgeschlagen: %s" % exc)
                self.stop_event.wait(2.0)
            if pause:
                self.stop_event.wait(pause)

    def set_enabled(self, enabled):
        if enabled and (self.thread is None or not self.thread.is_alive()):
            self.stop_event.clear()
            self.thread = threading.Thread(target=self._run, daemon=True)
            self.thread.start()
        elif not enabled and self.thread is not None:
            self.stop_event.set()
            self.thread = None
            with self.lock:
                self.result = {"boxes": [], "ms": 0.0, "ts": 0.0, "tiles": []}

    def snapshot_state(self):
        gap = max(5.0, float(self.cfg["detect"].get("snap_gap", 60)))
        with self.lock:
            data = dict(self.result)
            data["error"] = self.error or self.hit_error
            data["hits"] = self.hit_count
            data["hit_last"] = self.hit_last
            data["radar"] = self.radar_count
            data["motion_status"] = self.motion.status() if self.motion else None
            # Wieviel Sperrzeit noch laeuft - damit die Oberflaeche erklaeren
            # kann, warum ein sichtbarer Rahmen gerade kein Bild ergibt.
            data["hit_wait"] = (0 if self.hit_at is None
                                else max(0, round(gap - (time.monotonic() - self.hit_at))))
        data["running"] = self.thread is not None and self.thread.is_alive()
        return data


def clamp01(value):
    return max(0.0, min(1.0, value))


def prune_dir(directory, keep):
    files = sorted(directory.glob("*.jpg"), key=lambda p: p.stat().st_mtime, reverse=True)
    for old in files[keep:]:
        try:
            old.unlink()
        except OSError:
            pass


def prune_snapshots():
    prune_dir(SNAP_DIR, MAX_SNAPSHOTS)


def _font(px):
    from PIL import ImageFont
    try:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", px)
    except OSError:
        pass
    try:
        return ImageFont.load_default(size=px)
    except TypeError:                       # aeltere Pillow: nur Bitmap-Font
        return ImageFont.load_default()


def save_hit(frame, boxes, note, directory=None, keep=MAX_HITS, name_score=None,
             tracks=()):
    """Legt das Bild ab, in dem gerade etwas erkannt wurde - mit Rahmen drin.

    Der Rahmen wird fest eingezeichnet, nicht als Overlay nachgereicht: beim
    Justieren zaehlt, *wo* im Bild die Katze stand und wie gross sie dort war.
    Die Fusszeile haelt Uhrzeit und Lichtverhaeltnisse fest - daran sieht man
    hinterher, ob der Treffer aus der Tag- oder der Infrarot-Lage stammt.
    """
    from PIL import Image, ImageDraw

    img = Image.fromarray(frame)
    if img.size[0] > SAVE_WIDTH:
        # Rahmen sind normiert - verkleinern vor dem Zeichnen ist verlustfrei
        # fuer die Beschriftung und haelt die Galerien im WLAN flink.
        img = img.resize((SAVE_WIDTH, round(img.size[1] * SAVE_WIDTH / img.size[0])),
                         Image.BILINEAR, reducing_gap=2.0)
    width, height = img.size
    draw = ImageDraw.Draw(img)
    line = max(2, round(width / 400))
    font = _font(max(12, round(width / 45)))
    green, orange, dark = (90, 235, 130), (255, 170, 60), (8, 24, 14)
    best = 0.0
    # Bewegungsspuren hellblau, mit etwas Abstand um den Fleck, damit die
    # Katze selbst sichtbar bleibt; darunter Nummer und was die KI dort sah.
    cyan = (80, 200, 255)
    for tr in tracks:
        pad = 0.006
        x0, y0 = (tr["x"] - pad) * width, (tr["y"] - pad) * height
        x1 = (tr["x"] + tr["w"] + pad) * width
        y1 = (tr["y"] + tr["h"] + pad) * height
        draw.rectangle([x0, y0, x1, y1], outline=cyan, width=max(1, line - 1))
        pts = [(p[0] * width, p[1] * height) for p in tr.get("path") or []]
        if len(pts) > 1:
            draw.line(pts, fill=cyan, width=max(1, line - 1))
        tag = ("B%d %s" % (tr["id"], tr.get("best") or "")).strip()
        draw.text((min(x0, width - draw.textlength(tag, font=font) - 4), y1 + 2),
                  tag, font=font, fill=cyan)
    # Schwaechste zuerst zeichnen, damit die staerksten obenauf liegen.
    for box in sorted(boxes, key=lambda b: b["score"]):
        x0, y0 = box["x"] * width, box["y"] * height
        x1, y1 = (box["x"] + box["w"]) * width, (box["y"] + box["h"]) * height
        # Katze gruen, alles andere orange (nur in Radar-/Tier-Bildern sichtbar)
        col = green if Detector._is_cat(box["label"]) else orange
        draw.rectangle([x0, y0, x1, y1], outline=col, width=line)
        best = max(best, box["score"])
        tag = "%s %d%%" % (box["label"], round(box["score"] * 100))
        tw = draw.textlength(tag, font=font)
        th = font.size + 6 if hasattr(font, "size") else 18
        ty = y0 - th - line if y0 - th - line >= 0 else y0 + line
        tx = min(max(0.0, x0), max(0.0, width - tw - 8))
        draw.rectangle([tx, ty, tx + tw + 8, ty + th], fill=dark, outline=col, width=1)
        draw.text((tx + 4, ty + 2), tag, font=font, fill=col)
    stamp = datetime.now()
    foot = stamp.strftime("%d.%m. %H:%M:%S") + "  " + note
    fh = (font.size if hasattr(font, "size") else 14) + 8
    draw.rectangle([0, height - fh, width, height], fill=(0, 0, 0))
    draw.text((6, height - fh + 3), foot, font=font, fill=(220, 230, 240))

    directory = directory or HIT_DIR
    directory.mkdir(parents=True, exist_ok=True)
    if name_score is not None:
        best = name_score
    path = directory / ("%s_%02d.jpg" % (stamp.strftime("%Y%m%d_%H%M%S"), round(best * 100)))
    img.save(path, "JPEG", quality=80)
    prune_dir(directory, keep)
    return path


def system_info():
    info = {}
    try:
        raw = Path("/sys/class/thermal/thermal_zone0/temp").read_text().strip()
        info["temp"] = round(int(raw) / 1000.0, 1)
    except (OSError, ValueError):
        info["temp"] = None
    try:
        out = subprocess.run(["vcgencmd", "get_throttled"], capture_output=True,
                             text=True, timeout=2).stdout.strip()
        info["throttled"] = out.split("=")[-1]
    except Exception:
        info["throttled"] = ""
    try:
        info["load"] = round(os.getloadavg()[0], 2)
    except OSError:
        info["load"] = None
    return info


cfg = load_config()
SNAP_DIR.mkdir(parents=True, exist_ok=True)
HIT_DIR.mkdir(parents=True, exist_ok=True)
RADAR_DIR.mkdir(parents=True, exist_ok=True)
camera = Camera(cfg)
detector = Detector(camera, cfg)
motion = Motion()
detector.motion = motion
detector.pump = FramePump(camera, motion)

# -- Busgeraet (Phase 3) ------------------------------------------------------
# Die KI laeuft nur, wenn sie eingeschaltet ist (detect.enabled, auch vom VPS
# als stgCamAi schaltbar) UND das Geraet nicht im Ruhemodus steht (stgActive).
_bus_active = [True]


def apply_detect():
    detector.set_enabled(bool(cfg["detect"].get("enabled")) and _bus_active[0])


def _bus_set_ai(on):
    cfg["detect"]["enabled"] = bool(on)
    save_config(cfg)
    apply_detect()


def _bus_on_active(on):
    _bus_active[0] = bool(on)
    apply_detect()


def _bus_on_target(m, kind, p):
    """catObserved / catDetected vom Bus -> Radar-Bild (siehe MAX_RADAR)."""
    if not _bus_active[0]:
        return                      # Ruhemodus: keine Fotos
    name = SENDER_NAMES.get(m.sender, "#%d" % m.sender)
    if kind == "detected":
        info = "KATZE bestaetigt (%s, %d%%) Welt %.1f/%.1f m" % (
            name, p["score"], p["worldX"] / 1000.0, p["worldY"] / 1000.0)
    elif p.get("worldValid"):
        info = "%s Welt %.1f/%.1f m" % (name, p["worldX"] / 1000.0, p["worldY"] / 1000.0)
    else:
        info = "%s rel. %.1f/%.1f m" % (name, p["x"] / 1000.0, p["y"] / 1000.0)
    detector.radar_event(info, force=(kind == "detected"))
    _log_pair(m, kind, p)


def _log_pair(m, kind, p):
    """Radar-Weltposition neben die gerade bewegten Kamera-Spuren schreiben."""
    if not p.get("worldValid", kind == "detected"):
        return
    tracks = motion.moving()
    if not tracks:
        return
    row = {"t": round(time.time(), 2), "sender": m.sender, "kind": kind,
           "wx": p["worldX"], "wy": p["worldY"],
           "tracks": [{"id": t["id"], "foot": t["foot"], "w": t["w"], "h": t["h"],
                       "age": t["age"], "best": t["best"]} for t in tracks]}
    try:
        if PAIRS_FILE.exists() and PAIRS_FILE.stat().st_size > PAIRS_MAX_BYTES:
            PAIRS_FILE.replace(PAIRS_FILE.with_suffix(".old"))
        with PAIRS_FILE.open("a") as fh:
            fh.write(json.dumps(row) + "\n")
    except OSError as exc:
        print("[web] pairs.jsonl: %s" % exc)


def _start_bus():
    # xComDef6_3.h liegt beim Ausrollen neben diesem Skript; im Repo eine Ebene
    # hoeher beim Manager. Ohne Header laeuft die Kamera trotzdem, nur ohne Bus.
    for cand in (BASE / "xComDef6_3.h",
                 BASE.parent / "Controller" / "Manager6_3_0" / "xComDef6_3.h"):
        if cand.is_file():
            break
    else:
        print("[bus] xComDef6_3.h nicht gefunden - ohne Busanbindung")
        return None
    try:
        from xcom import XComDef
        from kivision_bus import BusNode
        node = BusNode(XComDef(cand), lambda: bool(cfg["detect"].get("enabled")),
                       _bus_set_ai, _bus_on_active, on_target=_bus_on_target)
        node.start()
        print("[bus] Geraet %d auf dem Bus, IP %s" % (node.id, node.bus.ip))
        return node
    except Exception as exc:                                  # noqa: BLE001
        print("[bus] Start fehlgeschlagen: %s" % exc)
        return None


apply_detect()
node = _start_bus()

app = Flask(__name__, template_folder=str(BASE / "templates"))


@app.after_request
def no_cache(resp):
    resp.headers["Cache-Control"] = "no-store"
    return resp


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/stream.mjpg")
def stream():
    def generate():
        camera.add_viewer()
        seq = -1
        try:
            while True:
                frame, seq = camera.output.wait(seq)
                if frame is None:
                    continue
                # X-Seq erlaubt es, den Rueckstand zu messen: der Vergleich mit
                # "frames" aus /api/state sagt, wieviele Bilder der Client
                # hinterherhinkt - ohne auf synchrone Uhren angewiesen zu sein.
                yield (b"--frame\r\nContent-Type: image/jpeg\r\nX-Seq: "
                       + str(seq).encode() + b"\r\nContent-Length: "
                       + str(len(frame)).encode() + b"\r\n\r\n" + frame + b"\r\n")
        finally:
            camera.remove_viewer()
    return Response(generate(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/api/state")
def api_state():
    meta = camera.metadata()
    return jsonify({
        "config": cfg,
        "sizes": [list(s) for s in SIZES],
        "fps_max": camera.fps_max(),
        "stream_size": list(camera.lores_size),
        "controls": CONTROL_SPEC,
        "limits": camera.limits,
        "models": Detector.available_models(),
        "fps": camera.output.fps(),
        "mbit": camera.output.mbit(),
        "frames": camera.output.count,
        "viewers": camera.viewers,
        "system": system_info(),
        "bus": node.status() if node else None,
        "meta": {
            "ExposureTime": meta.get("ExposureTime"),
            "AnalogueGain": round(meta.get("AnalogueGain") or 0, 2),
            "DigitalGain": round(meta.get("DigitalGain") or 0, 2),
            "Lux": round(meta.get("Lux") or 0, 1),
            "ColourTemperature": meta.get("ColourTemperature"),
            "ColourGains": [round(v, 2) for v in (meta.get("ColourGains") or [])],
            "FocusFoM": meta.get("FocusFoM"),
            "lag_ms": meta.get("_lag_ms"),
        },
    })


@app.route("/api/config", methods=["POST"])
def api_config():
    data = request.get_json(force=True, silent=True) or {}
    restart = False
    for key in ("size", "ai_size"):
        if key in data:
            size = [int(v) for v in data[key]]
            if list(cfg.get(key) or []) != size:
                cfg[key] = size
                restart = True
    for key in ("fps", "quality"):
        if key in data:
            value = int(data[key])
            if cfg[key] != value:
                cfg[key] = value
                if key == "quality":
                    restart = True
    for key in ("hflip", "vflip"):
        if key in data:
            value = bool(data[key])
            if cfg[key] != value:
                cfg[key] = value
                restart = True
    for key in ("rotate", "grid"):
        if key in data:
            cfg[key] = data[key]
    if "tuning" in data and data["tuning"] != cfg.get("tuning"):
        camera.set_tuning(str(data["tuning"]))
        restart = False          # set_tuning startet schon neu
    if restart:
        camera.restart()
    else:
        camera.apply_controls()
    save_config(cfg)
    return jsonify({"ok": True, "config": cfg})


@app.route("/api/control", methods=["POST"])
def api_control():
    data = request.get_json(force=True, silent=True) or {}
    known = dict((spec["name"], spec) for spec in CONTROL_SPEC)
    for name, value in data.items():
        if name not in known:
            continue
        spec = known[name]
        if spec["type"] == "bool":
            cfg["controls"][name] = bool(value)
        elif spec["type"] in ("int", "enum"):
            cfg["controls"][name] = int(value)
        else:
            cfg["controls"][name] = float(value)
    camera.apply_controls()
    save_config(cfg)
    return jsonify({"ok": True, "controls": cfg["controls"]})


@app.route("/api/reset_controls", methods=["POST"])
def api_reset_controls():
    cfg["controls"] = {}
    camera.restart()
    save_config(cfg)
    return jsonify({"ok": True})


@app.route("/api/detect", methods=["POST"])
def api_detect():
    data = request.get_json(force=True, silent=True) or {}
    det = cfg["detect"]
    for key in ("model", "tiles"):
        if key in data:
            det[key] = str(data[key])
    if "threshold" in data:
        det["threshold"] = max(0.05, min(0.95, float(data["threshold"])))
    if "interval" in data:
        det["interval"] = max(0.05, min(5.0, float(data["interval"])))
    if "cat_only" in data:
        det["cat_only"] = bool(data["cat_only"])
    if "max_area" in data:
        det["max_area"] = max(0.05, min(1.0, float(data["max_area"])))
    if "snap_mode" in data and str(data["snap_mode"]) in ("off", "cat", "animal", "all"):
        det["snap_mode"] = str(data["snap_mode"])
    if "snap_gap" in data:
        det["snap_gap"] = max(5, min(3600, int(data["snap_gap"])))
    was = bool(det.get("enabled"))
    if "enabled" in data:
        det["enabled"] = bool(data["enabled"])
    apply_detect()
    save_config(cfg)
    if node and was != bool(det["enabled"]):
        node.settings_changed()          # VPS-Steuertab sofort nachziehen
    return jsonify({"ok": True, "detect": det})


@app.route("/api/detections")
def api_detections():
    return jsonify(detector.snapshot_state())


@app.route("/api/snapshot", methods=["POST"])
def api_snapshot():
    full = bool((request.get_json(force=True, silent=True) or {}).get("full"))
    try:
        path = camera.snapshot(detector.pump.latest()[0], full_res=full)
    except Exception as exc:
        return jsonify({"ok": False, "error": str(exc)}), 500
    return jsonify({"ok": True, "name": path.name})


@app.route("/api/snapshots")
def api_snapshots():
    files = sorted(SNAP_DIR.glob("*.jpg"), key=lambda p: p.stat().st_mtime, reverse=True)
    return jsonify([{"name": p.name,
                     "size": p.stat().st_size,
                     "ts": int(p.stat().st_mtime)} for p in files[:60]])


# Treffer (vom Modell ausgeloest) und Radar-Bilder (vom Bus ausgeloest) teilen
# sich dieselben Endpunkte: /api/<art>, /<art>/<name>, DELETE, /api/<art>/clear.
GALLERIES = {"hits": (HIT_DIR, MAX_HITS), "radar": (RADAR_DIR, MAX_RADAR),
             "motion": (MOTION_DIR, MAX_MOTION)}


@app.route("/api/<any(hits, radar, motion):kind>")
def api_hits(kind):
    """Die von selbst entstandenen Bilder, neueste zuerst."""
    directory, keep = GALLERIES[kind]
    files = sorted(directory.glob("*.jpg"), key=lambda p: p.stat().st_mtime, reverse=True)
    out = []
    for path in files[:keep]:
        # Name ist <datum>_<zeit>_<prozent>.jpg - die Sicherheit steht mit drin,
        # damit die Galerie sie ohne zweite Datei je Bild anzeigen kann.
        part = path.stem.rsplit("_", 1)[-1]
        out.append({"name": path.name,
                    "size": path.stat().st_size,
                    "ts": int(path.stat().st_mtime),
                    "score": int(part) if part.isdigit() else None})
    return jsonify({"max": keep, "files": out})


@app.route("/<any(hits, radar, motion):kind>/<path:name>")
def hit_file(kind, name):
    return send_from_directory(GALLERIES[kind][0], name)


@app.route("/api/<any(hits, radar, motion):kind>/<path:name>", methods=["DELETE"])
def hit_delete(kind, name):
    directory = GALLERIES[kind][0]
    target = (directory / name).resolve()
    if target.parent != directory.resolve() or not target.exists():
        return jsonify({"ok": False}), 404
    target.unlink()
    return jsonify({"ok": True})


@app.route("/api/<any(hits, radar, motion):kind>/clear", methods=["POST"])
def hits_clear(kind):
    count = 0
    for path in GALLERIES[kind][0].glob("*.jpg"):
        try:
            path.unlink()
            count += 1
        except OSError:
            pass
    return jsonify({"ok": True, "deleted": count})


@app.route("/snapshots/<path:name>")
def snapshot_file(name):
    return send_from_directory(SNAP_DIR, name)


@app.route("/api/snapshots/<path:name>", methods=["DELETE"])
def snapshot_delete(name):
    target = (SNAP_DIR / name).resolve()
    if target.parent != SNAP_DIR.resolve() or not target.exists():
        return jsonify({"ok": False}), 404
    target.unlink()
    return jsonify({"ok": True})


if __name__ == "__main__":
    from werkzeug.serving import WSGIRequestHandler, make_server

    # Nagle aus. Sonst haelt der Kernel das letzte Teilstueck eines Bildes
    # zurueck, bis die vorige Sendung bestaetigt ist - zusammen mit der
    # verzoegerten Bestaetigung des Empfaengers kostet das bis zu 40 ms pro
    # Bild. Bei einem Strom aus fertigen Bildern bringt das Buendeln nichts.
    WSGIRequestHandler.disable_nagle_algorithm = True

    port = int(os.environ.get("KIVISION_PORT", 8080))
    server = make_server("0.0.0.0", port, app, threaded=True)
    # Kleiner Sendepuffer: was hier drinsteht, ist beim Ankommen schon alt.
    # Auf einer schwachen Funkstrecke ist der Puffer die Verzoegerung - er
    # braucht nur den Weg zu fuellen (Bandbreite x Laufzeit, hier rund 15 kB
    # bei 30 Mbit/s und 4 ms), nicht ganze Bilder zu stapeln. Bleibt er unter
    # einer Bildgroesse, blockiert das Schreiben frueh, der Stream ueberspringt
    # Bilder und der Client bekommt immer das neueste statt eines alten aus der
    # Schlange. Linux verdoppelt den Wunschwert intern.
    server.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 24 * 1024)
    actual = server.socket.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)
    print("[web] Server auf Port %d, Sendepuffer %d kB (Kernel), Nagle %s"
          % (port, actual // 1024,
             "aus" if WSGIRequestHandler.disable_nagle_algorithm else "an"))
    server.serve_forever()
