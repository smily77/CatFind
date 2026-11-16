"""Ende-zu-Ende-Test der echten Karte (Tab5 an COM) gegen den Head-Simulator.

Aufruf: python karte_e2e.py [COM19] [PC-IP]

Der Tab5 wird per Testzugang auf den Simulator (dieser PC) umgelenkt; der
echte Head bekommt in der Zeit nichts. Am Ende wird wieder auf 'auto' gestellt.
Prueft: Kalibrierung (Kopf-Pose gegen Wahrheit), Tilt-Korrektur, Punkte liegen
auf der Gartengrenze, Fehlmessung, Rueckgaengig, Schrittweite + Auto-Scan,
Edit/Loeschen, Anfahrt pruefen, Speichern - bei 20 % Paketverlust.
"""
import math
import sys
import time

import headsim as H
from karte import Karte

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM19"
PC_IP = next((a for a in sys.argv[2:] if a[0].isdigit()), "192.168.0.61")
fails = 0


def check(name, cond, info=""):
    global fails
    print(("OK    " if cond else "FEHLER") + f"  {name}  {info}")
    if not cond:
        fails += 1


def press_until(k, button, pattern, timeout=30):
    return k.wait_status(pattern, timeout, since=k.cmd(f"btn {button}"))


sim = H.Sim(drop=0.2).start()
k = Karte(PORT)
if "--reset" in sys.argv:
    print("Neustart Tab5:", k.reset())
k.cmd(f"head {PC_IP}")
k.pump(2.5)
d = k.dump()
check("mit Simulator verbunden", d.get("conn") == 1, d.get("raw_state", ""))
if d.get("state") != 0:
    print("Karte steht nicht in Schritt 1 (Origin). Bitte Tab5 neu starten.")
    sys.exit(2)

TILT = -5.0
k.cmd(f"tilt {TILT}")

# ── Kalibrierung ──
k.cmd(f"pan {H.pan_to(0, 0):.4f}")
st = press_until(k, "act", "Origin OK|ungueltig|antwortet")
check("Origin gemessen", st and "Origin OK" in st, st or "")
k.cmd(f"pan {H.pan_to(H.A_X, 0):.4f}")
st = press_until(k, "act", "Kalibriert|ungueltig|antwortet|zu nah")
check("X-Achse gemessen", st and "Kalibriert" in st, st or "")
d = k.dump()
ehx, ehy = d.get("hx", 0) - H.HEAD_X, d.get("hy", 0) - H.HEAD_Y
ephi = math.degrees(d.get("phi", 0)) - H.HEAD_PHI_DEG
check("Kopf-Position", math.hypot(ehx, ehy) < 0.01,
      f"ist ({d.get('hx')},{d.get('hy')}) soll ({H.HEAD_X},{H.HEAD_Y}) Fehler {math.hypot(ehx, ehy)*1000:.1f} mm")
check("Kopf-Richtung", abs(ephi) < 0.1, f"Fehler {ephi:.3f} Grad")

# ── Einzelpunkte ──
for pan in (-20.0, 10.0, 40.0):
    k.cmd(f"pan {pan}")
    st = press_until(k, "act", r"Pt \d+|ungueltig|antwortet")
    check(f"Punkt bei Pan {pan:+.0f}", st and "Pt" in st, st or "")
d = k.dump()
check("3 Punkte", len(d["points"]) == 3, str(len(d["points"])))
worst = max(H.dist_to_garden(x, y) for _, x, y in d["points"]) if d["points"] else 1
check("Punkte liegen auf der Grenze (mit Tilt-Korrektur)", worst < 0.01, f"max. Abweichung {worst*1000:.1f} mm")

# ── Fehlmessung (Strahl ins Leere) ──
k.cmd("pan 160")
t0 = time.time()
st = press_until(k, "act", r"Pt \d+|ungueltig|antwortet")
print(f"   Fehlmessung nach {time.time()-t0:.1f} s gemeldet")
check("Fehlmessung gemeldet", st and "ungueltig" in st, st or "")

# ── Rueckgaengig ──
k.cmd("pan 25")
press_until(k, "act", r"Pt \d+")
n_before = len(k.dump()["points"])
k.cmd("btn undo")
k.pump(0.5)
n_after = len(k.dump()["points"])
check("Letzten loeschen", n_after == n_before - 1, f"{n_before} -> {n_after}")

