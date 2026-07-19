#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Experiment (auf Wunsch, nicht produktiv verdrahtet): Wie veraendert sich der
Anteil "vernuenftiger" (kohaerent bewegter) catObserved-Punkte, wenn man

  a) ein Maeher-Zeitfenster ausschliesst, und
  b) nur Punkte INNERHALB der NoShot-Karte behaelt,

bevor man das bestehende Erkennungsmodell (catmodel.py) drueberlaufen laesst?
Vergleicht "roh" gegen "gefiltert" auf DENSELBEN Zeitraum.

Nutzung auf dem VPS (im Ordner VPS/dashboard, wo dashboard.py + catmodel.py
liegen und die SQLite-DB unter DB_PATH erreichbar ist):

  python3 analyze_noshot_filter.py \
      --db /data/catfinder.db \
      --t0 "2026-07-19 00:00" --t1 "2026-07-20 00:00" \
      --mower-t0 "2026-07-19 15:00" --mower-t1 "2026-07-19 16:30" \
      --noshot ../../Controller/Manager6_3_0/data/noshot.csv

--t0/--t1/--mower-t0/--mower-t1 werden als LOKALE Zeit in --tz (Default
Europe/Zurich, wie LOCAL_TZ in dashboard.py) interpretiert, nicht UTC -
die DB speichert reine Unix-Timestamps (zeitzonen-neutral), das Dashboard
im Browser zeigt automatisch lokale Zeit; dieses Skript muss die Umrechnung
darum selbst machen (frueher ein Bug hier: naive UTC-Interpretation liess
das Maeher-Fenster 2h daneben liegen).

