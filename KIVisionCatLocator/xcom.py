"""xCom 6.3 in Python - das Busprotokoll der CatFinder-ESP32, fuer den Pi.

Quelle der Wahrheit bleibt ``xComDef6_3.h``: dieses Modul liest die Datei und
holt sich daraus alle ``#define``-Konstanten (msgCodes, cmd-Codes, stg-/act-
Indizes, Geraete-IDs) und die Layouts aller ``__attribute__((packed))``-Structs.
Aendert sich das Protokoll im Header, zieht der Pi mit, sobald die neue Datei
neben diesem Modul liegt - es gibt keine zweite, von Hand gepflegte Kopie.

Leitung (wie auf den ESP32, alles little-endian):
    msgHeader  version(0x63) sender msgCode payloadLen timeStamp(int64)
    payload    payloadLen Bytes, Layout je msgCode (posPayload, hbPayload, ...)
Multicast 239.0.0.57:8266 (Broadcast), Unicast :23456, Text-Multicast :8300.
"""

import re
import socket
import struct
import threading
import time
from pathlib import Path

_CTYPES = {
    "uint8_t": "B", "int8_t": "b", "char": "b", "bool": "?", "byte": "B",
    "uint16_t": "H", "int16_t": "h", "uint32_t": "I", "int32_t": "i",
    "uint64_t": "Q", "int64_t": "q", "float": "f", "double": "d",
}


