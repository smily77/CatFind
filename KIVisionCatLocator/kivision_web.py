#!/usr/bin/env python3
"""KIVisionCatLocator - Phase 2: Kamera-Test und Webserver.

Liefert ein MJPEG-Livebild der OV5647 im Browser, erlaubt das Verstellen der
wichtigsten Kamera-Parameter, macht Schnappschuesse (Stream-Aufloesung oder
volle 5 MP) und kann optional den Coral zuschalten, um direkt am Montageort zu
pruefen, ob eine Katze von dort ueberhaupt erkannt wird.

Bewusst eigenstaendig: keine Bus-Anbindung, kein VPS - das kommt in Phase 3.
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

from picamera2 import Picamera2
from picamera2.encoders import JpegEncoder
from picamera2.outputs import FileOutput

BASE = Path(__file__).resolve().parent
HOME = Path(os.environ.get("KIVISION_HOME", Path.home() / "kivision"))
SNAP_DIR = HOME / "snapshots"
MODEL_DIR = HOME / "models"
CONFIG_FILE = HOME / "web_config.json"
MAX_SNAPSHOTS = 200

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
    "size": [640, 480],
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
        "tiles": "1x1",
        "interval": 0.3,
        "cat_only": True,
    },
}


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

    def _build_controls(self):
        ctrl = {}
        fps = max(1, int(self.cfg["fps"]))
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
            size = tuple(self.cfg["size"])
            config = self.picam.create_video_configuration(
                main={"size": size, "format": "RGB888"},
                transform=self._transform(),
                controls=self._build_controls(),
                buffer_count=3,   # weniger Puffer = weniger Verzoegerung
            )
            self.picam.configure(config)
            self.picam.start()
            self.running = True
            if self.viewers > 0:
                self._encoder_start()
            print("[web] Kamera laeuft: %dx%d @ %s fps" % (size[0], size[1], self.cfg["fps"]))

    # -- Encoder laeuft nur, solange jemand zuschaut ----------------------
    def _encoder_start(self):
        with self.lock:
            if self.encoder is not None or not self.running:
                return
            self.encoder = JpegEncoder(q=int(self.cfg["quality"]))
            self.picam.start_encoder(self.encoder, FileOutput(self.output))

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

    def snapshot(self, full_res=False):
        SNAP_DIR.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        tag = "voll" if full_res else "stream"
        path = SNAP_DIR / ("%s_%s.jpg" % (stamp, tag))
        with self.lock:
            if not full_res:
                self.picam.capture_file(str(path))
            else:
                # Fuer die volle Aufloesung muss der Stream kurz weichen.
                self.stop()
                try:
                    still = self.picam.create_still_configuration(
                        main={"size": SIZES[-1]},
                        transform=self._transform(),
                        controls=self._build_controls())
                    self.picam.configure(still)
                    self.picam.start()
                    time.sleep(0.8)          # Automatik einschwingen lassen
                    self.picam.capture_file(str(path))
                finally:
                    try:
                        self.picam.stop()
                    except Exception:
                        pass
                    self.start()
        prune_snapshots()
        return path


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
        self.model_path = None
        self.interpreter = None
        self.labels = {}
        self.input_size = (300, 300)

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
            return Image.fromarray(tile).resize(size, Image.BILINEAR)
        except ImportError:
            import numpy as np
            ys = (np.linspace(0, tile.shape[0] - 1, size[1])).astype(int)
            xs = (np.linspace(0, tile.shape[1] - 1, size[0])).astype(int)
            return tile[ys][:, xs]

    def _infer(self, frame):
        from pycoral.adapters import common, detect
        height, width = frame.shape[:2]
        tiles = self._tile_boxes(self.cfg["detect"].get("tiles", "1x1"), width, height)
        threshold = float(self.cfg["detect"].get("threshold", 0.4))
        cat_only = bool(self.cfg["detect"].get("cat_only", True))
        boxes = []
        started = time.monotonic()
        for (tx, ty, tw, th) in tiles:
            crop = frame[ty:ty + th, tx:tx + tw]
            if crop.size == 0:
                continue
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
            for obj in detect.get_objects(self.interpreter, threshold):
                label = self.labels.get(obj.id, str(obj.id))
                if cat_only and "cat" not in label.lower():
                    continue
                x0 = clamp01((tx + obj.bbox.xmin * sx) / width)
                y0 = clamp01((ty + obj.bbox.ymin * sy) / height)
                x1 = clamp01((tx + obj.bbox.xmax * sx) / width)
                y1 = clamp01((ty + obj.bbox.ymax * sy) / height)
                if x1 <= x0 or y1 <= y0:
                    continue
                boxes.append({
                    "x": round(x0, 4),
                    "y": round(y0, 4),
                    "w": round(x1 - x0, 4),
                    "h": round(y1 - y0, 4),
                    "score": round(float(obj.score), 3),
                    "label": label,
                })
        ms = round((time.monotonic() - started) * 1000, 1)
        norm_tiles = [{"x": t[0] / width, "y": t[1] / height,
                       "w": t[2] / width, "h": t[3] / height} for t in tiles]
        return {"boxes": boxes, "ms": ms, "ts": time.time(), "tiles": norm_tiles}

    # -- Thread ----------------------------------------------------------
    def _run(self):
        while not self.stop_event.is_set():
            try:
                self._ensure_model()
                frame = self.camera.capture_array()
                if frame is None:
                    time.sleep(0.2)
                    continue
                result = self._infer(frame)
                with self.lock:
                    self.result = result
                    self.error = ""
            except Exception as exc:
                with self.lock:
                    self.error = "%s: %s" % (type(exc).__name__, exc)
                print("[web] Erkennung fehlgeschlagen: %s" % exc)
                self.stop_event.wait(2.0)
            self.stop_event.wait(max(0.05, float(self.cfg["detect"].get("interval", 0.3))))

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
        with self.lock:
            data = dict(self.result)
            data["error"] = self.error
        data["running"] = self.thread is not None and self.thread.is_alive()
        return data


def clamp01(value):
    return max(0.0, min(1.0, value))


def prune_snapshots():
    files = sorted(SNAP_DIR.glob("*.jpg"), key=lambda p: p.stat().st_mtime, reverse=True)
    for old in files[MAX_SNAPSHOTS:]:
        try:
            old.unlink()
        except OSError:
            pass


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
camera = Camera(cfg)
detector = Detector(camera, cfg)
if cfg["detect"].get("enabled"):
    detector.set_enabled(True)

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
        "controls": CONTROL_SPEC,
        "limits": camera.limits,
        "models": Detector.available_models(),
        "fps": camera.output.fps(),
        "mbit": camera.output.mbit(),
        "frames": camera.output.count,
        "viewers": camera.viewers,
        "system": system_info(),
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
    if "size" in data:
        size = [int(v) for v in data["size"]]
        if list(cfg["size"]) != size:
            cfg["size"] = size
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
    if "enabled" in data:
        det["enabled"] = bool(data["enabled"])
    detector.set_enabled(det["enabled"])
    save_config(cfg)
    return jsonify({"ok": True, "detect": det})


@app.route("/api/detections")
def api_detections():
    return jsonify(detector.snapshot_state())


@app.route("/api/snapshot", methods=["POST"])
def api_snapshot():
    full = bool((request.get_json(force=True, silent=True) or {}).get("full"))
    try:
        path = camera.snapshot(full_res=full)
    except Exception as exc:
        return jsonify({"ok": False, "error": str(exc)}), 500
    return jsonify({"ok": True, "name": path.name})


@app.route("/api/snapshots")
def api_snapshots():
    files = sorted(SNAP_DIR.glob("*.jpg"), key=lambda p: p.stat().st_mtime, reverse=True)
    return jsonify([{"name": p.name,
                     "size": p.stat().st_size,
                     "ts": int(p.stat().st_mtime)} for p in files[:60]])


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
    from werkzeug.serving import make_server

    port = int(os.environ.get("KIVISION_PORT", 8080))
    server = make_server("0.0.0.0", port, app, threaded=True)
    # Kleiner Sendepuffer: bei schwachem WLAN blockiert das Schreiben schnell,
    # der Stream ueberspringt dann Bilder, statt sekundenlang nachzuhinken.
    # Angenommene Verbindungen erben die Puffergroesse vom Listener.
    server.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 96 * 1024)
    print("[web] Server auf Port %d, Sendepuffer 96 kB" % port)
    server.serve_forever()