# ── Doppelter Pan ersetzt ──
k.cmd("pan 10")
press_until(k, "act", r"Pt \d+")
check("gleicher Pan ersetzt statt verdoppelt", len(k.dump()["points"]) == n_after)

# ── Plausibilitaet: ein Ausreisser -> nachgemessen, richtiger Wert ──
k.cmd("pan 25")
sim.outliers = [34.11]
st = press_until(k, "act", r"Pt \d+|ungueltig|antwortet")
d = k.dump()
p25 = [pt for pt in d["points"] if abs(pt[0] - 25) < 0.2]
ok = st and "nachgemessen" in st and p25 and H.dist_to_garden(p25[0][1], p25[0][2]) < 0.01
check("Ausreisser nachgemessen", ok, st or "")
k.cmd("btn undo"); k.pump(0.3)

# ── zweimal derselbe abweichende Wert -> echte Stelle, uebernommen ──
sim.outliers = [2.0, 2.0]
st = press_until(k, "act", r"Pt \d+|ungueltig|antwortet")
d = k.dump()
p25 = [pt for pt in d["points"] if abs(pt[0] - 25) < 0.2]
r25 = math.hypot(p25[0][1] - H.HEAD_X, p25[0][2] - H.HEAD_Y) if p25 else 0
check("bestaetigte Abweichung uebernommen", st and "nachgemessen" in st and abs(r25 - 2.0 * math.cos(math.radians(TILT))) < 0.01,
      f"{st}  r={r25:.3f}")
k.cmd("btn undo"); k.pump(0.3)

# ── drei verschiedene Ausreisser -> unsicher ──
sim.outliers = [34.11, 12.0, 0.4]
st = press_until(k, "act", r"Pt \d+|ungueltig|antwortet")
check("drei Ausreisser -> unsicher", st and "unsicher" in st, st or "")
k.cmd("btn undo"); k.pump(0.3)
n_after = len(k.dump()["points"])

# ── Schrittweite + Auto-Scan ──
st = press_until(k, "step", "Scan-Schritt", 3)        # 2 -> 5
check("Schrittweite umgeschaltet", st and "5.0" in st, st or "")
t0 = time.time()
sim.outlier_every = 4                                 # jede 4. Messung ein Ausreisser
oc0 = sim.outlier_count
st = press_until(k, "scan", "Auto-Scan fertig|voll|gestoppt", 180)
sim.outlier_every = 0
print(f"   {sim.outlier_count - oc0} Ausreisser im Scan eingestreut")
check("Scan meldet Nachmessungen", st and "nachgemessen" in st, st or "")
d = k.dump()
pans = [p for p, _, _ in d["points"]]
check("Auto-Scan fertig", st is not None, f"{st}  {time.time()-t0:.1f} s")
expected = int((40 - (-20)) / 5) + 1
check("Scan-Punktzahl", len(pans) == expected, f"{len(pans)} Punkte, erwartet {expected}")
worst = max(H.dist_to_garden(x, y) for _, x, y in d["points"])
check("Scan-Punkte auf der Grenze", worst < 0.01, f"max. {worst*1000:.1f} mm")
check("nach Pan sortiert", pans == sorted(pans))

# ── Edit: Punkt 2 loeschen ──
k.cmd("btn edit")
k.cmd("enc 6 2")
st = press_until(k, "edit", "geloescht", 3)
k.cmd("btn act")
k.pump(0.3)
d = k.dump()
check("Edit loescht Punkt 2", st and "Punkt 2" in st and len(d["points"]) == expected - 1, st or "")
check("zurueck im Messmodus", d.get("state") == 2)

# ── Anfahrt pruefen ──
k.cmd("pan 0")
n = len(d["points"])
st = press_until(k, "check", "Check:|ungueltig|antwortet")
check("Anfahrt pruefen", st and "Check:" in st and len(k.dump()["points"]) == n, st or "")

# ── Speichern ──
st = press_until(k, "save", "Gespeichert|SD|Datei|Mind", 5)
check("Karte gespeichert", st and "Gespeichert" in st, st or "")

# Aufraeumen: Schrittweite zurueck auf 2 (5 -> 0.5 -> 1 -> 2), Head wieder automatisch
for _ in range(3):
    k.cmd("btn step")
k.cmd("head auto")
print(f"\nSimulator: {sim.rx} Pakete empfangen, {sim.dropped} absichtlich verworfen, {sim.measures} Messungen")
print(f"{fails} Fehler")
sys.exit(1 if fails else 0)
