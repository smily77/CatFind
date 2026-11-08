"""Incident-Tracer, Stufe 1: nachtraegliche Kot-Meldung ("Kot-Verdacht").

Hintergrund: Situations Dokumentation/2026-11-08.md. Die zwei sicheren Kotfaelle
(Track 81320 am 16.10., Track 83472 am 07.11.) zeigen dieselbe Spur: das Tier
kommt an, SITZT 38-56 s fast unbeweglich (Streuung wenige cm) und geht wieder.
Der CatIdent bestaetigt so etwas oft erst beim Weggehen oder gar nicht, weil
seine Pflicht "kohaerente Bewegung" beim Sitzen fehlt. Diese Pruefung laeuft
deshalb NACHTRAEGLICH auf den fertigen Tracks des VPS-Analysierers und meldet
den Sitzort, damit der User nachschauen/wegraeumen kann (und per Rueckmeldung
gefunden / nichts gefunden / nicht pruefbar das Profil schaerft).

Profil (Startwerte, alle in DEFAULT_CFG einstellbar):
  * Ketten: Tracks am selben Ort mit kurzer Pause werden zusammengenommen
    (83472 ist durch eine 17,5-s-Radarluecke in zwei Tracks zerfallen).
  * Sitzen: >= sit_s Sekunden alle Punkte in einem Kreis von sit_r_mm um den
    ersten Sitzpunkt, Streuung (Standardabweichung) <= sd_max_mm.
  * Ankunft: vor dem Sitzen mind. arrive_mm vom Sitzort entfernt gewesen.
  * Abgang: danach mind. leave_mm vom Sitzort weg.
  * Ausgeblendet: Maeher-Zeiten (taeglich, Lokalzeit), "Maeher"-Labels,
    Tracks mit Bewertung person/mower/storm/bird, Sperrstellen (Kreise, z.B.
    die Radar-Haltestelle hinten bei 7,1/11,0 m).
Duenne Radarmeldungen/Luecken beim Sitzen sind (noch) KEIN Muss, sie werden nur
als Merkmal mitgeschrieben (rate, maxgap), um spaeter zu sehen, ob sie trennen.

Bewusst ohne numpy und ohne DB-Zugriff: dashboard.py liefert Tracks und
schreibt die Ergebnisse (Tabelle incidents).
"""
import math
from datetime import datetime
from zoneinfo import ZoneInfo

TZ = ZoneInfo("Europe/Zurich")

DEFAULT_CFG = {
    "enabled": True,
    "push": True,                  # neue Verdachte per PushingBox (CatAssumed) melden
    "push_max_age_s": 3600,        # nur frische Verdachte pushen (nicht beim Nachrechnen)
    "sit_s": 20.0,
    "sit_r_mm": 600,
    "sd_max_mm": 150,
    "min_rate": 1.0,               # Meldungen/s beim Sitzen (Lidar-Einzelpunkte raus)
    "since": "2026-10-16",         # aeltere Daten = Aufbauzeit, nicht pruefen
    "arrive_mm": 500,
    "leave_mm": 1000,
    "link_s": 25.0,                # Pause zwischen zwei Tracks einer Kette
    "link_mm": 800,                # Ende -> Anfang des Folgetracks
    "mower_windows": [{"start": "15:00", "end": "16:45"}],
    "block_spots": [{"x": 7200, "y": 10800, "r": 700,
                     "note": "Radar-Haltestelle hinten (37-s-Halte)"}],
    "skip_marks": ["person", "mower", "storm", "bird"],
}


def merged_cfg(cfg):
    out = dict(DEFAULT_CFG)
    for k, v in (cfg or {}).items():
        if k in DEFAULT_CFG:
            out[k] = v
    return out


def since_epoch(cfg):
    try:
        return datetime.strptime(str(cfg.get("since") or "2000-01-01"), "%Y-%m-%d")             .replace(tzinfo=TZ).timestamp()
    except ValueError:
        return 0.0


def _hhmm(s):
    h, m = str(s).split(":")
    return int(h) * 60 + int(m)


