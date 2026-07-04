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

Analyse (Aufgabe "VPS-Modellierung der Katzenerkennung", GesamtKonzeptCatFinder.md):
  Alle catObserved werden zusätzlich PERSISTENT in SQLite gespeichert (Docker-
  Volume, überlebt Container-Neubau), solange die Aufnahme läuft (Pause/Append).
  Der Manager liefert pro Event seine millis()-Empfangszeit mit; daraus werden
  ms-genaue Serverzeiten gerechnet (die Push-Zeit allein hätte 1,5-s-Raster).
  GET/POST /rec         Aufnahme-Status / Aufnahme ein-aus (Pause/Append)
  GET  /density         Ereignisdichte je Sender für die Zeitleiste (?t0&t1&bins)
  GET  /adata           Events eines Zeitfensters (?t0&t1, ausgedünnt auf max_pts)
  GET  /amodel          Modell (catmodel.py) über ein Zeitfenster laufen lassen
  GET/POST /aparams     Modell-Parameter (JSON im Volume, ohne Rebuild änderbar)
  GET  /alabels         Labels eines Zeitfensters
  POST /alabel          Label anlegen {t0,t1,sender,label,note}
  POST /alabel_del      Label löschen {id}
  POST /amark           manuelle Track-Bewertung {key,t0,t1,mark: cat|nocat|""}
                        (key = stabiler Track-Schlüssel aus /amodel; "" = löschen)
  POST /devreload       Gerätetabelle (xComDef6_3.h) sofort neu aus dem Repo lesen
                        + Manager um frische Welt-Posen bitten (poseRequest)