Ohne --db: erzeugt einen synthetischen Testdatensatz (--demo), NUR um das
Skript selbst zu pruefen - keine echten Zahlen.
"""
import argparse
import random
import sqlite3
import sys
from datetime import datetime
from zoneinfo import ZoneInfo

DEFAULT_TZ = "Europe/Zurich"

import catmodel


def load_noshot_rings(path):
    """Ringe (Liste von (x,y)-Tupeln) aus einer noshot.csv/rasen.csv lesen."""
    rings, ring = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                if len(ring) >= 3:
                    rings.append(ring)
                ring = []
                continue
            if line.startswith("#"):
                continue
            x, y = line.split(",")[:2]
            ring.append((float(x), float(y)))
    if len(ring) >= 3:
        rings.append(ring)
    return rings


def inside_any_ring(x, y, rings):
    return any(catmodel.point_in_poly(x, y, ring) for ring in rings)


def parse_dt(s, tz=DEFAULT_TZ):
    """'YYYY-MM-DD HH:MM' als LOKALE Zeit in tz (Default Europe/Zurich) ->
    Unix-Timestamp. Die DB speichert UTC-Unix-Timestamps; ohne explizite
    Zeitzone wuerde Python die lokale Zeit der ausfuehrenden Maschine annehmen
    (hier: UTC-Sandbox) - das fuehrte zuvor zu einem 2h-Versatz (Sommerzeit)."""
    naive = datetime.strptime(s, "%Y-%m-%d %H:%M")
    return naive.replace(tzinfo=ZoneInfo(tz)).timestamp()


def fmt_local(ts, tz=DEFAULT_TZ):
    """Unix-Timestamp -> lesbare lokale Zeit in tz (fuers Ausgeben/Debuggen)."""
    return datetime.fromtimestamp(ts, tz=ZoneInfo(tz)).strftime("%Y-%m-%d %H:%M:%S %Z")


def load_events_from_db(db_path, t0, t1):
    con = sqlite3.connect(db_path)
    cur = con.execute(
        "SELECT t,sender,sensor,wx,wy,wv,speed FROM events "
        "WHERE t>=? AND t<=? AND wv=1 ORDER BY t", (t0, t1))
    events = [{"t": r[0], "sender": r[1], "sensor": r[2],
               "wx": r[3], "wy": r[4], "speed": r[6]} for r in cur.fetchall()]
    con.close()
    return events


def make_demo_events(t0, mower_t0, mower_t1):
    """NUR zum Selbsttest des Skripts - keine echten Messwerte. Baut:
       - zwei "Katzen"-artige, kohaerente Spuren (im NoShot-Bereich)
       - Maeher-Laerm im Maeher-Fenster (schnelle, ueberall verteilte Punkte)
       - Vegetations-/Nachbar-Rauschen ausserhalb der NoShot-Karte
    """
    rnd = random.Random(42)
    events = []

    def track(sender, start_t, x0, y0, x1, y1, n, jitter=60):
        for i in range(n):
            f = i / max(n - 1, 1)
            x = x0 + (x1 - x0) * f + rnd.uniform(-jitter, jitter)
            y = y0 + (y1 - y0) * f + rnd.uniform(-jitter, jitter)
            events.append({"t": start_t + i * 0.3, "sender": sender, "sensor": 0,
                            "wx": x, "wy": y, "speed": 300})

    # zwei plausible "Katzen"-Durchquerungen, innerhalb der Test-NoShot-Karte (s.u.)
    track(1, t0 + 3600, 500, 2000, 6000, 9000, 30)
    track(1, t0 + 20000, 7000, 12000, 2000, 3000, 25)

    # Maeher: viele Punkte, ueber die ganze Flaeche verteilt, im Ausschlussfenster
    for i in range(400):
        events.append({"t": mower_t0 + rnd.uniform(0, mower_t1 - mower_t0), "sender": 1,
                        "sensor": 0, "wx": rnd.uniform(0, 9000), "wy": rnd.uniform(0, 14000),
                        "speed": 500})

    # Vegetation/Nachbargrundstueck: Rauschen AUSSERHALB der Test-NoShot-Karte
    for i in range(150):
        events.append({"t": t0 + rnd.uniform(0, 30000), "sender": 2, "sensor": 0,
                        "wx": rnd.uniform(-5000, -500), "wy": rnd.uniform(0, 14000),
                        "speed": rnd.uniform(0, 100)})
    return sorted(events, key=lambda e: e["t"])


DEMO_NOSHOT = [[(0, 0), (9000, 0), (9000, 14000), (0, 14000)]]  # einfaches Testrechteck


def move_and_confirmed_stats(events, devices=None, params=None):
    """Baut Tracks wie catmodel.analyze() und zaehlt Punkte, die zu einem Track
    mit gefundener kohaerenter Bewegung ("Pflichtkriterium") bzw. zu einem
    vollstaendig BESTAETIGTEN Track gehoeren."""
    p = catmodel.merged_params(params)
    evs = sorted(events, key=lambda e: e["t"])
    if not evs:
        return {"n_events": 0, "n_tracks": 0, "n_pts_move": 0, "n_pts_confirmed": 0,
                "n_confirmed_tracks": 0}
    storms = catmodel.detect_storms(evs, p)
    tracks = catmodel.stitch_tracks(catmodel.build_tracks(evs, p), p)
    w_t0, w_t1 = evs[0]["t"], evs[-1]["t"]
    scored = [catmodel.score_track(tr, storms, None, devices, p, i, w_t0, w_t1)
              for i, tr in enumerate(tracks)]
    n_pts_move = sum(t["n"] for t in scored
                      if any(lbl == "kohärente Bewegung" for lbl, _ in t["score_detail"]))
    n_pts_confirmed = sum(t["n"] for t in scored if t["confirmed"])
    return {"n_events": len(evs), "n_tracks": len(scored),
            "n_pts_move": n_pts_move, "n_pts_confirmed": n_pts_confirmed,
            "n_confirmed_tracks": sum(1 for t in scored if t["confirmed"])}


def pct(a, b):
    return "%5.1f%%" % (100.0 * a / b) if b else "  n/a"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db")
    ap.add_argument("--t0", help="'YYYY-MM-DD HH:MM', lokale Zeit in --tz")
    ap.add_argument("--t1", help="'YYYY-MM-DD HH:MM', lokale Zeit in --tz")
    ap.add_argument("--day", help="Bequemlichkeit statt --t0/--t1: EIN Kalendertag 'YYYY-MM-DD' in --tz")
    ap.add_argument("--mower-t0")
    ap.add_argument("--mower-t1")
    ap.add_argument("--tz", default=DEFAULT_TZ,
                     help="Zeitzone fuer --t0/--t1/--day/--mower-t0/--mower-t1 (Default %(default)s)")
    ap.add_argument("--noshot")
    ap.add_argument("--demo", action="store_true",
                     help="synthetischer Testlauf statt echter DB (Default, wenn --db fehlt)")
    args = ap.parse_args()

    if args.db:
        if args.day:
            t0 = parse_dt(args.day + " 00:00", args.tz)
            t1 = parse_dt(args.day + " 00:00", args.tz) + 86400
        elif args.t0 and args.t1:
            t0, t1 = parse_dt(args.t0, args.tz), parse_dt(args.t1, args.tz)
        else:
            sys.exit("--t0/--t1 oder --day noetig mit --db")
        print("Zeitfenster: %s bis %s (%s)\n" % (fmt_local(t0, args.tz), fmt_local(t1, args.tz), args.tz))
        events = load_events_from_db(args.db, t0, t1)
        rings = load_noshot_rings(args.noshot) if args.noshot else None
        mode = "ECHTE DATEN (%s)" % args.db
    else:
        print("Kein --db angegeben -> synthetischer Selbsttest (KEINE echten Zahlen!)\n")
        t0 = parse_dt("2026-07-19 00:00", args.tz)
        mower_t0 = parse_dt("2026-07-19 15:00", args.tz)
        mower_t1 = parse_dt("2026-07-19 16:30", args.tz)
        events = make_demo_events(t0, mower_t0, mower_t1)
        rings = DEMO_NOSHOT
        args.mower_t0, args.mower_t1 = "2026-07-19 15:00", "2026-07-19 16:30"
        mode = "SYNTHETISCHER TESTLAUF"

    print("=== %s ===" % mode)
    print("Events gesamt: %d\n" % len(events))

    # A) roh
    raw = move_and_confirmed_stats(events)

    # B) gefiltert: Maeher-Fenster raus + nur innerhalb NoShot
    filtered = events
    if args.mower_t0 and args.mower_t1:
        mt0, mt1 = parse_dt(args.mower_t0, args.tz), parse_dt(args.mower_t1, args.tz)
        filtered = [e for e in filtered if not (mt0 <= e["t"] <= mt1)]
    if rings:
        filtered = [e for e in filtered if inside_any_ring(e["wx"], e["wy"], rings)]
    filt = move_and_confirmed_stats(filtered)

    def row(name, s):
        print("%-10s Events=%-6d Tracks=%-5d  Punkte in bewegten Tracks: %s (%d/%d)  "
              "bestaetigt: %s (%d/%d, %d Tracks)" % (
                  name, s["n_events"], s["n_tracks"],
                  pct(s["n_pts_move"], s["n_events"]), s["n_pts_move"], s["n_events"],
                  pct(s["n_pts_confirmed"], s["n_events"]), s["n_pts_confirmed"], s["n_events"],
                  s["n_confirmed_tracks"]))

    row("ROH", raw)
    row("GEFILTERT", filt)
    print("\nAusgeschlossen durch Maeher-Fenster + NoShot-Filter: %d von %d Events (%s)" % (
        len(events) - len(filtered), len(events), pct(len(events) - len(filtered), len(events))))


if __name__ == "__main__":
    main()