def in_mower_window(t, windows):
    lt = datetime.fromtimestamp(t, TZ)
    mod = lt.hour * 60 + lt.minute
    for w in windows or ():
        try:
            a, b = _hhmm(w["start"]), _hhmm(w["end"])
        except Exception:                                    # noqa: BLE001
            continue
        if (a <= mod < b) if a < b else (mod >= a or mod < b):
            return True
    return False


def build_chains(tracks, cfg):
    """tracks: nach t0 sortiert, je {key,id,t0,t1,pts,mark}. Liefert Ketten
    [{tracks:[...], t0, t1, last:(t,x,y)}]: ein Track haengt sich an die Kette,
    deren Ende hoechstens link_s vor seinem Anfang liegt und hoechstens link_mm
    von seinem ersten Punkt entfernt ist (sonst neue Kette)."""
    chains = []
    for tr in tracks:
        p = tr["pts"]
        if not p:
            continue
        first = p[0]
        best, bd = None, None
        for ch in chains:
            lt, lx, ly = ch["last"]
            gap = first[0] - lt
            if gap < -1.0 or gap > cfg["link_s"]:
                continue
            d = math.hypot(first[1] - lx, first[2] - ly)
            if d <= cfg["link_mm"] and (bd is None or d < bd):
                best, bd = ch, d
        if best is None:
            best = {"tracks": [], "t0": tr["t0"], "t1": tr["t1"], "last": (0, 0, 0)}
            chains.append(best)
        best["tracks"].append(tr)
        best["t1"] = max(best["t1"], tr["t1"])
        last = p[-1]
        if last[0] >= best["last"][0]:
            best["last"] = (last[0], last[1], last[2])
    return chains


def _longest_sit(pts, r):
    """Laengstes Zeitfenster, in dem alle Punkte innerhalb r vom ersten Punkt
    des Fensters bleiben. -> (dauer, i, j)"""
    best = (0.0, 0, 0)
    n = len(pts)
    for i in range(n):
        if n - i < 3:
            break
        if best[0] > 0 and pts[-1][0] - pts[i][0] <= best[0]:
            break                                  # laenger geht nicht mehr
        xi, yi = pts[i][1], pts[i][2]
        j = i
        while j + 1 < n and math.hypot(pts[j + 1][1] - xi, pts[j + 1][2] - yi) < r:
            j += 1
        dt = pts[j][0] - pts[i][0]
        if dt > best[0]:
            best = (dt, i, j)
    return best


def evaluate_chain(ch, cfg, mower_labels=()):
    """Kot-Profil fuer eine Kette pruefen. None oder dict mit Merkmalen."""
    if ch["t1"] - ch["t0"] < cfg["sit_s"]:
        return None
    if any((tr.get("mark") or "") in cfg["skip_marks"] for tr in ch["tracks"]):
        return None
    pts = sorted(tuple(q) for tr in ch["tracks"] for q in tr["pts"])
    if len(pts) < 5:
        return None
    dt, i, j = _longest_sit(pts, cfg["sit_r_mm"])
    if dt < cfg["sit_s"]:
        return None
    seg = pts[i:j + 1]
    xs = [q[1] for q in seg]
    ys = [q[2] for q in seg]
    cx, cy = sum(xs) / len(xs), sum(ys) / len(ys)
    sdx = math.sqrt(sum((x - cx) ** 2 for x in xs) / len(xs))
    sdy = math.sqrt(sum((y - cy) ** 2 for y in ys) / len(ys))
    if max(sdx, sdy) > cfg["sd_max_mm"]:
        return None
    din = max([math.hypot(q[1] - cx, q[2] - cy) for q in pts[:i + 1]] + [0.0])
    dout = max([math.hypot(q[1] - cx, q[2] - cy) for q in pts[j:]] + [0.0])
    if din < cfg["arrive_mm"] or dout < cfg["leave_mm"]:
        return None
    t_sit0, t_sit1 = seg[0][0], seg[-1][0]
    if in_mower_window(t_sit0, cfg["mower_windows"]) or \
       in_mower_window(t_sit1, cfg["mower_windows"]):
        return None
    if any(a <= t_sit1 and t_sit0 <= b for a, b in mower_labels):
        return None
    for b in cfg["block_spots"] or ():
        try:
            if math.hypot(cx - float(b["x"]), cy - float(b["y"])) <= float(b["r"]):
                return None
        except (KeyError, TypeError, ValueError):
            continue
    rate = len(seg) / max(dt, 0.1)
    if rate < cfg["min_rate"]:
        return None
    gaps = [b[0] - a[0] for a, b in zip(seg, seg[1:])] or [0.0]
    run = frz = 1
    for a, b in zip(seg, seg[1:]):
        run = run + 1 if (a[1], a[2]) == (b[1], b[2]) else 1
        frz = max(frz, run)
    return {
        "t_sit0": t_sit0, "t_sit1": t_sit1, "sit_s": round(dt, 1),
        "x": round(cx), "y": round(cy),
        "sdx": round(sdx), "sdy": round(sdy),
        "arrive_mm": round(din), "leave_mm": round(dout),
        "rate": round(rate, 2), "maxgap": round(max(gaps), 1),
        "uniq": round(len({(q[1], q[2]) for q in seg}) / len(seg), 2), "frz": frz,
        "senders": sorted({q[3] for q in seg}),
        "t0": pts[0][0], "t1": pts[-1][0],
        "tracks": [{"id": tr.get("id"), "key": tr["key"]} for tr in ch["tracks"]],
    }


