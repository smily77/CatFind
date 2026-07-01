#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CatFinder VPS-Dashboard (Treffervisualisierung).

Nimmt vom Manager (Gateway) gebündelte System-Ereignisse entgegen und stellt sie
auf einer Web-Seite dar:
  - immer sichtbar: scrollendes Debug-Fenster + Liste der zuletzt aktiven Geräte (HB)
  - umschaltbar: Ereignisliste (pro Minute zusammengefasst) und Karten der
    catObserved in Welt- bzw. relativen Gruppen-Koordinaten (Reset-Button)

Endpunkte:
  GET  /                index.html (Single-Page-UI)
  POST /ingest          Manager-Push: {"events":[...],"debug":[...],"hb":[...],"settings":[...]}
  GET  /state           Debug + Geräte + Minuten-Zusammenfassung + Geräte-Einstellungen
  GET  /events?since=N  neue catObserved ab Index N (für die Karten)
  POST /reset           akkumulierte Ereignisse löschen
  GET  /map             RasenKarte-Punkte (aus dem GitHub-Repo) für die Welt-Karte
  POST /command         Webinterface -> Kommando-Queue {"target":id,"cmd":c,"info":i}
  GET  /commands        Manager holt anstehende Kommandos ab (CSV "target,cmd,info"), leert die Queue
