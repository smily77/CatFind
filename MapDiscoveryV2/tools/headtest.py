"""Testet einen echten PanTiltHead_WM_V2 ueber UDP.

Aufruf:  python headtest.py [HEAD_IP]     (Standard 192.168.0.95)

Prueft: SET-Antwort, Messung (ACCEPTED -> DONE), Wiederholung derselben seq
misst nicht neu, Anti-Backlash-Anfahrt, SET waehrend Messung (BUSY, Position
wird nachgeholt), Marker/Laser-Flags, Antwortzeiten.
"""
import socket
import sys
import time

import mdv2

HEAD = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.95"
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", 0))
seq = 100
fails = 0


def send(cmd, flags, pan, tilt, s=None):
    global seq
    if s is None:
        seq += 1
        s = seq
    sock.sendto(mdv2.pack_cmd(s, cmd, flags, pan, tilt), (HEAD, mdv2.PORT_HEAD))
    return s


def recv(timeout):
    sock.settimeout(timeout)
    try:
        data, _ = sock.recvfrom(64)
    except socket.timeout:
        return None
    return mdv2.unpack_reply(data)


def wait_for(s, statuses, timeout):
    end = time.time() + timeout
    while time.time() < end:
        r = recv(end - time.time())
        if r and r["seq"] == s and r["status"] in statuses:
            return r
    return None


def check(name, cond, info=""):
    global fails
    print(("OK    " if cond else "FEHLER") + f"  {name}  {info}")
    if not cond:
        fails += 1


def fmt(r):
    if not r:
        return "keine Antwort"
    return (f"{mdv2.ST_NAME[r['status']]} d={r['dist']:.3f} pan={r['pan']:.2f} "
            f"tilt={r['tilt']:.2f}{'' if r['tilt_ok'] else '(soll)'} "
            f"sensor={mdv2.SENSOR_NAME.get(r['sensor'])} flags={r['flags']}")


def measure(pan, tilt=0.0, flags=0):
    t0 = time.time()
    s = send(mdv2.CMD_MEASURE, flags, pan, tilt)
    acc = wait_for(s, {mdv2.ST_ACCEPTED}, 1.0)
    fin = wait_for(s, {mdv2.ST_DONE, mdv2.ST_FAILED, mdv2.ST_NOSENSOR}, 20.0)
    return s, acc, fin, time.time() - t0


# 1. SET
t0 = time.time()
s = send(mdv2.CMD_SET, 0, 0.0, 0.0)
r = wait_for(s, {mdv2.ST_DONE}, 1.0)
check("SET beantwortet", r is not None, f"{(time.time()-t0)*1000:.0f} ms  {fmt(r)}")

# 2. Messung bei 0
s, acc, fin, dt = measure(0.0)
check("MEASURE angenommen", acc is not None)
check("MEASURE fertig", fin is not None and fin["status"] == mdv2.ST_DONE, f"{dt:.2f} s  {fmt(fin)}")

# 3. Wiederholung derselben seq -> gleiche Antwort, keine neue Messung
t0 = time.time()
send(mdv2.CMD_MEASURE, 0, 0.0, 0.0, s=s)
rep = wait_for(s, {mdv2.ST_DONE, mdv2.ST_FAILED, mdv2.ST_NOSENSOR}, 1.0)
dt = time.time() - t0
check("Wiederholung ohne Neumessung", rep is not None and dt < 0.3 and fin and rep["dist"] == fin["dist"],
      f"{dt*1000:.0f} ms")

# 4. Fahrt + Messung im selben Auftrag: Ist-Winkel muss zum Ziel passen
for target in (30.0, -45.0, 12.4):
    s, acc, fin, dt = measure(target)
    ok = fin is not None and abs(fin["pan"] - target) < 0.6
    check(f"Fahrt+Messung {target:+.1f}", ok, f"{dt:.2f} s  {fmt(fin)}")

# 5. Anti-Backlash-Anfahrt
s, acc, fin, dt = measure(20.0, flags=mdv2.F_APPROACH)
check("Anfahrt +20", fin is not None and abs(fin["pan"] - 20.0) < 0.6, f"{dt:.2f} s  {fmt(fin)}")

# 6. SET waehrend Messung -> BUSY, Position danach nachgeholt
s = send(mdv2.CMD_MEASURE, 0, 0.0, 0.0)
wait_for(s, {mdv2.ST_ACCEPTED}, 1.0)
s2 = send(mdv2.CMD_SET, 0, 10.0, 0.0)
busy = wait_for(s2, {mdv2.ST_BUSY, mdv2.ST_DONE}, 1.0)
fin = wait_for(s, {mdv2.ST_DONE, mdv2.ST_FAILED}, 20.0)
check("SET waehrend Messung = BUSY", busy is not None and busy["status"] == mdv2.ST_BUSY, fmt(busy))
check("Messung blieb bei 0", fin is not None and abs(fin["pan"]) < 0.6, fmt(fin))
time.sleep(0.8)
s3 = send(mdv2.CMD_SET, 0, 10.0, 0.0)
r = wait_for(s3, {mdv2.ST_DONE}, 1.0)
s, acc, fin, dt = measure(10.0)
check("Position 10 danach angefahren", fin is not None and abs(fin["pan"] - 10.0) < 0.6, fmt(fin))

# 7. Laser + Marker Flags
s = send(mdv2.CMD_SET, mdv2.F_LASER | mdv2.F_MARKER, 0.0, 0.0)
r = wait_for(s, {mdv2.ST_DONE}, 2.0)
check("Laser+Marker ein gemeldet", r is not None and r["flags"] == 3, fmt(r))
s, acc, fin, dt = measure(0.0, flags=mdv2.F_LASER | mdv2.F_MARKER)
check("Laser nach Messung wieder ein", fin is not None and fin["flags"] & 1, fmt(fin))
s = send(mdv2.CMD_SET, 0, 0.0, 0.0)
r = wait_for(s, {mdv2.ST_DONE}, 2.0)
check("Laser+Marker aus gemeldet", r is not None and r["flags"] == 0, fmt(r))

# 8. Wiederholgenauigkeit: 5x dieselbe Stelle mit Anfahrt
vals = []
for i in range(5):
    _, _, fin, _ = measure(-20.0 if i % 2 else 25.0, flags=mdv2.F_APPROACH)
    _, _, fin, _ = measure(0.0, flags=mdv2.F_APPROACH)
    if fin and fin["status"] == mdv2.ST_DONE:
        vals.append((fin["pan"], fin["dist"]))
print("Wiederholung Pan 0 mit Anfahrt:", ", ".join(f"{p:.2f}/{d:.3f}" for p, d in vals))
if vals:
    ds = [d for _, d in vals]
    check("Streuung Distanz < 2 cm", max(ds) - min(ds) < 0.02, f"{(max(ds)-min(ds))*1000:.0f} mm")

print(f"\n{fails} Fehler")
sys.exit(1 if fails else 0)