class XComDef:
    """Konstanten und Struct-Layouts aus xComDef6_3.h."""

    def __init__(self, path):
        text = Path(path).read_text(encoding="utf-8", errors="replace")
        # Kommentare weg, sonst stolpert der Struct-Parser ueber "// x; y"
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        self.defines = {k: int(v, 0) for k, v in
                        re.findall(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b", text)}
        self.defines.update({k: int(v, 0) for k, v in re.findall(
            r"(?:constexpr|static\s+const|const)\s+\w+\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;", text)})
        self._layouts = {}
        for name, body in re.findall(
                r"struct\s+__attribute__\s*\(\(\s*packed\s*\)\)\s+(\w+)\s*\{(.*?)\}\s*;",
                text, re.S):
            fields = []
            for ctype, fname in re.findall(r"(\w+)\s+(\w+)\s*;", body):
                if ctype in _CTYPES:
                    fields.append((fname, _CTYPES[ctype]))
                elif ctype in self._layouts:          # eingebettetes Struct (hbPayload hb;)
                    fields.extend((fname + "." + sub, fmt) for sub, fmt in self._layouts[ctype])
                else:
                    raise ValueError("xComDef: Typ %s in %s unbekannt" % (ctype, name))
            self._layouts[name] = fields
        self.structs = {name: (struct.Struct("<" + "".join(f for _, f in fields)),
                               [n for n, _ in fields])
                        for name, fields in self._layouts.items()}
        self.header = self.structs["msgHeader"][0]

    def __getattr__(self, name):
        try:
            return self.__dict__["defines"][name]
        except KeyError:
            raise AttributeError(name) from None

    def size(self, name):
        return self.structs[name][0].size

    def pack(self, name, **values):
        st, names = self.structs[name]
        return st.pack(*(values.get(n, 0) for n in names))

    def unpack(self, name, data):
        """dict oder None, wenn die Laenge nicht passt (wie getPayload())."""
        st, names = self.structs[name]
        if len(data) != st.size:
            return None
        return dict(zip(names, st.unpack(data)))

    def unpack_prefix(self, name, data):
        """Nur den Anfang lesen - fuer die HB-Varianten mit gemeinsamem Kopf."""
        st, names = self.structs[name]
        if len(data) < st.size:
            return None
        return dict(zip(names, st.unpack(data[:st.size])))


def local_ip(probe="192.168.0.1"):
    """Eigene IP im Heimnetz (ohne dass ein Paket rausgeht)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((probe, 9))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


class Message:
    __slots__ = ("sender", "code", "stamp", "payload", "src", "unicast")

    def __init__(self, sender, code, stamp, payload, src, unicast):
        self.sender, self.code, self.stamp = sender, code, stamp
        self.payload, self.src, self.unicast = payload, src, unicast


class Bus:
    """UDP-Sockets des xCom-Busses: senden, empfangen, Text-Multicast.

    handler(msg) wird fuer jede gueltige Nachricht in einem eigenen Thread pro
    Socket aufgerufen (Multicast und Unicast getrennt). Eigene Pakete kommen
    nicht zurueck (Multicast-Loop aus).
    """

    MC_GROUP = "239.0.0.57"
    NET = "192.168.0."

    def __init__(self, defs, my_id):
        self.x = defs
        self.id = my_id
        self.ip = local_ip() or "0.0.0.0"
        self.device_ips = {}          # sender -> letztes Oktett (aus HB gelernt)
        self.stats = {"tx": 0, "rx": 0, "bad": 0, "last_rx": 0.0}
        self._stop = threading.Event()
        self._tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        self._tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
        self._set_mc_if()
        self._mc = None
        self._uc = None
        self._handler = None

    @property
    def octet(self):
        try:
            return int(self.ip.rsplit(".", 1)[1])
        except (ValueError, IndexError):
            return 0

    def _set_mc_if(self):
        try:
            self._tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                                socket.inet_aton(self.ip))
        except OSError:
            pass

    # -- Empfang -----------------------------------------------------------
    def start(self, handler):
        self._handler = handler
        self._mc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self._mc.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._mc.bind(("", self.x.MC_PORT))
        self._join()
        self._mc.settimeout(1.0)
        self._uc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._uc.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._uc.bind(("", self.x.UC_PORT))
        self._uc.settimeout(1.0)
        for sock, uni in ((self._mc, False), (self._uc, True)):
            threading.Thread(target=self._rx_loop, args=(sock, uni), daemon=True).start()

    def _join(self):
        """Gruppe (erneut) beitreten. Nach einem WLAN-Abriss verliert Linux die
        Mitgliedschaft; deshalb ruft der Knoten das periodisch auf."""
        ip = local_ip()
        if ip and ip != self.ip:
            self.ip = ip
            self._set_mc_if()
        mreq = socket.inet_aton(self.MC_GROUP) + socket.inet_aton(self.ip)
        try:
            self._mc.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        except OSError:
            pass                      # EADDRINUSE: schon Mitglied

    def rejoin(self):
        if self._mc is not None:
            self._join()

    def _rx_loop(self, sock, unicast):
        hsize = self.x.header.size
        while not self._stop.is_set():
            try:
                data, addr = sock.recvfrom(512)
            except socket.timeout:
                continue
            except OSError:
                time.sleep(1.0)
                continue
            if len(data) < hsize:
                self.stats["bad"] += 1
                continue
            ver, sender, code, plen, stamp = self.x.header.unpack(data[:hsize])
            # dieselben Pruefungen wie parseXMsg() auf den ESP32
            if (ver != self.x.XCOM_VERSION or sender >= self.x.deviceCount
                    or plen > self.x.maxPayloadLen or len(data) != hsize + plen):
                self.stats["bad"] += 1
                continue
            if sender == self.id and addr[0] == self.ip:
                continue
            payload = data[hsize:]
            if code == self.x.HB:
                hb = self.x.unpack_prefix("hbPayload", payload)
                if hb:
                    self.device_ips[sender] = hb["ip"]
            self.stats["rx"] += 1
            self.stats["last_rx"] = time.time()
            try:
                self._handler(Message(sender, code, stamp, payload, addr[0], unicast))
            except Exception as exc:                          # noqa: BLE001
                print("[bus] Handler-Fehler: %s" % exc, flush=True)

    # -- Senden ------------------------------------------------------------
    def _frame(self, code, payload):
        if len(payload) > self.x.maxPayloadLen:
            raise ValueError("Payload zu lang (%d)" % len(payload))
        head = self.x.header.pack(self.x.XCOM_VERSION, self.id, code, len(payload),
                                  int(time.time()))
        return head + payload

    def broadcast(self, code, payload=b""):
        try:
            self._tx.sendto(self._frame(code, payload), (self.MC_GROUP, self.x.MC_PORT))
            self.stats["tx"] += 1
            return True
        except OSError as exc:
            print("[bus] Broadcast fehlgeschlagen: %s" % exc, flush=True)
            return False

    def unicast(self, code, payload, octet):
        if not octet:
            return False
        try:
            self._tx.sendto(self._frame(code, payload), (self.NET + str(octet), self.x.UC_PORT))
            self.stats["tx"] += 1
            return True
        except OSError as exc:
            print("[bus] Unicast fehlgeschlagen: %s" % exc, flush=True)
            return False

    def text(self, line):
        """Debug-Zeile auf den Text-Multicast (erscheint im VPS-Debugfenster)."""
        try:
            self._tx.sendto((line + "\r\n").encode("utf-8", "replace"),
                            (self.MC_GROUP, self.x.MC_Text_PORT))
        except OSError:
            pass

    def close(self):
        self._stop.set()