Erfassungsbereiche: die Sensoren tragen ihren nominellen Erfassungsbereich in der
xComDef (covLeftDeg/covRightDeg/covRangeMm); der Manager liefert die Welt-Posen
der Geräte per /ingest ("poses", aus poseReport). Daraus baut catmodel.
build_coverage_geo die Abdeckungs-Sektoren — nach dem Versetzen eines Sensors
stimmt der Bereich sofort wieder. Fallback ohne Posen: empirische Abdeckung.
"""
import os, re, time, json, sqlite3, threading, urllib.request
from collections import deque, OrderedDict
from flask import Flask, request, jsonify, send_from_directory
import catmodel

MAP_URL      = os.environ.get(
    "MAP_URL",
    "https://raw.githubusercontent.com/smily77/CatFind/main/Map/RasenKarte.csv")
FALLBACK_MAP = os.environ.get("FALLBACK_MAP", "/app/RasenKarte.csv")
MAX_EVENTS   = int(os.environ.get("MAX_EVENTS", "20000"))
HB_WINDOW_S  = int(os.environ.get("HB_WINDOW_S", "180"))    # "aktiv" = HB in den letzten 3 min
DB_PATH      = os.environ.get("DB_PATH", "/data/catfinder.db")
PARAMS_PATH  = os.environ.get("PARAMS_PATH", "/data/model_params.json")
XCOMDEF_URL  = os.environ.get(
    "XCOMDEF_URL",
    "https://raw.githubusercontent.com/smily77/CatFind/main/Controller/Manager6_3_0/xComDef6_3.h")

# Fallback-Geräteverzeichnis (Name/Typ/Gruppe/Erfassungsbereich), falls
# xComDef6_3.h nicht erreichbar ist. Führend ist IMMER die live geparste
# xComDef6_3.h (s.u.). Erfassungsbereich = (links Grad, rechts Grad, Reichweite mm).
FALLBACK_DEVICES = {
    0:("Manager","MananagementDevice",0,0,0,0), 1:("Dome","HLK",3,-60,60,7000),
    2:("MiniDome","HLK",2,-60,60,7000), 3:("CompactDome","HLK",1,-60,60,7000),
    4:("PA2i","PowerActor",1,0,0,0), 5:("Disp7","Screen",0,0,0,0),
    6:("CYD","Controller",0,0,0,0), 7:("LD06","Lidar",1,-180,180,12000),
    8:("Schalter","onOffSchalter",0,0,0,0), 9:("Disp5","Screen",0,0,0,0),
    10:("Core2","Screen",0,0,0,0), 11:("Tab5","Screen",0,0,0,0),
    12:("CYD35Z","Screen",0,0,0,0), 13:("Wave7z","Screen",0,0,0,0),
    14:("Sim","MananagementDevice",0,0,0,0), 15:("LaserMarker","Marker",0,0,0,0),
    16:("PA1_1","PowerActor",2,0,0,0), 17:("LidarC1","Lidar",0,-180,180,12000),
}

# ------------------------------------------------- Gerätetabelle aus xComDef6_3.h
# xComDef6_3.h ist die Quelle der Wahrheit (Gerät -> Typ/Gruppe/Name/Erfassungs-
# bereich); der VPS liest sie dynamisch aus dem GitHub-Repo — neue Sensoren
# brauchen keinen VPS-Eingriff. Der TYP steuert im Modell die Sensor-Gewichte
# (HLK vs. Lidar); covL/covR/covRange definieren zusammen mit der Welt-Pose
# den Erfassungssektor. Das ändert sich selten -> langer Zyklus + Reload-Knopf
# im Analyse-Tab (POST /devreload).

DEV_TTL_S = 6 * 3600

_dev_lock = threading.Lock()
_devtab = {i: {"name": n, "type": t, "group": g,
               "covL": cl, "covR": cr, "covRange": rng}
           for i, (n, t, g, cl, cr, rng) in FALLBACK_DEVICES.items()}
_dev_ts = 0.0


def parse_xcomdef(text):
    m = re.search(r"stationDefinitions\s+device\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\}\s*;",
                  text, re.S)
    if not m:
        return {}
    defines = dict(re.findall(r"#define\s+(\w+)\s+(\d+)", text))
    tab = {}
    pat = re.compile(r"\{\s*(\w+)\s*,\s*\w+\s*,\s*\w+\s*,\s*(\w+)\s*,\s*\"([^\"]*)\""
                     r"(?:\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+))?")
    for i, em in enumerate(pat.finditer(m.group(1))):
        typ, grp, name, cl, cr, rng = em.groups()
        tab[i] = {"name": name, "type": typ,
                  "group": int(defines.get(grp, grp if grp.isdigit() else "0")),
                  "covL": int(cl or 0), "covR": int(cr or 0),
                  "covRange": int(rng or 0)}
    return tab


def load_devices(force=False):
    """Gerätetabelle liefern; periodisch (bzw. per /devreload sofort) frisch
    aus dem Repo geparst."""
    global _devtab, _dev_ts
    with _dev_lock:
        if not force and _devtab and time.time() - _dev_ts < DEV_TTL_S:
            return _devtab
        try:
            with urllib.request.urlopen(XCOMDEF_URL, timeout=10) as r:
                tab = parse_xcomdef(r.read().decode("utf-8", "replace"))
            if len(tab) >= 10:
                _devtab = tab
                print("xComDef geladen: %d Geräte" % len(tab), flush=True)
        except Exception as e:                                   # noqa: BLE001
            print("xComDef fetch failed: %s" % e, flush=True)
        _dev_ts = time.time()
        return _devtab


def dev_name(sid):
    d = _devtab.get(sid)
    return d["name"] if d else "?"


def dev_group(sid):
    d = _devtab.get(sid)
    return d["group"] if d else 0

_lock = threading.Lock()
_debug = deque(maxlen=80)        # {t, msg}
_devices = {}                    # sender -> {t, ip}
_events = []                     # {t, sender, sensor, wx, wy, wv, x, y, group}
_reset_seq = 0
_settings = {}                   # sender -> {sup, val, act, t}  (aus settingsReport)
_poses = {}                      # sender -> {x, y, head, mir, valid, t}  (aus poseReport;
                                 # nur GÜLTIGE Posen, in SQLite persistiert — ein Sensor,
                                 # der nach Reboot noch nicht re-validiert ist, steht
                                 # physisch ja weiterhin am selben Ort)

_cmd_lock = threading.Lock()
_cmd_queue = []                  # [(target, cmd, info)]  Webinterface -> Manager -> Bus

_map_lock = threading.Lock()
_map = []                        # [[x,y],...] Meter
_map_ts = 0.0

# ---------------------------------------------------------------- Persistenz (SQLite im Docker-Volume)

_db_lock = threading.Lock()
_db = None
_rec_on = True                   # Aufnahme läuft (Pause/Append); persistent in meta


def init_db():
    global _db, _rec_on
    os.makedirs(os.path.dirname(DB_PATH) or ".", exist_ok=True)
    _db = sqlite3.connect(DB_PATH, check_same_thread=False)
    _db.execute("PRAGMA journal_mode=WAL")
    _db.execute("PRAGMA synchronous=NORMAL")
    _db.execute("""CREATE TABLE IF NOT EXISTS events(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        t REAL, sender INT, sensor INT, wx INT, wy INT, wv INT,
        x INT, y INT, grp INT, speed INT)""")
    _db.execute("CREATE INDEX IF NOT EXISTS idx_events_t ON events(t)")
    _db.execute("""CREATE TABLE IF NOT EXISTS drops(t REAL, dropped INT)""")
    _db.execute("""CREATE TABLE IF NOT EXISTS labels(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        t0 REAL, t1 REAL, sender INT, label TEXT, note TEXT, created REAL)""")
    _db.execute("CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v TEXT)")
    # letzte GÜLTIGE Welt-Pose je Sender (für die Erfassungssektoren)
    _db.execute("""CREATE TABLE IF NOT EXISTS poses(
        sender INT PRIMARY KEY, x INT, y INT, head REAL, mir INT, t REAL)""")
    # manuelle Track-Bewertung (Katze/keine Katze) — key = stabiler Track-
    # Schlüssel aus catmodel (Geburtszeit ms + Geburtsort), mark = cat|nocat
    _db.execute("""CREATE TABLE IF NOT EXISTS track_marks(
        key TEXT PRIMARY KEY, t0 REAL, t1 REAL, mark TEXT, created REAL)""")
    # Aufnahme-Abschnitte: wo wurde tatsächlich aufgezeichnet? (für die
    # Zeitleiste — Lücken = Pause/Container-Downtime sichtbar machen)
    _db.execute("""CREATE TABLE IF NOT EXISTS recspans(
        id INTEGER PRIMARY KEY AUTOINCREMENT, t0 REAL, t1 REAL)""")
    _db.commit()
    row = _db.execute("SELECT v FROM meta WHERE k='rec'").fetchone()
    _rec_on = (row is None) or (row[0] == "1")   # Default: Aufnahme AN
    # Migration: Daten aus der Zeit vor Einführung der recspans bekommen
    # einen Abschnitt über ihre gesamte Zeitspanne.
    if _db.execute("SELECT COUNT(*) FROM recspans").fetchone()[0] == 0:
        row = _db.execute("SELECT MIN(t),MAX(t) FROM events").fetchone()
        if row and row[0] is not None:
            _db.execute("INSERT INTO recspans(t0,t1) VALUES(?,?)", (row[0], row[1]))
            _db.commit()
    for sid, x, y, head, mir, t in _db.execute(
            "SELECT sender,x,y,head,mir,t FROM poses").fetchall():
        _poses[int(sid)] = {"x": x, "y": y, "head": head, "mir": mir,
                            "valid": 1, "t": t}
    print("db ready: %s (rec=%s, %d Posen)" % (DB_PATH, _rec_on, len(_poses)),
          flush=True)


_span_id = None                  # offener Aufnahme-Abschnitt (None = noch keiner)
_span_upd = 0.0


def _span_touch(now):
    """Aufnahme-Abschnitt fortschreiben (unter _db_lock aufrufen).
    Liefert True, wenn in der DB etwas geändert wurde."""
    global _span_id, _span_upd
    if _span_id is None:
        cur = _db.execute("INSERT INTO recspans(t0,t1) VALUES(?,?)", (now, now))
        _span_id = cur.lastrowid
        _span_upd = now
        return True
    if now - _span_upd >= 5:
        _db.execute("UPDATE recspans SET t1=? WHERE id=?", (now, _span_id))
        _span_upd = now
        return True
    return False


def set_rec(on):
    global _rec_on, _span_id
    with _db_lock:
        _rec_on = bool(on)
        _span_id = None          # Pause beendet den Abschnitt; Start öffnet neuen
        _db.execute("INSERT OR REPLACE INTO meta(k,v) VALUES('rec',?)",
                    ("1" if _rec_on else "0",))
        _db.commit()


def load_params():
    try:
        with open(PARAMS_PATH, encoding="utf-8") as f:
            return json.load(f)
    except Exception:                                        # noqa: BLE001
        return {}


def save_params(p):
    with open(PARAMS_PATH, "w", encoding="utf-8") as f:
        json.dump(p, f, indent=2, sort_keys=True)


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
    # ms-genaue Event-Zeit: der Manager schickt pro Event sein millis() beim
    # Empfang ("ms") und pro Push sein aktuelles millis() ("now_ms"). Ohne das
    # bekämen alle Events eines Pushes dieselbe Zeit (1,5-s-Raster).
    now_ms = data.get("now_ms")
    def ev_time(e):
        ms = e.get("ms")
        if now_ms is None or ms is None:
            return now
        age = (int(now_ms) - int(ms)) & 0xFFFFFFFF   # millis()-Wrap-sicher
        return now - age / 1000.0 if age <= 60000 else now
    db_rows = []
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
        pose_rows = []
        for p in data.get("poses", []):
            sid = int(p.get("sender", -1))
            if sid < 0 or int(p.get("valid", 0)) != 1:
                continue                       # ungültige Posen nicht übernehmen
            pose = {"x": int(p.get("x", 0)), "y": int(p.get("y", 0)),
                    "head": float(p.get("head", 0)), "mir": int(p.get("mir", 1)),
                    "valid": 1, "t": now}
            old = _poses.get(sid)
            _poses[sid] = pose
            if (not old or old["x"] != pose["x"] or old["y"] != pose["y"] or
                    old["head"] != pose["head"] or old["mir"] != pose["mir"]):
                pose_rows.append((sid, pose["x"], pose["y"], pose["head"],
                                  pose["mir"], now))
        for e in data.get("events", []):
            sid = int(e.get("sender", -1))
            _devices.setdefault(sid, {"t": now, "ip": 0})["t"] = now
            ev = {
                "t": ev_time(e),
                "sender": sid, "sensor": int(e.get("sensor", 0)),
                "wx": int(e.get("wx", 0)), "wy": int(e.get("wy", 0)),
                "wv": int(e.get("wv", 0)),
                "x": int(e.get("x", 0)), "y": int(e.get("y", 0)),
                "group": int(e.get("group", 0)),
                "speed": int(e.get("speed", 0)),
            }
            _events.append(ev)
            db_rows.append((ev["t"], sid, ev["sensor"], ev["wx"], ev["wy"],
                            ev["wv"], ev["x"], ev["y"], ev["group"], ev["speed"]))
        if len(_events) > MAX_EVENTS:
            del _events[0:len(_events) - MAX_EVENTS]
    dropped = int(data.get("dropped", 0) or 0)
    if pose_rows:
        with _db_lock:
            _db.executemany("INSERT OR REPLACE INTO poses(sender,x,y,head,mir,t) "
                            "VALUES(?,?,?,?,?,?)", pose_rows)
            _db.commit()
    if _rec_on:
        with _db_lock:
            # Abschnitt auch OHNE Events fortschreiben: solange der Manager
            # pusht, wurde aufgezeichnet (nur war nichts zu sehen).
            changed = _span_touch(now)
            if db_rows:
                _db.executemany("""INSERT INTO events
                    (t,sender,sensor,wx,wy,wv,x,y,grp,speed)
                    VALUES(?,?,?,?,?,?,?,?,?,?)""", db_rows)
            if dropped:
                _db.execute("INSERT INTO drops(t,dropped) VALUES(?,?)", (now, dropped))
            if changed or db_rows or dropped:
                _db.commit()
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
                devs.append({"id": sid, "name": dev_name(sid),
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
                                  "name": dev_name(sid),
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


# ---------------------------------------------------------------- Analyse-Endpunkte

def _win_args(default_all=False):
    t0 = request.args.get("t0", type=float)
    t1 = request.args.get("t1", type=float)
    if (t0 is None or t1 is None) and default_all:
        with _db_lock:
            row = _db.execute("SELECT MIN(t),MAX(t) FROM events").fetchone()
        lo = row[0] if row and row[0] is not None else time.time() - 3600
        hi = row[1] if row and row[1] is not None else time.time()
        t0 = lo if t0 is None else t0
        t1 = hi if t1 is None else t1
    return t0, t1


@app.get("/rec")
def rec_get():
    with _db_lock:
        row = _db.execute("SELECT COUNT(*),MIN(t),MAX(t) FROM events").fetchone()
    size = os.path.getsize(DB_PATH) if os.path.exists(DB_PATH) else 0
    return jsonify(on=_rec_on, rows=row[0], t_min=row[1], t_max=row[2], bytes=size)


@app.post("/rec")
def rec_set():
    d = request.get_json(force=True, silent=True) or {}
    set_rec(int(d.get("on", 1)) != 0)
    return rec_get()


@app.get("/density")
def density():
    # Ereignisdichte je Sender für die Zeitleiste (über die ganze Aufnahme).
    t0, t1 = _win_args(default_all=True)
    bins = min(max(int(request.args.get("bins", "600")), 10), 2000)
    span = max(t1 - t0, 1e-6)
    w = span / bins
    with _db_lock:
        rows = _db.execute(
            "SELECT CAST((t-?)/? AS INT) b, sender, COUNT(*) FROM events "
            "WHERE t>=? AND t<=? GROUP BY b, sender", (t0, w, t0, t1)).fetchall()
        drows = _db.execute(
            "SELECT CAST((t-?)/? AS INT) b, SUM(dropped) FROM drops "
            "WHERE t>=? AND t<=? GROUP BY b", (t0, w, t0, t1)).fetchall()
    per = {}
    for b, sid, n in rows:
        arr = per.setdefault(str(sid), [0] * bins)
        b = min(int(b), bins - 1)            # t == t1 landet sonst in Bin "bins"
        if b >= 0:
            arr[b] += n
    drops = [0] * bins
    for b, n in drows:
        b = min(int(b), bins - 1)
        if b >= 0:
            drops[b] += int(n or 0)
    with _db_lock:
        spans = _db.execute("SELECT t0,t1 FROM recspans WHERE t1>=? AND t0<=? "
                            "ORDER BY t0", (t0, t1)).fetchall()
    return jsonify(t0=t0, t1=t1, bins=bins, per_sender=per, drops=drops,
                   spans=[[a, b] for a, b in spans])


@app.get("/adata")
def adata():
    # Events eines Zeitfensters (auf max_pts ausgedünnt; Stride über die id).
    t0, t1 = _win_args()
    if t0 is None or t1 is None:
        return jsonify(error="t0/t1 fehlen"), 400
    max_pts = min(int(request.args.get("max", "20000")), 60000)
    with _db_lock:
        total = _db.execute("SELECT COUNT(*) FROM events WHERE t>=? AND t<=?",
                            (t0, t1)).fetchone()[0]
        stride = max(1, (total + max_pts - 1) // max_pts)
        rows = _db.execute(
            "SELECT t,sender,sensor,wx,wy,wv,x,y,grp,speed FROM events "
            "WHERE t>=? AND t<=? AND (id % ?)=0 ORDER BY t", (t0, t1, stride)).fetchall()
    evs = [{"t": r[0], "sender": r[1], "sensor": r[2], "wx": r[3], "wy": r[4],
            "wv": r[5], "x": r[6], "y": r[7], "group": r[8], "speed": r[9]}
           for r in rows]
    return jsonify(t0=t0, t1=t1, total=total, stride=stride, events=evs)


# ------------------------------------------------- Sensor-Abdeckung
# Primär GEOMETRISCH: nomineller Erfassungsbereich aus der xComDef
# (covL/covR/covRange je Gerät) + Welt-Pose (poseReport über den Manager)
# -> Sektor in Weltkoordinaten. Nach dem Versetzen eines Sensors stimmt der
# Bereich sofort wieder (neue Pose genügt), nichts muss neu erfasst werden.
# FALLBACK (keine Posen bekannt): empirische Abdeckung aus den Langzeitdaten
# (Belegungsraster; teuer -> gecacht, 10 min TTL).

_cov_lock = threading.Lock()
_cov = None
_cov_ts = 0.0
_cov_key = None


def _empirical_coverage(p):
    global _cov, _cov_ts, _cov_key
    key = (p["cov_cell_mm"], p["cov_min_events"])
    with _cov_lock:
        if _cov is not None and key == _cov_key and time.time() - _cov_ts < 600:
            return _cov
        with _db_lock:
            total = _db.execute("SELECT COUNT(*) FROM events WHERE wv=1").fetchone()[0]
            stride = max(1, total // 300000)
            rows = _db.execute(
                "SELECT sender,wx,wy FROM events WHERE wv=1 AND (id % ?)=0",
                (stride,)).fetchall()
        pts = {}
        for sid, x, y in rows:
            pts.setdefault(sid, []).append((x, y))
        min_ev = p["cov_min_events"] if stride == 1 else max(1, p["cov_min_events"] // stride)
        _cov = catmodel.build_coverage(pts, p["cov_cell_mm"], min_ev)
        _cov_ts, _cov_key = time.time(), key
        return _cov


def get_coverage(p):
    devices = load_devices()
    with _lock:
        poses = dict(_poses)
    cov = catmodel.build_coverage_geo(devices, poses, p["cov_cell_mm"])
    if cov["senders"]:
        return cov
    return _empirical_coverage(p)


@app.get("/acoverage")
def acoverage():
    # Abdeckung fürs Karten-Overlay: Sektoren (geometrisch) + Zellmittelpunkte.
    p = catmodel.merged_params(load_params())
    cov = get_coverage(p)
    cell = cov["cell_mm"]
    out = {str(sid): [[(ix + 0.5) * cell, (iy + 0.5) * cell] for ix, iy in cells]
           for sid, cells in cov["senders"].items()}
    return jsonify(cell_mm=cell, senders=out,
                   source=cov.get("source", ""),
                   sectors={str(k): v for k, v in cov.get("sectors", {}).items()},
                   union_cells=len(cov["union"]),
                   edge_active=len(cov["union"]) >= p["cov_min_cells"])


@app.post("/devreload")
def devreload():
    # Analyse-Tab-Knopf: Gerätetabelle (xComDef6_3.h) sofort neu aus dem Repo
    # lesen und den Manager um frische Welt-Posen bitten (target 254 =
    # poseRequest-Broadcast; die Antworten kommen mit dem nächsten Ingest).
    tab = load_devices(force=True)
    with _cmd_lock:
        _cmd_queue.append((254, 0, 0))
    with _lock:
        n_poses = len(_poses)
    return jsonify(ok=True, devices={str(k): v for k, v in tab.items()},
                   poses=n_poses)


@app.get("/amodel")
def amodel():
    # Modell über ein Zeitfenster: Tracks + Stürme + CatDetected-Markierung.
    t0, t1 = _win_args()
    if t0 is None or t1 is None:
        return jsonify(error="t0/t1 fehlen"), 400
    with _db_lock:
        total = _db.execute(
            "SELECT COUNT(*) FROM events WHERE t>=? AND t<=? AND wv=1",
            (t0, t1)).fetchone()[0]
        if total > 200000:
            return jsonify(error="Fenster zu gross (%d Events) — bitte stauchen" % total), 400
        rows = _db.execute(
            "SELECT t,sender,sensor,wx,wy,speed FROM events "
            "WHERE t>=? AND t<=? AND wv=1 ORDER BY t", (t0, t1)).fetchall()
    evs = [{"t": r[0], "sender": r[1], "sensor": r[2], "wx": r[3], "wy": r[4],
            "speed": r[5]} for r in rows]
    params = catmodel.merged_params(load_params())
    devices = load_devices()
    res = catmodel.analyze(evs, params, devices=devices,
                           coverage=get_coverage(params))
    res["t0"], res["t1"] = t0, t1
    res["devices"] = {str(k): {"name": v["name"], "type": v["type"],
                               "covL": v.get("covL", 0), "covR": v.get("covR", 0),
                               "covRange": v.get("covRange", 0)}
                      for k, v in devices.items()}
    # manuelle Track-Bewertungen (Katze/keine Katze) der Fenster-Tracks
    with _db_lock:
        rows = _db.execute("SELECT key,mark FROM track_marks "
                           "WHERE t1>=? AND t0<=?", (t0, t1)).fetchall()
    res["marks"] = {k: m for k, m in rows}
    return jsonify(res)


@app.post("/amark")
def amark():
    # manuelle Bewertung eines Tracks setzen/löschen. mark: "cat" (ist Katze),
    # "nocat" (ist keine Katze), "" = Markierung entfernen (= einverstanden
    # mit der Modellbewertung). Die Modellbewertung selbst bleibt unberührt —
    # der Vergleich Modell vs. Mensch läuft im UI (Übereinstimmungs-Statistik).
    d = request.get_json(force=True, silent=True) or {}
    key = str(d.get("key", ""))[:80]
    if not key:
        return jsonify(error="key fehlt"), 400
    mark = str(d.get("mark", ""))
    with _db_lock:
        if mark in ("cat", "nocat"):
            _db.execute("INSERT OR REPLACE INTO track_marks(key,t0,t1,mark,created) "
                        "VALUES(?,?,?,?,?)",
                        (key, float(d.get("t0", 0)), float(d.get("t1", 0)),
                         mark, time.time()))
        else:
            _db.execute("DELETE FROM track_marks WHERE key=?", (key,))
        _db.commit()
    return jsonify(ok=True)


@app.get("/aparams")
def aparams_get():
    return jsonify(catmodel.merged_params(load_params()))


@app.post("/aparams")
def aparams_set():
    d = request.get_json(force=True, silent=True)
    if not isinstance(d, dict):
        return jsonify(error="JSON-Objekt erwartet"), 400
    merged = catmodel.merged_params(d)      # validiert/typisiert gegen die Defaults
    save_params(merged)
    return jsonify(merged)


@app.get("/alabels")
def alabels():
    t0, t1 = _win_args(default_all=True)
    with _db_lock:
        rows = _db.execute(
            "SELECT id,t0,t1,sender,label,note FROM labels "
            "WHERE t1>=? AND t0<=? ORDER BY t0", (t0, t1)).fetchall()
    return jsonify(labels=[{"id": r[0], "t0": r[1], "t1": r[2], "sender": r[3],
                            "label": r[4], "note": r[5]} for r in rows])


@app.post("/alabel")
def alabel():
    d = request.get_json(force=True, silent=True) or {}
    try:
        t0, t1 = float(d["t0"]), float(d["t1"])
    except (KeyError, TypeError, ValueError):
        return jsonify(error="t0/t1 fehlen"), 400
    sender = int(d.get("sender", -1))        # -1 = alle Sensoren
    label = str(d.get("label", "unbekannt"))[:40]
    note = str(d.get("note", ""))[:200]
    with _db_lock:
        cur = _db.execute("INSERT INTO labels(t0,t1,sender,label,note,created) "
                          "VALUES(?,?,?,?,?,?)", (t0, t1, sender, label, note, time.time()))
        _db.commit()
    return jsonify(ok=True, id=cur.lastrowid)


@app.post("/alabel_del")
def alabel_del():
    d = request.get_json(force=True, silent=True) or {}
    with _db_lock:
        _db.execute("DELETE FROM labels WHERE id=?", (int(d.get("id", -1)),))
        _db.commit()
    return jsonify(ok=True)


init_db()
load_map()
load_devices()

if __name__ == "__main__":
    from waitress import serve
    port = int(os.environ.get("PORT", "80"))
    print("CatFinder dashboard on :%d" % port, flush=True)
    serve(app, host="0.0.0.0", port=port, threads=4)
