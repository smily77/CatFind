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
  POST /ingest          Manager-Push: {"events":[...],"debug":[...],"hb":[...],"settings":[...],
                        "maps":[{"type","version","crc"},...]}
  GET  /state           Debug + Geräte + Minuten-Zusammenfassung + Geräte-Einstellungen
  GET  /events?since=N  neue catObserved ab Index N (für die Karten)
  POST /reset           akkumulierte Ereignisse löschen
  GET  /map             Rasen-Punkte (Manager-Spiegel, Meter) für die Welt-Karte
  GET  /noshot          No-Shot-Karte (Manager-Spiegel) als Ringe in Welt-mm
  GET  /rasenmap        RasenKarte (Manager-Spiegel) als Ringe in Welt-mm (für den Editor)
  GET  /maps/<typ>      Manager holt eine pending Karte ab (204 = nichts pending)
  POST /maps/<typ>      Editor speichert eine Kartenänderung als pending {"rings":[[[x,y],...],...]}
  POST /mapsync         Manager meldet die aktive Karte (Version/CRC in der Query, CSV im Body) -
                        siehe MapConcept.md für den vollständigen Karten-Sync-Ablauf
  GET/POST /coverage_*  gelernte Sensor-Erfassungs-Polygone aus Mäherfahrten
                        inkl. /coverage_export(.csv) fuer CatIdentifier/ESP32
  GET  /manual_data     Daten fuer manuelle Sensor-Weltpose-Kalibration
  POST /manual_pose     manuell bestimmte Pose als Kommando-Sequenz an Sensor senden
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
  GET  /amodel          Tracks eines Zeitfensters aus der Analysierer-DB lesen
                        (die ERKENNUNG läuft kontinuierlich im Hintergrund-Thread,
                        völlig unabhängig von der Betrachtung — s.u.)
  GET/POST /aparams     Modell-Parameter (JSON im Volume; Änderung -> Neuaufbau)
  GET  /alabels         Labels eines Zeitfensters
  POST /alabel          Label anlegen {t0,t1,sender,label,note}
  POST /alabel_del      Label löschen {id}
  POST /amark           manuelle Track-Bewertung {key,t0,t1,mark} (cat|person|bird|
                        mower|insect|storm|nocat|surenocat; "" = löschen)
  POST /amerge          Tracks zusammenkleben {ids:[...]}; /aunmerge {merge_id} löst
  GET  /atracks(.csv)   komplette Trackliste inkl. Bewertungen/Klebungen (JSON/CSV)
  GET  /avalidate       Modell gegen alle von Hand bewerteten Tracks prüfen
  POST /devreload       Gerätetabelle (xComDef6_3.h) sofort neu aus dem Repo lesen
                        + Manager um frische Welt-Posen bitten (poseRequest)

Kontinuierlicher Analysierer: Hintergrund-Thread arbeitet die Aufnahme in
Häppchen ab (catmodel.analyze_stream — dasselbe Modell, das später auf dem
ESP32 laufen soll), vergibt fortlaufende Track-Nummern und persistiert fertige
Tracks in SQLite (Tabelle tracks). Parameter-/Mäher-Label-Änderungen lösen
automatisch einen kompletten Neuaufbau aus (Signaturvergleich).

