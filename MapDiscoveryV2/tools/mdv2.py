"""MapDiscoveryV2 - Protokoll-Helfer fuer die Python-Testwerkzeuge.

Spiegelt ../Common/MDV2Protocol.h. Nur Standardbibliothek.
"""
import struct

MAGIC = 0x3256444D
PORT_HEAD = 4210
PORT_KARTE = 4211

CMD_SET, CMD_MEASURE = 1, 2
F_LASER, F_MARKER, F_APPROACH = 0x01, 0x02, 0x04
ST_DONE, ST_ACCEPTED, ST_BUSY, ST_FAILED, ST_NOSENSOR = 0, 1, 2, 3, 4
ST_NAME = {0: "DONE", 1: "ACCEPTED", 2: "BUSY", 3: "FAILED", 4: "NOSENSOR"}
SENSOR_NAME = {0: "keiner", 1: "SEN0366", 2: "X40"}

CMD_FMT = "<IHBBff"          # 16 Bytes
REPLY_FMT = "<IHBBfffBBBB"   # 24 Bytes
assert struct.calcsize(CMD_FMT) == 16
assert struct.calcsize(REPLY_FMT) == 24


def pack_cmd(seq, cmd, flags, pan, tilt):
    return struct.pack(CMD_FMT, MAGIC, seq & 0xFFFF, cmd, flags, pan, tilt)


def unpack_cmd(data):
    if len(data) != 16:
        return None
    m, seq, cmd, flags, pan, tilt = struct.unpack(CMD_FMT, data)
    if m != MAGIC:
        return None
    return dict(seq=seq, cmd=cmd, flags=flags, pan=pan, tilt=tilt)


def pack_reply(seq, cmd, status, dist=0.0, pan=0.0, tilt=0.0, flags=0, sensor=1, tilt_ok=1):
    return struct.pack(REPLY_FMT, MAGIC, seq & 0xFFFF, cmd, status, dist, pan, tilt,
                       flags, sensor, tilt_ok, 0)


def unpack_reply(data):
    if len(data) != 24:
        return None
    m, seq, cmd, st, dist, pan, tilt, flags, sensor, tok, _ = struct.unpack(REPLY_FMT, data)
    if m != MAGIC:
        return None
    return dict(seq=seq, cmd=cmd, status=st, dist=dist, pan=pan, tilt=tilt,
                flags=flags, sensor=sensor, tilt_ok=tok)
