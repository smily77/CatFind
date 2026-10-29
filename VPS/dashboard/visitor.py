"""Spin-off BurglerProtect: Auswertung der Veranda-Meldungen (visitorObserved).

Konzept: SpinOff/BurglerProtect/Dokumentation/Konzept.md. Der Dome-C1 meldet
Ziele ausserhalb seiner Filterkarte im Quadranten X > 0, Y < 0 als
visitorObserved; das Ausloesepolygon prueft erst der Empfaenger. Heute ist das
der VPS (Tab "Besucher"), spaeter das Steuergeraet Besucherlicht, das dieselbe
Regel lokal rechnet. Darum bleibt die Regel hier bewusst einfach und
nachbaubar (kein numpy, nur Listen).

Regel (retriggerbar, User 2026-10-27: "solange der Besucher da ist, soll das
Licht leuchten"):
  * Spur: Meldung gehoert zur naechsten Spur, wenn hoechstens link_mm und
    link_s entfernt, sonst neue Spur.
  * Ausloesen (Licht aus -> an): eine Spur hat im Polygon mindestens
    min_events Meldungen innerhalb von window_s, und diese ueberspannen
    mindestens min_span_s.
  * Halten: solange das Licht brennt, verlaengert JEDE Meldung im Polygon
    die Einschaltdauer auf "Meldung + on_s".
  * Aktive Zeit (Schritt 2): nur von Sonnenuntergang + start_off_min bis
    end_min (Minuten ab Mitternacht, 1440 = 24:00, bis 1800 = 06:00 frueh).
    Ausserhalb schaltet das Steuergeraet nicht ein (Rolle 3 hier).
Das Steuergeraet (SpinOff/BurglerProtect/Firmware/Besucherlicht6_3_0) holt
dieselben Werte als /visitor_device.cfg (device_cfg_text).
"""
import datetime
import math
from zoneinfo import ZoneInfo

TZ = ZoneInfo("Europe/Zurich")
SITE_LAT, SITE_LON = 47.3857, 9.2794          # Herisau AR

# Hue-Gruppen (Namen wie in der Hue-App; gross/klein und Umlaute egal).
# Brennt eine Zuhause-Gruppe, ist jemand da und das Licht wird nicht eingeschaltet.
HOME_GROUPS = ["Arbeitszimmer", "Buro", "stubbe", "Wohnzimmer Decke", "Veranda"]
LIGHT_GROUPS = ["Wohnzimmer Decke"]
MAX_GROUPS = 8

DEFAULTS = {
    "min_events": 12,     # Meldungen einer Spur im Polygon ...
    "window_s": 3.0,      # ... innerhalb dieses Fensters ...
    "min_span_s": 1.0,    # ... die mindestens so lange dauern
    "link_mm": 1000,      # Spur: groesster Sprung zwischen zwei Meldungen
    "link_s": 1.0,        # Spur: groesste Pause zwischen zwei Meldungen
    "on_s": 600,          # Licht brennt so lange nach der letzten Meldung im Polygon
    "start_off_min": 0,   # aktive Zeit beginnt so viele Minuten nach Sonnenuntergang
    "end_min": 1440,      # ... und endet um (Minuten ab Mitternacht, 1440 = 24:00)
}
LIMITS = {
    "min_events": (1, 64), "window_s": (0.5, 60.0), "min_span_s": (0.0, 30.0),
    "link_mm": (100, 5000), "link_s": (0.2, 10.0), "on_s": (10, 7200),
    "start_off_min": (-120, 240),
    "end_min": (720, 1800),
}


def clean_groups(groups, default):
    """Liste von Hue-Gruppennamen; Komma/Zeilenumbruch sind im Geraeteformat Trenner."""
    if groups is None:
        return list(default)
    out = []
    for g in groups if isinstance(groups, list) else []:
        g = str(g).replace(",", " ").replace(chr(10), " ").strip()[:32]
        if g and g not in out:
            out.append(g)
    return out[:MAX_GROUPS]


def sunset_min(day):
    """Sonnenuntergang in Minuten ab lokaler Mitternacht (NOAA-Naeherung, gleiche
    Formel wie im Steuergeraet sun.ino)."""
    g = 2 * math.pi / 365 * (day.timetuple().tm_yday - 1 + 0.5)
    eqt = 229.18 * (0.000075 + 0.001868 * math.cos(g) - 0.032077 * math.sin(g)
                    - 0.014615 * math.cos(2 * g) - 0.040849 * math.sin(2 * g))
    decl = (0.006918 - 0.399912 * math.cos(g) + 0.070257 * math.sin(g)
            - 0.006758 * math.cos(2 * g) + 0.000907 * math.sin(2 * g)
            - 0.002697 * math.cos(3 * g) + 0.00148 * math.sin(3 * g))
    lat = math.radians(SITE_LAT)
    c = math.cos(math.radians(90.833)) / (math.cos(lat) * math.cos(decl)) - math.tan(lat) * math.tan(decl)
    ha = math.degrees(math.acos(max(-1.0, min(1.0, c))))
    noon = datetime.datetime.combine(day, datetime.time(12), TZ)
    off = noon.utcoffset().total_seconds() / 60
    return round(720 - 4 * (SITE_LON - ha) - eqt + off)


def active_window(day, params):
    """(Beginn, Ende) der aktiven Zeit des Abends 'day' als Unix-Zeit."""
    p = clean_params(params)
    mid = datetime.datetime.combine(day, datetime.time(0), TZ).timestamp()
    start = sunset_min(day) + p["start_off_min"]
    # Sommerzeit-Wechsel in der Nacht vernachlaessigt (eine Stunde Versatz)
    return mid + start * 60, mid + p["end_min"] * 60