Erfassungsbereiche: die Sensoren tragen ihren nominellen Erfassungsbereich in der
xComDef (covLeftDeg/covRightDeg/covRangeMm); der Manager liefert die Welt-Posen
der Geräte per /ingest ("poses", aus poseReport). Daraus baut catmodel.
build_coverage_geo die Abdeckungs-Sektoren — nach dem Versetzen eines Sensors
stimmt der Bereich sofort wieder. Fallback ohne Posen: empirische Abdeckung.
"""
import os, re, time, json, base64, sqlite3, threading, hashlib, traceback, urllib.request, urllib.error, zoneinfo
from datetime import datetime
from collections import deque, OrderedDict
from flask import Flask, request, jsonify, send_from_directory, Response
import catmodel

MAX_EVENTS   = int(os.environ.get("MAX_EVENTS", "20000"))
HB_WINDOW_S  = int(os.environ.get("HB_WINDOW_S", "180"))    # "aktiv" = HB in den letzten 3 min
DB_PATH      = os.environ.get("DB_PATH", "/data/catfinder.db")
PARAMS_PATH  = os.environ.get("PARAMS_PATH", "/data/model_params.json")
PHOTOS_DIR   = os.environ.get("PHOTOS_DIR", "/data/photos")
LOCAL_TZ     = os.environ.get("LOCAL_TZ", "Europe/Zurich")  # für die täglichen Bilder-Sperrzeiten
try:
    _TZ = zoneinfo.ZoneInfo(LOCAL_TZ)
except Exception:                                            # noqa: BLE001
    _TZ = None                                               # Fallback: OS-lokale Zeit
XCOMDEF_URL  = os.environ.get(
    "XCOMDEF_URL",
    "https://raw.githubusercontent.com/smily77/CatFind/main/Controller/Manager6_3_0/xComDef6_3.h")

# --- Karten-Spiegel (MapConcept.md) ------------------------------------------------
# Kein GitHub-Regex-Fetch mehr (frueher NOSHOT_URL -> Manager6_3_0.ino parsen bzw.
# MAP_URL auf die Meter-RasenKarte): der VPS zeigt nur noch, was der Manager per
# /mapsync SELBST bestaetigt hat - das ist immer der wirklich aktive Kartenstand,
# nicht das, was zuletzt auf GitHub gepusht wurde. Ist der Spiegel leer (frischer
# VPS, Manager noch nie verbunden), liefern /map und /noshot schlicht nichts.
GITHUB_REPO      = os.environ.get("GITHUB_REPO", "smily77/CatFind")
GITHUB_MAP_TOKEN = os.environ.get("GITHUB_MAP_TOKEN", "")   # Deploy-Token, nur Schreibrecht auf die zwei Pfade unten
MAP_TYPE_ID   = {"noshot": 1, "rasen": 2}      # == mapNoShot/mapRasen in xComDef6_3.h
# Single Source of Truth im Repo: Controller/Manager6_3_0/data/<typ>.csv - GENAU
# der Ordner, aus dem das Arduino/ESP32-Tooling das LittleFS-Image fuer USB-/OTA-
# Upload baut (siehe Controller/Manager6_3_0/KartenUpload.md). Map/backup/<typ>.csv
# ist NUR eine zusaetzliche Sicherheitskopie, nie die Quelle.
MAP_SOURCE_PATH = {"noshot": "Controller/Manager6_3_0/data/noshot.csv",
                    "rasen":  "Controller/Manager6_3_0/data/rasen.csv"}
MAP_BACKUP_PATH = {"noshot": "Map/backup/noshot.csv", "rasen": "Map/backup/rasen.csv"}
MAP_CMD_FETCH = 28   # == cmdMapFetch (xComDef6_3.h): Manager holt eine pending Karte ab
MAP_CMD_PUSH  = 29   # == cmdMapPush  (xComDef6_3.h): Manager meldet die aktive Karte (Re-Sync)
MANAGER_TARGET = 0   # == #define Manager 0 (xComDef6_3.h device-Index)

_map_mirror_lock = threading.Lock()
_map_mirror = {}                 # {"noshot": {"version","crc","csv","rings","t"}, "rasen": {...}}
_map_pending_lock = threading.Lock()
_map_pending = {}                # {"noshot": {"csv","t"}, "rasen": {...}} - Editor "Speichern", bis
                                  # der Manager sie abgeholt (und via /mapsync bestaetigt) hat
_map_resync_requested = {}       # type -> letzter cmdMapPush-Anstoss (Anti-Spam in ingest())


def _parse_map_csv(text):
    """CSV (Kommentarzeilen '#', Ringe durch Leerzeile getrennt, 'x,y' in Welt-mm) -> Ringe."""
    rings, ring = [], []
    for line in (text or "").splitlines():
        line = line.strip()
        if not line:
            if len(ring) >= 3:
                rings.append(ring)
            ring = []
            continue
        if line.startswith("#"):
            continue
        p = line.replace(";", ",").split(",")
        try:
            ring.append([int(float(p[0])), int(float(p[1]))])
        except (ValueError, IndexError):
            continue
    if len(ring) >= 3:
        rings.append(ring)
    return rings


def _github_commit_map(map_type, csv_text, version, path):
    """Vom Manager bestaetigte Karte nach `path` committen (GitHub Contents API -
    kein lokaler Git-Client/Checkout im Container noetig). Wird zweimal aufgerufen
    (Source of truth + Backup, siehe mapsync()). Best-effort: ohne Token oder bei
    Fehlern nur geloggt, nie fatal - die Kartenverteilung selbst haengt NICHT vom
    Repo-Spiegel ab (Leitplanke 1, MapConcept.md)."""
    if not GITHUB_MAP_TOKEN or not path:
        return
    api = "https://api.github.com/repos/%s/contents/%s" % (GITHUB_REPO, path)
    headers = {"Authorization": "token " + GITHUB_MAP_TOKEN,
               "Accept": "application/vnd.github+json",
               "User-Agent": "CatFind-VPS"}
    try:
        sha = None
        try:
            with urllib.request.urlopen(urllib.request.Request(api, headers=headers), timeout=10) as r:
                sha = json.load(r).get("sha")
        except urllib.error.HTTPError as e:
            if e.code != 404:        # 404 = Datei existiert noch nicht -> sha bleibt None (Neuanlage)
                raise
        payload = {
            "message": "Karte %s: v%s vom Manager uebernommen (VPS-Editor)" % (map_type, version),
            "content": base64.b64encode(csv_text.encode("utf-8")).decode("ascii"),
        }
        if sha:
            payload["sha"] = sha
        req = urllib.request.Request(api, data=json.dumps(payload).encode("utf-8"),
                                      headers=headers, method="PUT")
        with urllib.request.urlopen(req, timeout=15) as r:
            r.read()
        print("map %s: v%s nach %s committet" % (map_type, version, path), flush=True)
    except Exception as e:                                        # noqa: BLE001
        print("map %s: Git-Commit fehlgeschlagen: %s" % (map_type, e), flush=True)

# Fallback-Geräteverzeichnis (Name/Typ/Gruppe/Erfassungsbereich), falls
# xComDef6_3.h nicht erreichbar ist. Führend ist IMMER die live geparste
# xComDef6_3.h (s.u.). Erfassungsbereich = (links Grad, rechts Grad, Reichweite mm).
# Namen identisch zur device[]-Tabelle in xComDef6_3.h halten!
FALLBACK_DEVICES = {
    0:("Manager_Dev","MananagementDevice",0,0,0,0), 1:("Dome","HLK",3,-60,60,7000),
    2:("Mini_Dome","HLK",2,-60,60,7000), 3:("Compact_Dome","HLK",1,-60,60,7000),
    4:("PowerActor1","PowerActor",1,0,0,0), 5:("Disp_7","Screen",0,0,0,0),
    6:("CYD","Controller",0,0,0,0), 7:("LD6","Lidar",1,-180,180,12000),
    8:("Button","onOffSchalter",0,0,0,0), 9:("Disp_5","Screen",0,0,0,0),
    10:("Core2","Screen",0,0,0,0), 11:("Tab5","Screen",0,0,0,0),
    12:("CYD35Zoll","Screen",0,0,0,0), 13:("Wavetec_7inch","Screen",0,0,0,0),
    14:("Simulator","MananagementDevice",0,0,0,0), 15:("Laser_Marker","Marker",0,0,0,0),
    16:("PowerActor1_1","PowerActor",2,0,0,0), 17:("LidarC1","Lidar",0,-180,180,12000),
    18:("Cat_Identifier","Detector",0,0,0,0), 19:("Cat_Cam","Kamera",0,-31,31,8000),
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
_events_base = 0                 # ABSOLUTER Index des ersten RAM-Events: /events?since=N
                                 # arbeitet mit absoluten Indizes, damit das Protokoll auch
                                 # nach dem Trimmen auf MAX_EVENTS weiterlaeuft (vorher fror
                                 # die Live-Ansicht ein, sobald der Puffer voll war)
_reset_seq = 0
_settings = {}                   # sender -> {sup, val, act, t}  (aus settingsReport)
_poses = {}                      # sender -> {x, y, head, mir, valid, t}  (aus poseReport;
                                 # nur GÜLTIGE Posen, in SQLite persistiert — ein Sensor,
                                 # der nach Reboot noch nicht re-validiert ist, steht
                                 # physisch ja weiterhin am selben Ort)

def _sysmsg(msg):
    """VPS-eigene Meldung ins scrollende Debug-Fenster stellen (wie Bus-Debug).
    Damit sieht der Benutzer direkt, ob eine Aktion (Coverage lernen/loeschen,
    manuelle Pose) tatsaechlich gegriffen hat."""
    line = "VPS: " + str(msg)
    with _lock:
        _debug.append({"t": time.time(), "msg": line[:200]})
    print(line, flush=True)


_cmd_lock = threading.Lock()
_cmd_queue = []                  # [(target, cmd, info)]  Webinterface -> Manager -> Bus

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
    # fertige Tracks des kontinuierlichen Analysierers: id = fortlaufende
    # Track-Nummer, key = stabiler Schlüssel, data = volle Features als JSON
    _db.execute("""CREATE TABLE IF NOT EXISTS tracks(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        key TEXT UNIQUE, t0 REAL, t1 REAL, confirmed INT, score INT,
        data TEXT, updated REAL)""")
    _db.execute("CREATE INDEX IF NOT EXISTS idx_tracks_t ON tracks(t0,t1)")
    # Klebungen: vom Menschen zusammengefügte Tracks (JSON-Liste der keys)
    _db.execute("""CREATE TABLE IF NOT EXISTS merges(
        id INTEGER PRIMARY KEY AUTOINCREMENT, keys TEXT, created REAL)""")
    # erkannte Sturm-/Burst-Fenster (vom Analysierer, je Chunk fortgeschrieben)
    _db.execute("""CREATE TABLE IF NOT EXISTS storm_iv(
        sender INT, t0 REAL, t1 REAL)""")
    # Fotos der Cat Cam (Dateien liegen unter PHOTOS_DIR, Metadaten hier)
    _db.execute("""CREATE TABLE IF NOT EXISTS photos(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        t REAL, sender INT, trig TEXT, score INT, x INT, y INT, file TEXT)""")
    os.makedirs(PHOTOS_DIR, exist_ok=True)
    # Aufnahme-Abschnitte: wo wurde tatsächlich aufgezeichnet? (für die
    # Zeitleiste — Lücken = Pause/Container-Downtime sichtbar machen)
    _db.execute("""CREATE TABLE IF NOT EXISTS recspans(
        id INTEGER PRIMARY KEY AUTOINCREMENT, t0 REAL, t1 REAL)""")
    # gelernte, posegebundene Welt-Polygone je Sensor-Erfassungsbereich
    _db.execute("""CREATE TABLE IF NOT EXISTS coverage_polygons(
        sender INT PRIMARY KEY, active INT, pose_x INT, pose_y INT,
        pose_head REAL, pose_mir INT, pose_hash TEXT, polygon_json TEXT,
        point_count INT, source_t0 REAL, source_t1 REAL, quality REAL,
        created REAL, note TEXT)""")
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


def map_mirror_rings(map_type):
    """Aktuelle Ringe eines Kartentyps aus dem Manager-Spiegel (leer = noch nichts
    bestaetigt - kein GitHub-Fallback mehr, siehe /mapsync und _map_mirror oben)."""
    with _map_mirror_lock:
        return list(_map_mirror.get(map_type, {}).get("rings", []))


def map_status(map_type):
    """Ringe + Version + Pending-Status - fuer den Editor (Statuszeile "wartet auf
    Manager..." -> "uebernommen als vN"), siehe analysis.js anaMapEditPoll()."""
    with _map_mirror_lock:
        m = _map_mirror.get(map_type, {})
        rings, version = list(m.get("rings", [])), int(m.get("version", 0))
    with _map_pending_lock:
        pending = map_type in _map_pending
    return {"rings": rings, "version": version, "pending": pending}


app = Flask(__name__, static_folder="static")


@app.after_request
def no_cache(resp):
    # Browser nie cachen lassen -> Live-Daten + immer aktuelle Seite.
    # AUSNAHME /photo/<id>: Fotos sind unveraenderlich - ohne Cache laedt der
    # Bilder-Tab bei jedem 15-s-Refresh alle Thumbnails neu (Flackern+Bandbreite).
    if request.path.startswith("/photo/"):
        return resp                          # max_age aus send_from_directory gilt
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
            # dz = im Sensor eingestellte Totzone (mm, aus radarHbPayload; 0 = keine)
            _devices[sid] = {"t": now, "ip": int(h.get("ip", 0)),
                             "dz": float(h.get("dz", 0) or 0)}
        for s in data.get("settings", []):
            sid = int(s.get("sender", -1))
            _settings[sid] = {"sup": int(s.get("sup", 0)), "val": int(s.get("val", 0)),
                              "act": int(s.get("act", 0)), "t": now}
        pose_rows = []
        invalid_pose_sids = []
        for p in data.get("poses", []):
            sid = int(p.get("sender", -1))
            if sid < 0:
                continue
            if int(p.get("valid", 0)) != 1:
                _poses.pop(sid, None)
                invalid_pose_sids.append(sid)
                continue
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
            cut = len(_events) - MAX_EVENTS
            del _events[0:cut]
            global _events_base
            _events_base += cut
    dropped = int(data.get("dropped", 0) or 0)
    if pose_rows or invalid_pose_sids:
        with _db_lock:
            if pose_rows:
                _db.executemany("INSERT OR REPLACE INTO poses(sender,x,y,head,mir,t) "
                                "VALUES(?,?,?,?,?,?)", pose_rows)
                for sid, x, y, head, mir, _t in pose_rows:
                    ph = catmodel.pose_hash({"x": x, "y": y, "head": head, "mir": mir, "valid": 1})
                    _db.execute("UPDATE coverage_polygons SET active=0,note=? "
                                "WHERE sender=? AND active=1 AND pose_hash<>?",
                                ("Pose geaendert -> Polygon deaktiviert", sid, ph))
            for sid in invalid_pose_sids:
                _db.execute("DELETE FROM poses WHERE sender=?", (sid,))
                _db.execute("UPDATE coverage_polygons SET active=0,note=? WHERE sender=?",
                            ("Pose geloescht/ungueltig -> Polygon deaktiviert", sid))
            _db.commit()
        _coverage_dirty()
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
    # Leichter Kartenstand-Heartbeat (Version/CRC, kein Body - siehe gwFlush im
    # Manager): weicht er vom Spiegel ab (z.B. nach einem Notweg-Flash, oder der
    # Spiegel ist noch leer), stoesst der VPS per cmdMapPush ueber /commands einen
    # Re-Sync an (der Manager postet dann die volle CSV an /mapsync). Anti-Spam:
    # hoechstens alle 60s pro Kartentyp, sonst wuerde jeder gwFlush (1,5s) erneut anstossen.
    for mp in data.get("maps", []):
        typ = mp.get("type")
        if typ not in MAP_TYPE_ID:
            continue
        version = int(mp.get("version", 0) or 0)
        if not version:
            continue                                       # Manager hat selbst noch keine Karte
        crc = int(mp.get("crc", 0) or 0)
        with _map_mirror_lock:
            mirrored = _map_mirror.get(typ)
        if mirrored and mirrored.get("version") == version and mirrored.get("crc") == crc:
            continue                                        # Spiegel schon aktuell
        if now - _map_resync_requested.get(typ, 0) < 60:
            continue
        _map_resync_requested[typ] = now
        with _cmd_lock:
            _cmd_queue.append((MANAGER_TARGET, MAP_CMD_PUSH, MAP_TYPE_ID[typ]))
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
    # since = ABSOLUTER Event-Index (base im letzten Response + Anzahl gelieferter
    # Events). Liegt er vor dem RAM-Fenster (getrimmt) oder dahinter (Server-
    # Neustart), wird auf den Fensteranfang zurueckgesetzt - der Client erkennt
    # das an base < since und ersetzt seine Ansicht.
    since = int(request.args.get("since", "0"))
    with _lock:
        total = _events_base + len(_events)
        if since < _events_base or since > total:
            since = _events_base
        lo = since - _events_base
        return jsonify(reset_seq=_reset_seq, base=since, total=total,
                       events=_events[lo:lo + 5000])


@app.post("/reset")
def reset():
    global _reset_seq, _events_base
    with _lock:
        _events.clear()
        _events_base = 0
        _reset_seq += 1
    return jsonify(ok=True, reset_seq=_reset_seq)


@app.get("/map")
def get_map():
    # Weltkarten-Hintergrund (Rasen-Umriss): Spiegel vom Manager (Welt-mm -> Meter
    # fuers bestehende Frontend-Format; einzelne Punkte, keine Ringlinien - das
    # Frontend zeichnet sie ohnehin nur als Punktwolke, siehe index.html). Ringe
    # werden dafuer flach zusammengefasst (Loecher sind fuer den Hintergrund
    # irrelevant); die strukturierte Variante liefert /noshot fuer den Editor.
    rings = map_mirror_rings("rasen")
    pts = [[x / 1000.0, y / 1000.0] for ring in rings for x, y in ring]
    return jsonify(points=pts)


@app.get("/noshot")
def get_noshot():
    # No-Shot-Karte (erlaubte Schusszone) als Ringe in Welt-mm - Spiegel vom Manager,
    # plus version/pending fuer die Editor-Statuszeile (siehe map_status()).
    return jsonify(**map_status("noshot"))


@app.get("/rasenmap")
def get_rasenmap():
    # RasenKarte (Relevanzfilter) als Ringe in Welt-mm - fuer den Karten-Editor
    # (derselbe Editor bearbeitet NoShot UND Rasen, siehe analysis.js).
    return jsonify(**map_status("rasen"))


@app.get("/maps/<typ>")
def maps_get(typ):
    # Regulaerer Weg: der Manager holt eine vom Editor gespeicherte pending Karte
    # ab (ausgeloest durch cmdMapFetch aus /commands). 204 = nichts pending -
    # das ist der Normalfall, kein Fehler.
    if typ not in MAP_TYPE_ID:
        return "", 404
    with _map_pending_lock:
        p = _map_pending.get(typ)
    if not p:
        return "", 204
    return p["csv"], 200, {"Content-Type": "text/plain; charset=utf-8"}


@app.post("/maps/<typ>")
def maps_post(typ):
    # Editor "Speichern": Ring-Rohdaten (OHNE Versions-Header - den vergibt
    # ausschliesslich der Manager) als pending ablegen und den Manager beim
    # naechsten /commands-Poll zum Abholen auffordern.
    if typ not in MAP_TYPE_ID:
        return jsonify(error="unbekannter Kartentyp"), 404
    d = request.get_json(force=True, silent=True) or {}
    rings = d.get("rings") or []
    if not rings or not all(isinstance(r, list) and len(r) >= 3 for r in rings):
        return jsonify(error="jeder Ring braucht mindestens 3 Punkte"), 400
    lines = []
    for i, ring in enumerate(rings):
        if i:
            lines.append("")
        for pt in ring:
            lines.append("%d,%d" % (int(round(float(pt[0]))), int(round(float(pt[1])))))
    csv_text = "\n".join(lines) + "\n"
    with _map_pending_lock:
        _map_pending[typ] = {"csv": csv_text, "t": time.time()}
    with _cmd_lock:
        _cmd_queue.append((MANAGER_TARGET, MAP_CMD_FETCH, MAP_TYPE_ID[typ]))
    _sysmsg("Karte %s: Aenderung gespeichert, wartet auf Manager" % typ)
    return jsonify(ok=True, pending=True)


@app.post("/mapsync")
def mapsync():
    # Manager meldet die aktive Karte (gleich welcher Herkunft - VPS-Weg oder
    # Notweg-Flash): Version/CRC in der Query, CSV-Text im Body. Der VPS
    # uebernimmt sie in den Spiegel und committet sie bei Abweichung ins Repo.
    typ = request.args.get("type", "")
    if typ not in MAP_TYPE_ID:
        return jsonify(error="unbekannter Kartentyp"), 404
    version = request.args.get("version", type=int) or 0
    crc = request.args.get("crc", type=int) or 0
    csv_text = request.get_data(as_text=True) or ""
    rings = _parse_map_csv(csv_text)

    with _map_mirror_lock:
        old = _map_mirror.get(typ)
        changed = not old or old.get("version") != version or old.get("crc") != crc
        _map_mirror[typ] = {"version": version, "crc": crc, "csv": csv_text,
                             "rings": rings, "t": time.time()}
    with _map_pending_lock:
        was_pending = _map_pending.pop(typ, None) is not None
    if was_pending:
        _sysmsg("Karte %s: uebernommen als v%d" % (typ, version))
    if changed:
        # Source of truth (Controller/Manager6_3_0/data/) UND Backup-Kopie
        # (Map/backup/) committen - zwei separate Contents-API-Calls, da die
        # GitHub-API immer nur eine Datei pro Aufruf schreibt.
        threading.Thread(target=_github_commit_map,
                          args=(typ, csv_text, version, MAP_SOURCE_PATH.get(typ)), daemon=True).start()
        threading.Thread(target=_github_commit_map,
                          args=(typ, csv_text, version, MAP_BACKUP_PATH.get(typ)), daemon=True).start()
    return jsonify(ok=True)



@app.get("/manual_data")
def manual_data():
    # Kompakter Snapshot fuer die manuelle Kalibrationsseite: aktive/steuerbare
    # Sensoren, bekannte gueltige Posen und zuletzt gepufferte Treffer.
    devices = load_devices()
    now = time.time()
    with _lock:
        active = set(_devices.keys()) | set(_settings.keys())
        devs = {}
        for sid in sorted(set(devices.keys()) | active | set(_poses.keys())):
            d = devices.get(sid, {})
            hb = _devices.get(sid)
            st = _settings.get(sid)
            devs[str(sid)] = {
                "name": d.get("name", dev_name(sid)),
                "type": d.get("type", ""),
                "group": d.get("group", 0),
                "active": bool(hb and now - hb["t"] <= HB_WINDOW_S),
                "ip": hb.get("ip", 0) if hb else 0,
                "settings": {"sup": st["sup"], "val": st["val"], "act": st["act"]} if st else None,
                "pose": _poses.get(sid),
            }
        # nur die juengsten Events: die Kalibrieransicht nutzt ohnehin nur die
        # letzten Punktwolken - alle 20k RAM-Events waeren mehrere MB pro Aufruf
        evs = list(_events[-4000:])
        poses = dict(_poses)
    # Manuell kalibrierbar = die HLK-Radarmodule (Dome/Mini_Dome/Compact_Dome).
    # Sie tragen in xComDef den Typ "HLK" (nicht "Radar") und koennen sich – anders
    # als der Lidar – nicht selbst per VPS lokalisieren, brauchen also eine manuelle
    # oder kopierte Welt-Pose.
    calibratable = [int(sid) for sid, d in devs.items() if "HLK" in d.get("type", "")]
    return jsonify(now=now, devices=devs, poses={str(k): v for k, v in poses.items()},
                   events=evs, calibratable=calibratable)


@app.post("/manual_pose")
def manual_pose():
    # Sendet eine manuell ausgerichtete Pose ueber den Manager an den Sensor.
    # Die gemeinsame xCom-Schicht speichert sie in derselben worldPose-Struktur
    # wie automatische /localize- oder /calibrate-Ergebnisse: Pose ist Pose.
    d = request.get_json(force=True, silent=True) or {}
    target = int(d.get("target", -1))
    if target < 0:
        return jsonify(error="target fehlt"), 400
    x = int(round(float(d.get("x", 0))))
    y = int(round(float(d.get("y", 0))))
    heading_deg = float(d.get("heading_deg", 0.0)) % 360.0
    heading_pa = int(round(heading_deg * 4096.0 / 360.0)) % 4096
    mirror = -1 if int(d.get("mirror", 1)) < 0 else 1
    with _db_lock:
        _db.execute("UPDATE coverage_polygons SET active=0,note=? WHERE sender=?",
                    ("manuelle Pose gesendet -> neu lernen", target))
        _db.commit()
    _coverage_dirty()
    cmds = [(target, 19, 0), (target, 24, x), (target, 25, y),
            (target, 26, heading_pa), (target, 27, mirror), (254, 0, 0)]
    with _cmd_lock:
        _cmd_queue.extend(cmds)
        n = len(_cmd_queue)
    _sysmsg("Manuelle Pose an #%d (%s) gesendet: x=%d y=%d head=%.1f mir=%+d (gelerntes Polygon deaktiviert)"
            % (target, dev_name(target), x, y, heading_deg, mirror))
    return jsonify(ok=True, queued=n, commands=len(cmds), target=target, x=x, y=y,
                   heading_deg=heading_deg, heading_pa=heading_pa, mirror=mirror)

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



def _coverage_dirty():
    global _cov, _cov_ts, _cov_key
    with _cov_lock:
        _cov = None
        _cov_ts = 0.0
        _cov_key = None


def _active_coverage_profiles():
    with _db_lock:
        rows = _db.execute("""SELECT sender,active,pose_x,pose_y,pose_head,pose_mir,
                            pose_hash,polygon_json,point_count,source_t0,source_t1,
                            quality,created,note FROM coverage_polygons""").fetchall()
    out = {}
    for r in rows:
        try:
            poly = json.loads(r[7] or "[]")
        except Exception:  # noqa: BLE001
            poly = []
        out[int(r[0])] = {"sender": int(r[0]), "active": int(r[1]),
                          "pose": {"x": r[2], "y": r[3], "head": r[4], "mir": r[5], "valid": 1},
                          "pose_hash": r[6], "polygon": poly, "point_count": r[8],
                          "source_t0": r[9], "source_t1": r[10], "quality": r[11],
                          "created": r[12], "note": r[13] or ""}
    return out


def _coverage_polygons_for_current(devices, poses):
    profiles = _active_coverage_profiles()
    polys, sources = {}, {}
    for sid, dev in (devices or {}).items():
        pose = (poses or {}).get(sid) or (poses or {}).get(str(sid))
        if not pose or not pose.get("valid"):
            continue
        ph = catmodel.pose_hash(pose)
        prof = profiles.get(int(sid))
        if prof and prof["active"] and prof["pose_hash"] == ph and len(prof["polygon"]) >= 3:
            polys[int(sid)] = prof["polygon"]
            sources[str(sid)] = "learned"
            continue
        poly = catmodel.sector_polygon(dev, pose)
        if poly:
            polys[int(sid)] = poly
            sources[str(sid)] = "default"
    return polys, sources


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
    polys, sources = _coverage_polygons_for_current(devices, poses)
    if polys:
        cov = catmodel.build_coverage_polygons(polys, p["cov_cell_mm"],
                                               "polygon" if all(v == "learned" for v in sources.values()) else "mixed")
        cov["polygon_sources"] = sources
        return cov
    return _empirical_coverage(p)



@app.get("/coverage_profiles")
def coverage_profiles():
    devices = load_devices()
    with _lock:
        poses = dict(_poses)
    profs = _active_coverage_profiles()
    for sid, pr in profs.items():
        cur = poses.get(sid)
        pr["pose_matches"] = bool(cur and catmodel.pose_hash(cur) == pr["pose_hash"])
        pr["device"] = devices.get(sid, {})
    return jsonify(profiles={str(k): v for k, v in profs.items()},
                   poses={str(k): v for k, v in poses.items()})


@app.post("/coverage_learn")
def coverage_learn():
    d = request.get_json(force=True, silent=True) or {}
    sid = int(d.get("sender", -1))
    if sid < 0:
        return jsonify(error="sender fehlt"), 400
    try:
        t0, t1 = float(d["t0"]), float(d["t1"])
    except (KeyError, TypeError, ValueError):
        return jsonify(error="t0/t1 fehlen"), 400
    save = bool(d.get("save", False))
    name = dev_name(sid)
    with _lock:
        pose = dict(_poses.get(sid) or {})
    if not pose or not pose.get("valid"):
        _sysmsg("Coverage lernen #%d (%s) abgebrochen: keine gueltige Welt-Pose" % (sid, name))
        return jsonify(error="Sensor hat keine gueltige Pose"), 400
    with _db_lock:
        rows = _db.execute("SELECT wx,wy FROM events WHERE sender=? AND wv=1 AND t>=? AND t<=?",
                           (sid, t0, t1)).fetchall()
    pts = [(x, y) for x, y in rows]
    poly, q = catmodel.learn_mower_polygon(
        pts, pose,
        angle_bin_deg=float(d.get("angle_bin_deg", 10)),
        quantile=float(d.get("quantile", 0.90)),
        min_points=int(d.get("min_points", 40)),
        target_vertices=int(d.get("target_vertices", 8)))
    win = "%s-%s" % (time.strftime("%H:%M:%S", time.localtime(t0)),
                     time.strftime("%H:%M:%S", time.localtime(t1)))
    if not poly:
        _sysmsg("Coverage lernen #%d (%s) FEHLGESCHLAGEN: %s (%d Punkte, Fenster %s)"
                % (sid, name, q.get("reason", "unbekannt"), len(pts), win))
        return jsonify(ok=False, error=q.get("reason", "Polygon konnte nicht erzeugt werden"),
                       quality=q, point_count=len(pts)), 400
    ph = catmodel.pose_hash(pose)
    area_m2 = float(q.get("area_mm2", 0.0)) / 1e6
    if save:
        with _db_lock:
            _db.execute("""INSERT OR REPLACE INTO coverage_polygons
                (sender,active,pose_x,pose_y,pose_head,pose_mir,pose_hash,polygon_json,
                 point_count,source_t0,source_t1,quality,created,note)
                VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                (sid, 1, pose["x"], pose["y"], pose["head"], pose["mir"], ph,
                 json.dumps(poly, separators=(",", ":")), len(pts), t0, t1,
                 float(q.get("area_mm2", 0.0)), time.time(), "aus Maeherperiode gelernt"))
            _db.commit()
        _coverage_dirty()
        _sysmsg("Coverage GELERNT + aktiv: #%d (%s) - %d Ecken, %d Punkte, %.1f m2, Fenster %s"
                % (sid, name, len(poly), len(pts), area_m2, win))
    else:
        _sysmsg("Coverage-Vorschau #%d (%s): %d Ecken, %d Punkte, %.1f m2 (noch nicht gespeichert)"
                % (sid, name, len(poly), len(pts), area_m2))
    return jsonify(ok=True, saved=save, sender=sid, pose_hash=ph, polygon=poly,
                   quality=q, point_count=len(pts), t0=t0, t1=t1)


@app.post("/coverage_delete")
def coverage_delete():
    d = request.get_json(force=True, silent=True) or {}
    sid = int(d.get("sender", -1))
    if sid < 0:
        return jsonify(error="sender fehlt"), 400
    with _db_lock:
        cur = _db.execute("UPDATE coverage_polygons SET active=0,note=? WHERE sender=? AND active=1",
                          ("manuell deaktiviert", sid))
        n = cur.rowcount
        _db.commit()
    _coverage_dirty()
    if n:
        _sysmsg("Coverage GELOESCHT: #%d (%s) deaktiviert -> wieder Default-Sektor"
                % (sid, dev_name(sid)))
    else:
        _sysmsg("Coverage loeschen #%d (%s): kein aktives Polygon vorhanden"
                % (sid, dev_name(sid)))
    return jsonify(ok=True, sender=sid, deactivated=n)


@app.get("/coverage_export")
def coverage_export():
    p = catmodel.merged_params(load_params())
    devices = load_devices()
    with _lock:
        poses = dict(_poses)
    polys, sources = _coverage_polygons_for_current(devices, poses)
    return jsonify(version=1, cell_mm=p["cov_cell_mm"],
                   polygons=[{"sender": sid, "source": sources.get(str(sid), ""),
                              "pose_hash": catmodel.pose_hash(poses.get(sid) or poses.get(str(sid))),
                              "points": poly}
                             for sid, poly in sorted(polys.items())])



@app.get("/coverage_export.csv")
def coverage_export_csv():
    # Kompakter ESP32-Export: sender,source,n,x1,y1,x2,y2,...
    # Der CatIdentifier nutzt eine Liste von Polygonen (inside-any), keine Union.
    p = catmodel.merged_params(load_params())
    devices = load_devices()
    with _lock:
        poses = dict(_poses)
    polys, sources = _coverage_polygons_for_current(devices, poses)
    lines = ["# CatFinder Coverage-Polygone v1", "# sender,source,n,x1,y1,..."]
    for sid, poly in sorted(polys.items()):
        # ESP32-Budget: max. 16 Ecken - VEREINFACHEN statt abschneiden (ein hartes
        # poly[:16] machte den 360-Grad-Lidar-Sektor zum Halbkreis).
        pts = poly if len(poly) <= 16 else catmodel.simplify_polygon(poly, 6, 16)
        fields = [str(sid), sources.get(str(sid), ""), str(len(pts))]
        for x, y in pts:
            fields += [str(int(x)), str(int(y))]
        lines.append(",".join(fields))
    return Response("\n".join(lines) + "\n", mimetype="text/plain; charset=utf-8")

def _dead_zone_sectors():
    # Totzonen der Radarsensoren (dz aus dem HB, via Manager) als Sektoren in
    # Weltkoordinaten - fürs Karten-Overlay, damit man die eingestellte Totzone
    # beim Justieren sieht. Bewusst NICHT aus den Coverage-Polygonen/-Zellen
    # ausgestanzt (fürs Erkennungsmodell/CatIdent ohne Belang).
    devices = load_devices()
    with _lock:
        poses = dict(_poses)
        devs_rt = {sid: dict(d) for sid, d in _devices.items()}
    out = {}
    for sid, d in devs_rt.items():
        dz = float(d.get("dz", 0) or 0)
        if dz <= 0:
            continue
        pose = poses.get(sid) or poses.get(str(sid))
        dev = devices.get(sid) or devices.get(str(sid))
        if not pose or not pose.get("valid") or not dev:
            continue
        out[str(sid)] = {"x": float(pose["x"]), "y": float(pose["y"]),
                         "head": float(pose.get("head", 0)),
                         "mir": -1 if int(pose.get("mir", 1)) < 0 else 1,
                         "left": float(dev.get("covL", -180)),
                         "right": float(dev.get("covR", 180)),
                         "range": dz}
    return out


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
                   polygons={str(k): v for k, v in cov.get("polygons", {}).items()},
                   polygon_sources=cov.get("polygon_sources", {}),
                   dead_zones=_dead_zone_sectors(),
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


# ------------------------------------------------- Kontinuierlicher Analysierer
# Die Trackerkennung läuft UNABHÄNGIG von der Betrachtung im Browser: ein
# Hintergrund-Thread arbeitet die Aufnahme fortlaufend in Häppchen ab (genau
# das Modell, das später auf dem ESP32 in Echtzeit laufen soll), vergibt
# fortlaufende Track-Nummern und legt fertige Tracks persistent in SQLite ab.
# /amodel LIEST nur noch — egal wie gross das betrachtete Fenster ist.
# Ändern sich Modell-Parameter oder „Mäher"-Fenster (-> Signatur), rechnet der
# Thread automatisch alles neu; manuelle Bewertungen und Klebungen überleben
# das über den stabilen Track-key (Geburtszeit+Geburtsort).

ANA_CHUNK    = 80000     # Events je Analyse-Häppchen (Backfill)
ANA_SAFE_GAP = 8.0       # s Ruhe > track_gap+stitch_gap -> sichere Schnittstelle
ANA_HOLD     = 12.0      # s: jüngste Daten zurückhalten, bis Tracks fertig sein können
ANA_STALL_S  = 21600     # Notbremse: ewig offener Track wird trotzdem festgeschrieben

_ana_lock  = threading.Lock()
_ana_prov  = []          # provisorische (noch offene) Tracks — nur RAM
_ana_state = {"done_t": None, "backlog": False, "err": ""}


def _meta_get(k, default=None):
    row = _db.execute("SELECT v FROM meta WHERE k=?", (k,)).fetchone()
    return row[0] if row else default


def _meta_set(k, v):
    _db.execute("INSERT OR REPLACE INTO meta(k,v) VALUES(?,?)", (k, str(v)))


def _model_sig():
    """Signatur von allem, was das Modellergebnis beeinflusst: Parameter +
    „Mäher"-Ausschlussfenster. Ändert sie sich -> kompletter Neuaufbau."""
    p = catmodel.merged_params(load_params())
    with _db_lock:
        mow = _db.execute("SELECT t0,t1 FROM labels WHERE label='Mäher' "
                          "ORDER BY t0").fetchall()
    return hashlib.md5(json.dumps([p, mow], sort_keys=True).encode()).hexdigest()


def _analyzer_step():
    """Ein Analyse-Häppchen verarbeiten. True = es wartet noch Backlog."""
    global _ana_prov
    params = catmodel.merged_params(load_params())
    sig = _model_sig()
    with _db_lock:
        if _meta_get("ana_sig") != sig:
            # Parameter/Mäher-Fenster geändert -> Tracks neu rechnen, Nummern
            # neu vergeben (Bewertungen + Klebungen hängen am stabilen key)
            _db.execute("DELETE FROM tracks")
            _db.execute("DELETE FROM storm_iv")
            _db.execute("DELETE FROM sqlite_sequence WHERE name='tracks'")
            _db.execute("DELETE FROM meta WHERE k='ana_done_t'")
            _meta_set("ana_sig", sig)
            _db.commit()
            with _ana_lock:
                _ana_prov = []
            print("Analysierer: Signatur geändert — rechne alles neu", flush=True)
        done_t = _meta_get("ana_done_t")
        if done_t is None:
            row = _db.execute("SELECT MIN(t) FROM events WHERE wv=1").fetchone()
            if not row or row[0] is None:
                _ana_state.update(done_t=None, backlog=False)
                return False
            done_t = row[0] - 1.0
        else:
            done_t = float(done_t)
        hi = time.time() - ANA_HOLD
        rows = _db.execute(
            "SELECT t,sender,sensor,wx,wy,speed FROM events "
            "WHERE wv=1 AND t>? AND t<=? ORDER BY t LIMIT ?",
            (done_t, hi, ANA_CHUNK + 1)).fetchall()
    if not rows:
        with _ana_lock:
            _ana_prov = []
        _ana_state.update(done_t=done_t, backlog=False)
        return False

    backlog = len(rows) > ANA_CHUNK
    if backlog:
        rows = rows[:ANA_CHUNK]
        # am letzten ruhigen Loch abschneiden, damit kein Track zerteilt wird
        cut = len(rows) - 1
        for i in range(len(rows) - 1, 0, -1):
            if rows[i][0] - rows[i - 1][0] >= ANA_SAFE_GAP:
                cut = i - 1
                break
        rows = rows[:cut + 1]
        final_before = rows[-1][0] + 1e-3
    else:
        # aufgeholt: Tracks, deren Ende jünger als hi-SAFE_GAP ist, könnten
        # durch noch zurückgehaltene Events weiterwachsen -> provisorisch
        final_before = hi - ANA_SAFE_GAP

    evs = [{"t": r[0], "sender": r[1], "sensor": r[2], "wx": r[3], "wy": r[4],
            "speed": r[5]} for r in rows]
    with _db_lock:
        mow = _db.execute("SELECT t0,t1 FROM labels WHERE label='Mäher' "
                          "AND t1>=? AND t0<=?", (rows[0][0], rows[-1][0])).fetchall()
    res = catmodel.analyze_stream(evs, params, devices=load_devices(),
                                  coverage=get_coverage(params),
                                  exclude=[(a, b) for a, b in mow],
                                  final_before=final_before)
    prov = res["prov"]
    # bis wohin ist ENDGÜLTIG gerechnet? Offene Tracks müssen im nächsten
    # Häppchen ab ihrer Geburt nochmals rein (sie wachsen evtl. noch).
    new_done = final_before
    if prov:
        new_done = min(new_done, min(tr["t0"] for tr in prov) - 1e-3)
    if final_before - new_done > ANA_STALL_S:
        # Notbremse (z.B. stundenlang gestitchter Dauer-Track): festschreiben
        res["final"].extend(prov)
        prov = []
        new_done = final_before
    new_done = max(new_done, done_t)

    now = time.time()
    with _db_lock:
        for tr in res["final"]:
            tr.pop("id", None)                    # Nummer vergibt die DB
            _db.execute(
                """INSERT INTO tracks(key,t0,t1,confirmed,score,data,updated)
                   VALUES(?,?,?,?,?,?,?)
                   ON CONFLICT(key) DO UPDATE SET t0=excluded.t0, t1=excluded.t1,
                     confirmed=excluded.confirmed, score=excluded.score,
                     data=excluded.data, updated=excluded.updated""",
                (tr["key"], tr["t0"], tr["t1"], int(tr["confirmed"]),
                 tr["score"], json.dumps(tr, separators=(",", ":")), now))
        # Sturm-Fenster des Häppchens (Reprocessing-Duplikate vorher räumen)
        _db.execute("DELETE FROM storm_iv WHERE t1>=? AND t0<=?",
                    (rows[0][0], rows[-1][0]))
        for sid, ivs in res["storms"].items():
            _db.executemany("INSERT INTO storm_iv(sender,t0,t1) VALUES(?,?,?)",
                            [(int(sid), a, b) for a, b in ivs])
        _meta_set("ana_done_t", repr(new_done))
        _db.commit()
    for tr in prov:
        tr["id"] = None
        tr["provisional"] = True
    with _ana_lock:
        _ana_prov = prov
    _ana_state.update(done_t=new_done, backlog=backlog, err="")
    if backlog:
        print("Analysierer: %d Events bis %s verarbeitet (Backlog)" %
              (len(rows), datetime.fromtimestamp(rows[-1][0]).isoformat()), flush=True)
    return backlog


def _analyzer_loop():
    time.sleep(2.0)                    # Server erst fertig hochkommen lassen
    while True:
        try:
            more = _analyzer_step()
        except Exception as e:                                   # noqa: BLE001
            traceback.print_exc()
            _ana_state["err"] = str(e)
            more = False
        time.sleep(0.3 if more else 5.0)


# ------------------------------------------------- Tracks servieren (aus der DB)

def _combine_tracks(members, params, devices, coverage, storms=None):
    """Geklebte Tracks zu EINEM zusammenfassen und als Ganzes neu bewerten
    (so, wie das Modell den Track sähe, wäre er nie zerrissen worden).
    storms = Sturm-Fenster aus storm_iv - ohne sie zählten Sturm-Punkte, die
    in den Einzeltracks verworfen waren, nach dem Kleben plötzlich als Evidenz."""
    members = sorted(members, key=lambda m: m["t0"])
    pts = sorted([list(p) for m in members for p in m["pts"]])
    feats = catmodel.score_track(pts, storms or {}, coverage, devices, params, 0,
                                 -1e18, 1e18)
    feats["id"] = min(m["id"] for m in members)
    feats["key"] = members[0]["key"]
    feats["members"] = [{"id": m["id"], "key": m["key"]} for m in members]
    feats["flags"] = feats.get("flags", []) + ["GEKLEBT"]
    return feats


def _served_tracks(t0, t1, params, devices, coverage):
    """Tracks eines Fensters (oder alle bei t0=None) aus der DB, Klebungen
    bereits zusammengefasst (Mitglieder ersetzt durch den kombinierten Track)."""
    with _db_lock:
        if t0 is None:
            rows = _db.execute("SELECT id,key,data FROM tracks ORDER BY t0").fetchall()
        else:
            rows = _db.execute("SELECT id,key,data FROM tracks "
                               "WHERE t1>=? AND t0<=? ORDER BY t0",
                               (t0, t1)).fetchall()
        groups = _db.execute("SELECT id,keys FROM merges").fetchall()
        srows = _db.execute("SELECT sender,t0,t1 FROM storm_iv").fetchall()
    storms = {}
    for sid, a, b in srows:
        storms.setdefault(int(sid), []).append([a, b])
    tracks = {}
    for rid, key, data in rows:
        tr = json.loads(data)
        tr["id"] = rid
        tracks[key] = tr
    for gid, keys_json in groups:
        keys = json.loads(keys_json)
        present = [k for k in keys if k in tracks]
        if not present:
            continue                    # Klebung liegt ganz ausserhalb des Fensters
        missing = [k for k in keys if k not in tracks]
        if missing:
            with _db_lock:
                more = _db.execute(
                    "SELECT id,key,data FROM tracks WHERE key IN (%s)" %
                    ",".join("?" * len(missing)), missing).fetchall()
            for rid, key, data in more:
                tr = json.loads(data)
                tr["id"] = rid
                tracks[key] = tr
        members = [tracks[k] for k in keys if k in tracks]
        if len(members) < 2:
            continue
        comb = _combine_tracks(members, params, devices, coverage, storms)
        comb["merge_id"] = gid
        for m in members:
            tracks.pop(m["key"], None)
        tracks[comb["key"]] = comb
    return list(tracks.values())


@app.get("/amodel")
def amodel():
    # Tracks eines Zeitfensters aus der DB des kontinuierlichen Analysierers
    # (plus noch offene provisorische) — hier wird NICHTS mehr gerechnet, die
    # Erkennung läuft unabhängig von der Betrachtung im Hintergrund.
    t0, t1 = _win_args()
    if t0 is None or t1 is None:
        return jsonify(error="t0/t1 fehlen"), 400
    params = catmodel.merged_params(load_params())
    devices = load_devices()
    cov = get_coverage(params)
    tracks = _served_tracks(t0, t1, params, devices, cov)
    have = {t["key"] for t in tracks}
    with _ana_lock:
        tracks += [dict(tr) for tr in _ana_prov
                   if tr["t1"] >= t0 and tr["t0"] <= t1 and tr["key"] not in have]
    n_confirmed = sum(1 for t in tracks if t["confirmed"])
    truncated = 0
    if len(tracks) > 600:               # Riesenfenster: wichtigste zuerst behalten
        tracks.sort(key=lambda t: (t["confirmed"], t["score"], t["n"]), reverse=True)
        truncated = len(tracks) - 600
        tracks = tracks[:600]
    tracks.sort(key=lambda t: t["t0"])
    with _db_lock:
        n_events = _db.execute(
            "SELECT COUNT(*) FROM events WHERE t>=? AND t<=? AND wv=1",
            (t0, t1)).fetchone()[0]
        mow = _db.execute("SELECT t0,t1 FROM labels WHERE label='Mäher' "
                          "AND t1>=? AND t0<=?", (t0, t1)).fetchall()
        n_excl = 0
        for a, b in mow:
            n_excl += _db.execute(
                "SELECT COUNT(*) FROM events WHERE t>=? AND t<=? AND wv=1",
                (max(a, t0), min(b, t1))).fetchone()[0]
        srows = _db.execute("SELECT sender,t0,t1 FROM storm_iv "
                            "WHERE t1>=? AND t0<=?", (t0, t1)).fetchall()
        marks = _db.execute("SELECT key,mark FROM track_marks").fetchall()
    storms = {}
    for sid, a, b in srows:
        storms.setdefault(str(sid), []).append([a, b])
    done_t = _ana_state.get("done_t")
    return jsonify(
        t0=t0, t1=t1, tracks=tracks, storms=storms,
        n_events=n_events, n_excluded=n_excl, n_confirmed=n_confirmed,
        edge_active=len(cov.get("union", ())) >= params["cov_min_cells"],
        cov_source=cov.get("source", ""),
        params=params,
        devices={str(k): {"name": v["name"], "type": v["type"],
                          "covL": v.get("covL", 0), "covR": v.get("covR", 0),
                          "covRange": v.get("covRange", 0)}
                 for k, v in devices.items()},
        marks={k: m for k, m in marks},
        ana={"done_t": done_t, "backlog": _ana_state["backlog"],
             "err": _ana_state["err"], "truncated": truncated})


@app.post("/amerge")
def amerge():
    # zwei oder mehr Tracks zusammenkleben (gehören zum selben Tier). Berührt
    # eine Auswahl eine bestehende Klebung, wird sie mit ihr vereinigt.
    d = request.get_json(force=True, silent=True) or {}
    ids = [int(i) for i in d.get("ids", [])]
    if len(ids) < 2:
        return jsonify(error="mindestens 2 Track-Nummern nötig"), 400
    with _db_lock:
        rows = _db.execute("SELECT key FROM tracks WHERE id IN (%s)" %
                           ",".join("?" * len(ids)), ids).fetchall()
        if len(rows) < 2:
            return jsonify(error="Tracks nicht (mehr) gefunden — Modell neu geladen?"), 404
        keys = {r[0] for r in rows}
        for gid, kj in _db.execute("SELECT id,keys FROM merges").fetchall():
            ks = set(json.loads(kj))
            if ks & keys:
                keys |= ks
                _db.execute("DELETE FROM merges WHERE id=?", (gid,))
        cur = _db.execute("INSERT INTO merges(keys,created) VALUES(?,?)",
                          (json.dumps(sorted(keys)), time.time()))
        _db.commit()
    return jsonify(ok=True, merge_id=cur.lastrowid, n=len(keys))


@app.post("/aunmerge")
def aunmerge():
    d = request.get_json(force=True, silent=True) or {}
    with _db_lock:
        _db.execute("DELETE FROM merges WHERE id=?", (int(d.get("merge_id", -1)),))
        _db.commit()
    return jsonify(ok=True)


def _all_tracks_with_marks():
    params = catmodel.merged_params(load_params())
    devices = load_devices()
    cov = get_coverage(params)
    tracks = _served_tracks(None, None, params, devices, cov)
    with _db_lock:
        marks = dict(_db.execute("SELECT key,mark FROM track_marks").fetchall())
    for tr in tracks:
        tr["mark"] = marks.get(tr["key"], "")
    tracks.sort(key=lambda t: t["t0"])
    return tracks


@app.get("/atracks")
def atracks():
    # komplette Trackliste (inkl. Bewertungen/Klebungen) als JSON — separat
    # verwendbar, z.B. um das Modell gegen die markierten Tracks zu prüfen.
    # ?pts=1 liefert auch die Punktfolgen (gross!).
    tracks = _all_tracks_with_marks()
    if request.args.get("pts") != "1":
        for tr in tracks:
            tr.pop("pts", None)
    return jsonify(n=len(tracks), tracks=tracks)


@app.get("/atracks.csv")
def atracks_csv():
    def iso(t):
        return datetime.fromtimestamp(t).strftime("%Y-%m-%d %H:%M:%S")
    lines = ["Nr;key;start;ende;dauer_s;punkte;netto_mm;weg_mm;v_mittel;v_max;"
             "sender;flags;score;modell_katze;ablehnung;bewertung;klebung"]
    for tr in _all_tracks_with_marks():
        lines.append(";".join(str(x) for x in (
            tr["id"], tr["key"], iso(tr["t0"]), iso(tr["t1"]),
            round(tr["t1"] - tr["t0"], 1), tr["n"], tr["net_mm"], tr["path_mm"],
            tr["v_mean"], tr["v_max"],
            "+".join(str(s) for s in tr["senders"]),
            "+".join(tr.get("flags", [])), tr["score"],
            1 if tr["confirmed"] else 0,
            (tr.get("reject") or "").replace(";", ","), tr.get("mark", ""),
            "+".join(str(m["id"]) for m in tr.get("members", [])))))
    return Response("\n".join(lines) + "\n", mimetype="text/csv",
                    headers={"Content-Disposition":
                             "attachment; filename=catfinder_tracks.csv"})


@app.get("/avalidate")
def avalidate():
    # Modell gegen ALLE von Hand bewerteten Tracks prüfen: Übereinstimmung,
    # fälschliche Katzen (Modell ja / Mensch nein) und verpasste Katzen.
    tracks = _all_tracks_with_marks()
    marked = [t for t in tracks if t["mark"]]
    per_mark, mismatches = {}, []
    agree = false_cat = missed_cat = 0
    for t in marked:
        pm = per_mark.setdefault(t["mark"], {"n": 0, "model_cat": 0})
        pm["n"] += 1
        if t["confirmed"]:
            pm["model_cat"] += 1
        human_cat = t["mark"] == "cat"
        if human_cat == bool(t["confirmed"]):
            agree += 1
        else:
            if t["confirmed"]:
                false_cat += 1
            else:
                missed_cat += 1
            mismatches.append({"id": t["id"], "key": t["key"], "t0": t["t0"],
                               "t1": t["t1"], "mark": t["mark"],
                               "score": t["score"], "confirmed": t["confirmed"],
                               "reject": t.get("reject")})
    return jsonify(n_tracks=len(tracks), n_marked=len(marked), agree=agree,
                   false_cat=false_cat, missed_cat=missed_cat,
                   per_mark=per_mark, mismatches=mismatches)


# ---------------------------------------------------------------- Fotos (Cat Cam)

def _local_minute_of_day(epoch):
    # Minuten seit lokaler Mitternacht (DST-korrekt via zoneinfo, sonst OS-lokal).
    if _TZ is not None:
        lt = datetime.fromtimestamp(epoch, _TZ)
        return lt.hour * 60 + lt.minute
    lt = time.localtime(epoch)
    return lt.tm_hour * 60 + lt.tm_min


def _hhmm_to_min(s):
    h, m = str(s).split(":")
    return int(h) * 60 + int(m)


def _photo_windows():
    # Liste der täglichen Bilder-Sperrzeiten [{"start":"HH:MM","end":"HH:MM"}].
    with _db_lock:
        raw = _meta_get("photo_block")
    if not raw:
        return []
    try:
        w = json.loads(raw)
        return w if isinstance(w, list) else []
    except Exception:                                        # noqa: BLE001
        return []


def _photo_blocked_window(epoch, windows=None):
    # Liefert das zutreffende Sperrfenster (oder None). end<=start = über Mitternacht.
    if windows is None:
        windows = _photo_windows()
    if not windows:
        return None
    mod = _local_minute_of_day(epoch)
    for w in windows:
        try:
            a, b = _hhmm_to_min(w["start"]), _hhmm_to_min(w["end"])
        except Exception:                                    # noqa: BLE001
            continue
        if a == b:
            continue
        if (a <= mod < b) if a < b else (mod >= a or mod < b):
            return w
    return None


@app.get("/photo_block")
def photo_block_get():
    # Aktuelle Sperrzeiten + lokale Uhrzeit + ob gerade gesperrt (für die UI).
    wins = _photo_windows()
    now = time.time()
    mod = _local_minute_of_day(now)
    return jsonify(windows=wins, tz=LOCAL_TZ,
                   now_local="%02d:%02d" % (mod // 60, mod % 60),
                   blocked=bool(_photo_blocked_window(now, wins)))


@app.post("/photo_block")
def photo_block_set():
    # Ersetzt die komplette Sperrzeiten-Liste. Body: {"windows":[{start,end,note?}]}.
    d = request.get_json(force=True, silent=True) or {}
    raw = d.get("windows", [])
    out = []
    for w in raw if isinstance(raw, list) else []:
        try:
            a, b = _hhmm_to_min(w["start"]), _hhmm_to_min(w["end"])
        except Exception:                                    # noqa: BLE001
            return jsonify(error="ungültige Zeit (HH:MM erwartet)"), 400
        if not (0 <= a < 1440 and 0 <= b < 1440):
            return jsonify(error="Zeit außerhalb 00:00–23:59"), 400
        item = {"start": "%02d:%02d" % (a // 60, a % 60),
                "end": "%02d:%02d" % (b // 60, b % 60)}
        note = str(w.get("note", ""))[:60]
        if note:
            item["note"] = note
        out.append(item)
    with _db_lock:
        _meta_set("photo_block", json.dumps(out))
        _db.commit()
    _sysmsg("Bilder-Sperrzeiten aktualisiert: " +
            (", ".join("%s-%s" % (w["start"], w["end"]) for w in out) or "keine"))
    return jsonify(ok=True, windows=out)


@app.post("/photo_del")
def photo_del():
    # Foto(s) löschen: DB-Eintrag UND Datei entfernen. Body {"id":N} oder
    # {"ids":[...]} (Mehrfachauswahl).
    d = request.get_json(force=True, silent=True) or {}
    ids = d.get("ids")
    if not isinstance(ids, list):
        ids = [d.get("id", -1)]
    try:
        ids = [int(x) for x in ids]
    except (TypeError, ValueError):
        return jsonify(error="ungültige id"), 400
    files = []
    with _db_lock:
        for pid in ids:
            row = _db.execute("SELECT file FROM photos WHERE id=?", (pid,)).fetchone()
            if row:
                files.append(row[0])
                _db.execute("DELETE FROM photos WHERE id=?", (pid,))
        _db.commit()
    for f in files:
        try:
            os.remove(os.path.join(PHOTOS_DIR, f))
        except OSError:
            pass
    if not files:
        return jsonify(error="unbekannt"), 404
    return jsonify(ok=True, deleted=len(files))


@app.post("/photo")
def photo_upload():
    # Cat Cam laedt ein Foto hoch: Query-Parameter = Metadaten, Body = Base64-JPEG
    # (das Vision-AI-Modul liefert das Bild als Base64 - der ESP32-C3 reicht es
    # unveraendert durch, dekodiert wird hier).
    if request.content_length and request.content_length > 3 * 1024 * 1024:
        return jsonify(error="zu gross"), 413
    sender = request.args.get("sender", type=int, default=-1)
    trig = str(request.args.get("trigger", "?"))[:20]
    score = request.args.get("score", type=int, default=0)
    x = request.args.get("x", type=int, default=0)
    y = request.args.get("y", type=int, default=0)
    now = time.time()
    blk = _photo_blocked_window(now)
    if blk:                              # tägliche Sperrzeit (z. B. Mäher) -> verwerfen
        return jsonify(ok=True, blocked=True, window=blk)
    try:
        raw = base64.b64decode(request.get_data(as_text=True).strip(), validate=False)
    except Exception:                                        # noqa: BLE001
        return jsonify(error="Base64 ungueltig"), 400
    if len(raw) < 100 or raw[:2] != b"\xff\xd8":
        return jsonify(error="kein JPEG"), 400
    fname = "%d_%s.jpg" % (int(now * 1000), re.sub(r"[^A-Za-z0-9]", "", trig) or "x")
    with open(os.path.join(PHOTOS_DIR, fname), "wb") as f:
        f.write(raw)
    with _db_lock:
        cur = _db.execute("INSERT INTO photos(t,sender,trig,score,x,y,file) "
                          "VALUES(?,?,?,?,?,?,?)", (now, sender, trig, score, x, y, fname))
        _db.commit()
    return jsonify(ok=True, id=cur.lastrowid, bytes=len(raw))


@app.get("/photos")
def photos_list():
    # Fotoliste (neueste zuerst) inkl. zugeordneter Track-Nummer: der Track des
    # kontinuierlichen Analysierers, der den Aufnahmezeitpunkt ueberlappt
    # (bestaetigte zuerst). KI-Fotos der Kamera haben oft keinen Radar-Track.
    limit = min(request.args.get("limit", type=int, default=120), 500)
    t0 = request.args.get("t0", type=float)
    t1 = request.args.get("t1", type=float)
    with _db_lock:
        if t0 is not None and t1 is not None:
            rows = _db.execute("SELECT id,t,sender,trig,score,x,y FROM photos "
                               "WHERE t>=? AND t<=? ORDER BY t DESC LIMIT ?",
                               (t0, t1, limit)).fetchall()
        else:
            rows = _db.execute("SELECT id,t,sender,trig,score,x,y FROM photos "
                               "ORDER BY t DESC LIMIT ?", (limit,)).fetchall()
        out = []
        for pid, t, sender, trig, score, x, y in rows:
            tr = _db.execute("SELECT id,confirmed FROM tracks WHERE t0<=? AND t1>=? "
                             "ORDER BY confirmed DESC, score DESC LIMIT 1",
                             (t + 2.0, t - 2.0)).fetchone()
            out.append({"id": pid, "t": t, "sender": sender, "trigger": trig,
                        "score": score, "x": x, "y": y,
                        "track": tr[0] if tr else None,
                        "track_cat": bool(tr[1]) if tr else False})
    return jsonify(photos=out)


@app.get("/photo/<int:pid>")
def photo_get(pid):
    with _db_lock:
        row = _db.execute("SELECT file FROM photos WHERE id=?", (pid,)).fetchone()
    if not row:
        return jsonify(error="unbekannt"), 404
    return send_from_directory(PHOTOS_DIR, row[0], mimetype="image/jpeg",
                               max_age=86400)


# erlaubte Track-Bewertungen (Ground-Truth-Kategorien)
AMARKS = ("cat", "person", "bird", "mower", "insect", "storm",
          "nocat", "surenocat")


@app.post("/amark")
def amark():
    # manuelle Bewertung eines Tracks setzen/löschen. mark: "cat" (ist Katze),
    # "person"/"bird"/"mower"/"insect"/"storm" (was es stattdessen war), "nocat"
    # (Störung/sonstiges), "surenocat" (sicher keine Katze, Ursache egal),
    # "" = Markierung entfernen (= einverstanden mit der Modellbewertung). Die
    # Modellbewertung selbst bleibt unberührt — der Vergleich Modell vs. Mensch
    # läuft im UI (Übereinstimmungs-Statistik; alles ausser "cat" zählt als
    # "keine Katze"). Die Kategorien sind gelabelte Ground-Truth fürs Iterieren.
    d = request.get_json(force=True, silent=True) or {}
    key = str(d.get("key", ""))[:80]
    if not key:
        return jsonify(error="key fehlt"), 400
    mark = str(d.get("mark", ""))
    with _db_lock:
        if mark in AMARKS:
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


@app.get("/aparams.csv")
def aparams_csv():
    # Modell-Parameter als flache key=value-Zeilen: der Cat Identifier (ESP32)
    # holt sie hier (Boot + Aktion "Parameter laden") - kein JSON-Parser noetig.
    # Verschachtelte Parameter (type_weight) werden zu "type_weight.HLK=..." usw.
    p = catmodel.merged_params(load_params())
    lines = ["# CatFinder Modell-Parameter (catmodel DEFAULT_PARAMS + Overrides)"]
    for k, v in sorted(p.items()):
        if isinstance(v, dict):
            lines += ["%s.%s=%g" % (k, kk, vv) for kk, vv in sorted(v.items())]
        else:
            lines.append("%s=%g" % (k, v))
    return Response("\n".join(lines) + "\n", mimetype="text/plain; charset=utf-8")


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
    # all=1 -> ALLE Labels (zeitfensterunabhängig, für die globale Verwaltung).
    if request.args.get("all"):
        with _db_lock:
            rows = _db.execute("SELECT id,t0,t1,sender,label,note FROM labels "
                               "ORDER BY t0").fetchall()
    else:
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
load_devices()
# kontinuierlicher Analysierer: Trackerkennung läuft ab jetzt im Hintergrund
# über die ganze Aufnahme — unabhängig davon, was im Browser betrachtet wird
threading.Thread(target=_analyzer_loop, daemon=True).start()

if __name__ == "__main__":
    from waitress import serve
    port = int(os.environ.get("PORT", "80"))
    print("CatFinder dashboard on :%d" % port, flush=True)
    serve(app, host="0.0.0.0", port=port, threads=4)