"""
import os, time, threading, urllib.request
from collections import deque, OrderedDict
from flask import Flask, request, jsonify, send_from_directory

MAP_URL      = os.environ.get(
    "MAP_URL",
    "https://raw.githubusercontent.com/smily77/CatFind/main/Map/RasenKarte.csv")
FALLBACK_MAP = os.environ.get("FALLBACK_MAP", "/app/RasenKarte.csv")
MAX_EVENTS   = int(os.environ.get("MAX_EVENTS", "20000"))
HB_WINDOW_S  = int(os.environ.get("HB_WINDOW_S", "180"))    # "aktiv" = HB in den letzten 3 min

# Geräte-Verzeichnis (spiegelt device[] in xComDef6_3.h; nur für Anzeigenamen/Gruppen)
DEVICES = {
    0:("Manager",0), 1:("Dome",0), 2:("MiniDome",2), 3:("CompactDome",1),
    4:("PA2i",1), 5:("Disp7",0), 6:("CYD",0), 7:("LD06",1), 8:("Schalter",0),
    9:("Disp5",0), 10:("Core2",0), 11:("Tab5",0), 12:("CYD35Z",0), 13:("Wave7z",0),
    14:("Sim",0), 15:("LaserMarker",0), 16:("PA1_1",2), 17:("LidarC1",0),
}

_lock = threading.Lock()
_debug = deque(maxlen=80)        # {t, msg}
_devices = {}                    # sender -> {t, ip}
_events = []                     # {t, sender, sensor, wx, wy, wv, x, y, group}
_reset_seq = 0
_settings = {}                   # sender -> {sup, val, act, t}  (aus settingsReport)

_cmd_lock = threading.Lock()
_cmd_queue = []                  # [(target, cmd, info)]  Webinterface -> Manager -> Bus

_map_lock = threading.Lock()
_map = []                        # [[x,y],...] Meter
_map_ts = 0.0


def load_map():
    global _map, _map_ts
    with _map_lock:
        if _map and time.time() - _map_ts < 600:
            return
        text = None
        try:
            with urllib.request.urlopen(MAP_URL, timeout=10) as r:
                text = r.read().decode("utf-8", "replace")
        except Exception as e:                                  # noqa: BLE001
            print("map fetch failed: %s" % e, flush=True)
        if text is None and os.path.exists(FALLBACK_MAP):
            text = open(FALLBACK_MAP, encoding="utf-8", errors="replace").read()
        pts = []
        if text:
            for line in text.splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                p = line.replace(";", ",").split(",")
                try:
                    pts.append([float(p[0]), float(p[1])])
                except (ValueError, IndexError):
                    continue
        if pts:
            _map, _map_ts = pts, time.time()
            print("map loaded: %d points" % len(pts), flush=True)


app = Flask(__name__, static_folder="static")


@app.after_request
def no_cache(resp):
    # Browser nie cachen lassen -> Live-Daten + immer aktuelle Seite
    resp.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
    resp.headers["Pragma"] = "no-cache"
    return resp


@app.get("/")
def index():
    return send_from_directory("static", "index.html")


@app.post("/ingest")
def ingest():
    data = request.get_json(force=True, silent=True) or {}
    now = time.time()
    with _lock:
        for m in data.get("debug", []):
            _debug.append({"t": now, "msg": str(m)[:200]})
        for h in data.get("hb", []):
            sid = int(h.get("sender", -1))
            _devices[sid] = {"t": now, "ip": int(h.get("ip", 0))}
        for s in data.get("settings", []):
            sid = int(s.get("sender", -1))
            _settings[sid] = {"sup": int(s.get("sup", 0)), "val": int(s.get("val", 0)),
                              "act": int(s.get("act", 0)), "t": now}
        for e in data.get("events", []):
            sid = int(e.get("sender", -1))
            _devices.setdefault(sid, {"t": now, "ip": 0})["t"] = now
            _events.append({
                "t": now,
                "sender": sid, "sensor": int(e.get("sensor", 0)),
                "wx": int(e.get("wx", 0)), "wy": int(e.get("wy", 0)),
                "wv": int(e.get("wv", 0)),
                "x": int(e.get("x", 0)), "y": int(e.get("y", 0)),
                "group": int(e.get("group", 0)),
            })
        if len(_events) > MAX_EVENTS:
            del _events[0:len(_events) - MAX_EVENTS]
    return jsonify(ok=True)


def minute_summary():
    # catObserved pro Minute zu einem Eintrag zusammenfassen
    # (Zeit + meldende Sensor-IDs + Anzahl Treffer in der Minute)
    buckets = OrderedDict()
    for e in _events:
        key = int(e["t"] // 60) * 60
        b = buckets.setdefault(key, {"senders": set(), "count": 0})
        b["senders"].add(e["sender"])
        b["count"] += 1
    out = [{"minute": k, "senders": sorted(v["senders"]), "count": v["count"]}
           for k, v in buckets.items()]
    out.sort(key=lambda r: r["minute"], reverse=True)
    return out[:180]


@app.get("/state")
def state():
    now = time.time()
    with _lock:
        devs = []
        for sid, d in sorted(_devices.items()):
            age = now - d["t"]
            if age <= HB_WINDOW_S:
                devs.append({"id": sid, "name": DEVICES.get(sid, ("?", 0))[0],
                             "ip": d["ip"], "age": round(age, 1)})
        # Steuerung nur fuer AKTIVE Geraete (HB in den letzten HB_WINDOW_S Sekunden gesehen).
        # Ein abgeschaltetes Geraet faellt so aus der Steuerungsliste, auch wenn sein letzter
        # settingsReport noch bekannt ist.
        settings = {}
        for sid, s in _settings.items():
            dev = _devices.get(sid)
            if not dev or (now - dev["t"]) > HB_WINDOW_S:
                continue
            settings[str(sid)] = {"sup": s["sup"], "val": s["val"], "act": s["act"],
                                  "name": DEVICES.get(sid, ("?", 0))[0],
                                  "ip": dev.get("ip", 0),
                                  "age": round(now - dev["t"], 1)}
        return jsonify(now=now, reset_seq=_reset_seq,
                       debug=list(_debug)[-60:], devices=devs,
                       summary=minute_summary(), event_count=len(_events),
                       settings=settings)


@app.get("/events")
def events():
    since = int(request.args.get("since", "0"))
    with _lock:
        if since < 0 or since > len(_events):
            since = 0
        return jsonify(reset_seq=_reset_seq, base=since, total=len(_events),
                       events=_events[since:since + 5000])


@app.post("/reset")
def reset():
    global _reset_seq
    with _lock:
        _events.clear()
        _reset_seq += 1
    return jsonify(ok=True, reset_seq=_reset_seq)


@app.get("/map")
def get_map():
    load_map()
    with _map_lock:
        return jsonify(points=_map)


@app.post("/command")
def command():
    # Webinterface stellt ein Kommando in die Queue; der Manager holt es per /commands ab
    # und gibt es auf den lokalen Bus (der VPS erreicht die 192.168.0.x-Geräte nicht direkt).
    # target 255 = Broadcast settingsRequest (alle Geräte melden ihre Einstellungen).
    d = request.get_json(force=True, silent=True) or {}
    t = int(d.get("target", -1))
    c = int(d.get("cmd", 0))
    i = int(d.get("info", 0))
    with _cmd_lock:
        _cmd_queue.append((t, c, i))
        n = len(_cmd_queue)
    return jsonify(ok=True, queued=n)


@app.get("/commands")
def commands():
    with _cmd_lock:
        q = _cmd_queue[:]
        _cmd_queue.clear()
    body = "".join("%d,%d,%d\n" % (t, c, i) for (t, c, i) in q)
    return body, 200, {"Content-Type": "text/plain; charset=utf-8"}


load_map()

if __name__ == "__main__":
    from waitress import serve
    port = int(os.environ.get("PORT", "80"))
    print("CatFinder dashboard on :%d" % port, flush=True)
    serve(app, host="0.0.0.0", port=port, threads=4)