def is_active(t, params):
    """Liegt t in der aktiven Zeit (heutiger Abend oder Nacht nach gestern)?"""
    d = datetime.datetime.fromtimestamp(t, TZ).date()
    for day in (d, d - datetime.timedelta(days=1)):
        a, b = active_window(day, params)
        if a <= t < b:
            return True
    return False


def next_window(t, params):
    """Laufende oder naechste aktive Zeit (Beginn, Ende) - fuer die Anzeige."""
    d = datetime.datetime.fromtimestamp(t, TZ).date()
    for day in (d - datetime.timedelta(days=1), d, d + datetime.timedelta(days=1)):
        a, b = active_window(day, params)
        if t < b and a < b:
            return a, b
    return active_window(d + datetime.timedelta(days=1), params)


def device_cfg_text(cfg):
    """Konfiguration fuers Steuergeraet: eine Angabe je Zeile 'schluessel,wert',
    Schlusszeile 'end' (das Geraet verwirft abgeschnittene Antworten)."""
    p = clean_params(cfg.get("params"))
    lines = ["# Besucherlicht-Konfiguration vom VPS (visitor.py)",
             "version,%s" % int(cfg.get("updated") or 0)]
    lines += ["%s,%s" % (k, p[k]) for k in DEFAULTS]
    lines += ["light,%s" % g for g in clean_groups(cfg.get("light_groups"), LIGHT_GROUPS)]
    lines += ["home,%s" % g for g in clean_groups(cfg.get("home_groups"), HOME_GROUPS)]
    lines += ["poly,%d,%d" % (x, y) for x, y in clean_poly(cfg.get("poly"))]
    lines.append("end")
    return chr(10).join(lines) + chr(10)


def clean_params(p):
    out = dict(DEFAULTS)
    for k, (lo, hi) in LIMITS.items():
        if k in (p or {}):
            try:
                v = float(p[k])
            except (TypeError, ValueError):
                continue
            v = min(hi, max(lo, v))
            out[k] = int(round(v)) if isinstance(DEFAULTS[k], int) else round(v, 2)
    return out


def clean_poly(poly):
    """[[x,y],...] in Welt-mm; weniger als 3 Ecken = kein Polygon."""
    out = []
    for pt in poly or []:
        try:
            out.append([int(round(float(pt[0]))), int(round(float(pt[1])))])
        except (TypeError, ValueError, IndexError):
            continue
    return out if len(out) >= 3 else []


def point_in_poly(x, y, poly):
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / ((yj - yi) or 1e-9) + xi:
            inside = not inside
        j = i
    return inside


def evaluate(events, poly, params, timed=True):
    """events: Liste (t, wx, wy) nach Zeit sortiert. Ohne Polygon zaehlt der
    ganze Quadrant. Liefert je Meldung (Spur-Nr, im Polygon, Rolle) sowie die
    Spuren und Licht-Intervalle.
    Rolle: 0 = nichts, 1 = loest aus, 2 = haelt das Licht an,
    3 = haette ausgeloest, aber ausserhalb der aktiven Zeit (nur mit timed)."""
    p = clean_params(params)
    link2 = float(p["link_mm"]) ** 2
    tracks = []            # aktive Spuren: dict(id, t, x, y, win=[t im Polygon])
    info = {}              # Spur-Nr -> Zusammenfassung
    per_event = []
    lights = []            # dict(on, off, track, last)
    off_at = None
    next_id = 1
    for t, x, y in events:
        tracks = [tr for tr in tracks if t - tr["t"] <= p["link_s"]]
        best, bd = None, link2
        for tr in tracks:
            d = (x - tr["x"]) ** 2 + (y - tr["y"]) ** 2
            if d <= bd:
                best, bd = tr, d
        if best is None:
            best = {"id": next_id, "t": t, "x": x, "y": y, "win": []}
            next_id += 1
            tracks.append(best)
            info[best["id"]] = {"id": best["id"], "t0": t, "t1": t, "n": 0, "n_in": 0,
                                "sx": 0.0, "sy": 0.0, "trig": False, "hold": False,
                                "day": False}
        best["t"], best["x"], best["y"] = t, x, y
        s = info[best["id"]]
        s["t1"] = t
        s["n"] += 1
        s["sx"] += x
        s["sy"] += y
        inside = point_in_poly(x, y, poly) if poly else True
        role = 0
        if off_at is not None and t > off_at:
            off_at = None
        if inside:
            s["n_in"] += 1
            best["win"].append(t)
            best["win"] = [u for u in best["win"] if t - u <= p["window_s"]]
            if off_at is not None:                               # Licht brennt: halten
                off_at = t + p["on_s"]
                lights[-1]["off"] = off_at
                lights[-1]["last"] = t
                role = 2
                s["hold"] = True
            elif (len(best["win"]) >= p["min_events"]
                  and best["win"][-1] - best["win"][0] >= p["min_span_s"]):
                if timed and not is_active(t, p):
                    role = 3
                    s["day"] = True
                    per_event.append((best["id"], 1, role))
                    continue
                off_at = t + p["on_s"]
                lights.append({"on": t, "off": off_at, "track": best["id"], "last": t})
                role = 1
                s["trig"] = True
        per_event.append((best["id"], 1 if inside else 0, role))
    summary = []
    for s in info.values():
        n = s.pop("n") or 1
        s["n"] = n
        s["x"] = round(s.pop("sx") / n)
        s["y"] = round(s.pop("sy") / n)
        summary.append(s)
    return {"events": per_event, "tracks": summary, "lights": lights, "params": p}