def same_incident(a, b):
    """Gleicher Verdacht (z.B. nach einem Neuaufbau der Tracks)? Sitzzeiten
    ueberlappen (+-5 s) und Sitzorte liegen <= 1 m auseinander."""
    return (a["t_sit0"] - 5 <= b["t_sit1"] and b["t_sit0"] - 5 <= a["t_sit1"] and
            math.hypot(a["x"] - b["x"], a["y"] - b["y"]) <= 1000)


def push_text(inc):
    lt = datetime.fromtimestamp(inc["t_sit0"], TZ)
    return {
        "zeit": lt.strftime("%d.%m. %H:%M"),
        "ort": "%.1f / %.1f m" % (inc["x"] / 1000.0, inc["y"] / 1000.0),
        "dauer": "%d s" % round(inc["sit_s"]),
        "nr": str(inc.get("id", "")),
        "track": ",".join(str(t.get("id")) for t in inc.get("tracks", [])),
    }


def scan_step(fetch, cursor, horizon, cfg, mower_labels=(), limit=3000):
    """Ein Haeppchen der fortlaufenden Pruefung.
    fetch(t_from, t_to, limit) -> Tracks mit t_from <= t0 <= t_to, nach t0
    sortiert (je {key,id,t0,t1,pts,mark}). horizon = bis hierhin sind alle Tracks
    fertig (Analysierer). Liefert (treffer, neuer_cursor, mehr_da).
    Eine Kette gilt erst als abgeschlossen, wenn nach ihrem Ende link_s lang
    nichts mehr kommen kann; offene Ketten werden beim naechsten Mal ab ihrem
    Anfang erneut gelesen (Treffer werden per same_incident dedupliziert)."""
    trs = fetch(cursor, horizon, limit + 1)
    full = len(trs) > limit
    if full:
        trs = trs[:limit]
    if not trs:
        return [], max(cursor, horizon - cfg["link_s"] - 1.0), False
    close_before = (trs[-1]["t0"] if full else horizon) - cfg["link_s"]
    hits, open_starts = [], []
    for ch in build_chains(trs, cfg):
        if ch["t1"] < close_before:
            inc = evaluate_chain(ch, cfg, mower_labels)
            if inc:
                hits.append(inc)
        else:
            open_starts.append(ch["t0"])
    if open_starts:
        new_cursor = min(open_starts)
    else:
        new_cursor = (trs[-1]["t0"] + 1e-6) if full else max(cursor, close_before)
    if new_cursor <= cursor:
        # Notbremse: alles in einer einzigen offenen Kette (z.B. Sturm) ->
        # trotzdem weiter, sonst haengt die Pruefung an dieser Stelle fest
        new_cursor = trs[-1]["t0"] + 1e-6
    return hits, new_cursor, full
