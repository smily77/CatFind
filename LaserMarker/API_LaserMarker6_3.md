# LaserMarker 6_3 — Netzwerk-API (vollständige Spezifikation)

Diese Spezifikation beschreibt **byte-genau**, wie der LaserMarker über das
Netzwerk gesteuert und ausgelesen wird. Sie ist bewusst so detailliert, dass
**jedes** Programm — auch eines, das nicht zur CatFind-Serie gehört und die
gemeinsamen Header (`xComDef6_3.h` / `xComProc6_3.h`) nicht verwendet — den
Marker vollständig bedienen kann, indem es die UDP-Pakete selbst zusammenbaut.

> Es wird **kein** spezielles SDK benötigt. Wer nur Sockets und das Packen von
> Bytes beherrscht (Python `struct`, C `memcpy`, …), kann den Marker steuern.

---

## 1. Gerät auf einen Blick

| Eigenschaft | Wert |
|---|---|
| Board | ESP32-C3 Super Mini |
| Geräte-ID (`sender` in seinen Heartbeats) | **15** |
| Gerätetyp | `Marker` (8) |
| Feste IP-Adresse | **192.168.0.182** |
| Steuerbare Ausgänge | mainLaser, subLaser, Aux (je an/aus), Pixelfarbe (RGB) |

### Steuerbare Funktionen

| Funktion | Hardware | Zustand |
|---|---|---|
| **mainLaser** | GPIO3 → TTL-Laser-Modul | aus / an |
| **subLaser** | GPIO21 → BS170-MOSFET → Laserdiode | aus / an |
| **Aux** | GPIO7 → frei verwendbarer Schaltausgang | aus / an |
| **Pixel** | GPIO10 → 1× WS2812 NeoPixel | Farbe 0x000000…0xFFFFFF |

---

## 2. Transport

Alles läuft über **UDP/IPv4**. Es gibt zwei Richtungen:

| Richtung | Zweck | Ziel-Adresse | Port |
|---|---|---|---|
| **Steuern** (zum Marker) | Kommandos senden | `192.168.0.182` (Unicast) | **23456** |
| **Steuern** (an alle Marker) | Kommando an alle Marker gleichzeitig | `239.0.0.57` (Multicast) | **8266** |
| **Auslesen** (vom Marker) | Zustand/Heartbeat empfangen | `239.0.0.57` (Multicast) | **8266** |

- **Kommandos** schickt man üblicherweise als **Unicast** an die feste IP des
  Markers (Port 23456).
- Wer mehrere Marker auf einmal schalten will (z.B. „alle Laser aus"), sendet
  dasselbe Kommando-Paket per **Multicast** an `239.0.0.57:8266`.
- Den **Zustand** liest man, indem man der Multicast-Gruppe `239.0.0.57:8266`
  beitritt und auf die Heartbeats des Markers hört (siehe Kap. 6).

UDP ist verbindungslos und ungesichert: Pakete können verloren gehen. Es gibt
**keine** ACK-Antwort ausser dem Heartbeat. Wer Zustellung sicherstellen will,
sendet ein Kommando mehrfach oder prüft den Zustand über den Heartbeat.

---

## 3. Allgemeines Nachrichtenformat (Protokoll 6_3)

Jede Nachricht besteht aus einem **fixen 12-Byte-Header** und einem
**Payload** variabler Länge. Alle Mehrbyte-Felder sind **Little-Endian**
(niedrigstwertiges Byte zuerst — ESP32-Konvention). Alle Strukturen sind
**ohne Padding** gepackt.

### 3.1 Header (`msgHeader`, 12 Bytes)

| Offset | Größe | Feld | Typ | Bedeutung |
|---:|---:|---|---|---|
| 0 | 1 | `version` | uint8 | **immer 0x63**. Pakete mit anderer Version werden verworfen. |
| 1 | 1 | `sender` | uint8 | Geräte-ID des Absenders. Bei Kommandos vom Marker **ignoriert** (frei wählbar). In den Heartbeats des Markers = **15**. |
| 2 | 1 | `msgCode` | uint8 | Nachrichtenart (siehe 3.3). |
| 3 | 1 | `payloadLen` | uint8 | Länge des Payloads in Bytes (0…64). |
| 4 | 8 | `timeStamp` | int64 | Unix-Zeit des Absenders. Bei eingehenden Kommandos **ignoriert** → darf 0 sein. |

### 3.2 Validierung durch den Marker

Ein eingehendes Paket wird **nur dann** verarbeitet, wenn **alle** Bedingungen
erfüllt sind (sonst wird es kommentarlos verworfen):

1. Gesamtlänge ≥ 12 (Header passt),
2. `version` == `0x63`,
3. `payloadLen` ≤ 64,
4. Gesamtlänge == `12 + payloadLen` (exakt).

Für ein Kommando bedeutet das konkret: **Gesamtlänge exakt 17 Bytes**,
`payloadLen` == 5.

### 3.3 Relevante `msgCode`-Werte

| msgCode | Wert | Verwendung am Marker |
|---|---:|---|
| `HB` | 1 | wird vom Marker **gesendet** (Zustand/Heartbeat) |
| `commandMsg` | 5 | wird vom Marker **empfangen** (Steuerung) |

(Andere Codes wie `catObserved`=2 existieren im System, sind für den Marker
aber bedeutungslos und werden ignoriert.)

---

## 4. Steuern: das Kommando-Paket

Ein Kommando ist eine Nachricht mit `msgCode = 5` (`commandMsg`) und einem
**`cmdPayload`** von **5 Bytes**.

### 4.1 Payload `cmdPayload` (5 Bytes)

| Offset (im Payload) | Größe | Feld | Typ | Bedeutung |
|---:|---:|---|---|---|
| 0 | 1 | `cmd` | uint8 | Kommando-Code (siehe 4.3) |
| 1 | 4 | `info` | int32 | Parameter (Little-Endian) |

### 4.2 Vollständiges Kommando-Paket (17 Bytes)

| Byte(s) | Inhalt | Wert |
|---:|---|---|
| 0 | version | `0x63` |
| 1 | sender | beliebig (z.B. `0xF0`) — wird ignoriert |
| 2 | msgCode | `0x05` (commandMsg) |
| 3 | payloadLen | `0x05` |
| 4–11 | timeStamp | beliebig, z.B. alles `0x00` |
| 12 | cmd | Kommando-Code |
| 13–16 | info | int32, Little-Endian |

### 4.3 Kommando-Codes

| Funktion | `cmd` (dez / hex) | `info` | Wirkung |
|---|---:|---|---|
| mainLaser schalten | 11 / `0x0B` | 0 = aus, ≠0 = an | setzt mainLaser |
| subLaser schalten | 12 / `0x0C` | 0 = aus, ≠0 = an | setzt subLaser |
| Aux schalten | 13 / `0x0D` | 0 = aus, ≠0 = an | setzt Aux |
| Pixelfarbe setzen | 14 / `0x0E` | `0x00RRGGBB` | setzt die NeoPixel-Farbe |
| Zustand abfragen | 15 / `0x0F` | (egal) | ändert nichts, erzwingt sofortigen Heartbeat |

**Farbformat (`cmdPixelColor`):** `info` ist eine 24-Bit-Farbe im Schema
`0x00RRGGBB` (R = Bits 16–23, G = Bits 8–15, B = Bits 0–7), Standard-RGB.
Beispiel: Orange = `0xFF8000` (R=255, G=128, B=0). Die geräteinterne
Farbreihenfolge des WS2812 wird von der Firmware behandelt — extern immer
normales **RGB** angeben.

**Reaktion:** Nach jedem **erkannten** Kommando wendet der Marker den neuen
Zustand auf die Hardware an und sendet **sofort einen Heartbeat** mit dem
kompletten Zustand (siehe Kap. 6) — das dient als Bestätigung. Unbekannte
`cmd`-Werte werden ignoriert (kein Heartbeat).

### 4.4 Konkrete Byte-Beispiele

Jeweils 17 Bytes, Hex, timeStamp = 0:

| Aktion | Bytes (hex) |
|---|---|
| mainLaser **an** | `63 F0 05 05  00 00 00 00 00 00 00 00  0B  01 00 00 00` |
| mainLaser **aus** | `63 F0 05 05  00 00 00 00 00 00 00 00  0B  00 00 00 00` |
| subLaser **an** | `63 F0 05 05  00 00 00 00 00 00 00 00  0C  01 00 00 00` |
| Aux **an** | `63 F0 05 05  00 00 00 00 00 00 00 00  0D  01 00 00 00` |
| Pixel **orange** (0xFF8000) | `63 F0 05 05  00 00 00 00 00 00 00 00  0E  00 80 FF 00` |
| Pixel **aus** (0x000000) | `63 F0 05 05  00 00 00 00 00 00 00 00  0E  00 00 00 00` |
| Zustand abfragen | `63 F0 05 05  00 00 00 00 00 00 00 00  0F  00 00 00 00` |

> Hinweis zur Farbe orange: `info = 0x00FF8000`. Little-Endian abgelegt ergibt
> das die Bytes `00 80 FF 00`.

---

## 5. Beispiel: ein Kommando-Paket erzeugen

### 5.1 Python (ohne jede CatFind-Abhängigkeit)

```python
import socket, struct

MARKER_IP  = "192.168.0.182"
UC_PORT    = 23456

VERSION     = 0x63
MSG_COMMAND = 5

CMD_MAIN_LASER  = 11
CMD_SUB_LASER   = 12
CMD_AUX         = 13
CMD_PIXEL_COLOR = 14
CMD_STATE       = 15

def build_command(cmd, info, sender=0xF0, timestamp=0):
    # Header (12 Bytes): version, sender, msgCode, payloadLen, timeStamp(int64)
    header  = struct.pack("<BBBBq", VERSION, sender, MSG_COMMAND, 5, timestamp)
    # cmdPayload (5 Bytes): cmd(uint8), info(int32)
    payload = struct.pack("<Bi", cmd, info)
    return header + payload            # genau 17 Bytes

def send(cmd, info):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(build_command(cmd, info), (MARKER_IP, UC_PORT))
    s.close()

# --- Anwendung ---
send(CMD_MAIN_LASER,  1)          # mainLaser an
send(CMD_PIXEL_COLOR, 0xFF8000)   # Pixel orange
send(CMD_AUX,         0)          # Aux aus
send(CMD_MAIN_LASER,  0)          # mainLaser aus
```

> Das Format `"<BBBBq"` (Little-Endian, ohne Alignment) ergibt exakt die
> 12 gepackten Header-Bytes; `"<Bi"` exakt die 5 Payload-Bytes.

### 5.2 C / C++ (ohne die gemeinsamen Header)

```c
#include <stdint.h>
#include <string.h>

// Erzeugt ein 17-Byte-Kommando in buf. Rueckgabe: Laenge (17).
int build_command(uint8_t *buf, uint8_t cmd, int32_t info) {
    buf[0] = 0x63;                 // version
    buf[1] = 0xF0;                 // sender (egal)
    buf[2] = 5;                    // msgCode = commandMsg
    buf[3] = 5;                    // payloadLen
    memset(&buf[4], 0, 8);         // timeStamp = 0
    buf[12] = cmd;                 // cmd
    memcpy(&buf[13], &info, 4);    // info, Little-Endian (auf LE-Host direkt)
    return 17;
}
// danach per UDP an 192.168.0.182:23456 senden (sendto()).
```

### 5.3 Mit der CatFind-Bibliothek (zur Vollständigkeit)

```cpp
cmdPayload c;
c.cmd = cmdMainLaser; c.info = 1;
unicastMsg(commandMsg, c, device[LaserMarker].IP);   // an 192.168.0.182
```

---

## 6. Auslesen: der Heartbeat (Zustand)

Der Marker sendet seinen **kompletten Zustand** als Heartbeat
(`msgCode = HB = 1`) per **Multicast an `239.0.0.57:8266`**:

- zyklisch alle **5000 ms** (`periodeForHB`), **und**
- **sofort** nach jedem akzeptierten Kommando, **und**
- einmal direkt nach dem Start.

So lässt sich der aktuelle Zustand jederzeit verifizieren; mit `cmdMarkerState`
(cmd 15) kann ein sofortiger Heartbeat angefordert werden.

### 6.1 Payload `markerHbPayload` (11 Bytes)

| Offset (im Payload) | Größe | Feld | Typ | Bedeutung |
|---:|---:|---|---|---|
| 0 | 1 | `ip` | uint8 | letztes Oktett der eigenen IP (182) |
| 1 | 4 | `HBperiode` | uint32 | Heartbeat-Periode in ms (5000) |
| 5 | 1 | `mainLaser` | uint8 | 0/1 |
| 6 | 1 | `subLaser` | uint8 | 0/1 |
| 7 | 1 | `aux` | uint8 | 0/1 |
| 8 | 1 | `r` | uint8 | Pixel-Rot 0…255 |
| 9 | 1 | `g` | uint8 | Pixel-Grün 0…255 |
| 10 | 1 | `b` | uint8 | Pixel-Blau 0…255 |

### 6.2 Vollständiges Heartbeat-Paket (23 Bytes)

| Byte(s) | Inhalt | erwarteter Wert |
|---:|---|---|
| 0 | version | `0x63` |
| 1 | sender | `0x0F` (15 = LaserMarker) |
| 2 | msgCode | `0x01` (HB) |
| 3 | payloadLen | `0x0B` (11) |
| 4–11 | timeStamp | Unix-Zeit (0, falls keine NTP-Zeit gesetzt) |
| 12 | ip | `0xB6` (182) |
| 13–16 | HBperiode | `88 13 00 00` (= 5000) |
| 17 | mainLaser | 0/1 |
| 18 | subLaser | 0/1 |
| 19 | aux | 0/1 |
| 20 | r | 0…255 |
| 21 | g | 0…255 |
| 22 | b | 0…255 |

Ein Fremdprogramm erkennt einen Marker-Heartbeat eindeutig an
`version==0x63 && msgCode==1 && sender==15 && payloadLen==11`.

### 6.3 Python-Beispiel: Zustand mithören

```python
import socket, struct

MC_GROUP, MC_PORT = "239.0.0.57", 8266
VERSION, MSG_HB, MARKER_ID = 0x63, 1, 15

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", MC_PORT))
mreq = struct.pack("4sl", socket.inet_aton(MC_GROUP), socket.INADDR_ANY)
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

while True:
    data, _ = s.recvfrom(256)
    if len(data) < 12:
        continue
    ver, sender, code, plen, ts = struct.unpack("<BBBBq", data[:12])
    if ver != VERSION or code != MSG_HB or sender != MARKER_ID or plen != 11:
        continue
    ip, per, ml, sl, aux, r, g, b = struct.unpack("<BIBBBBBB", data[12:23])
    print(f"Marker .{ip}: main={ml} sub={sl} aux={aux} rgb=({r},{g},{b}) periode={per}ms")
```

---

## 7. Verhalten & Sicherheitshinweise

- **Startzustand:** Nach Boot/Reset sind alle Ausgänge **aus** und das Pixel
  **dunkel**. Ein Reset entschärft den Marker also automatisch.
- **Kein Auto-Timeout:** Ein eingeschalteter Laser **bleibt an**, bis er
  explizit ausgeschaltet wird oder das Gerät neu startet. Ein steuerndes
  Programm ist für das rechtzeitige Ausschalten verantwortlich (z.B.
  „an"-Kommando senden, nach gewünschter Dauer „aus"-Kommando senden).
- **Laserschutz:** mainLaser/subLaser sind echte Laser. Steuerprogramme sollten
  unbeabsichtigtes Dauereinschalten vermeiden (Watchdog/Timeout im Controller).
- **Idempotent:** Mehrfaches Senden desselben Kommandos ist unkritisch
  (Zustand wird nur gesetzt). Bei Paketverlust einfach erneut senden.
- **Keine Authentifizierung:** Jeder im selben WLAN kann Kommandos senden.
  Für den Garteneinsatz unkritisch, aber bewusst so.

---

## 8. Schnellreferenz (Cheat-Sheet)

```
Kommando senden  -> UDP Unicast  192.168.0.182 : 23456   (17 Bytes)
Alle Marker      -> UDP Multicast 239.0.0.57   : 8266    (17 Bytes)
Zustand hören    <- UDP Multicast 239.0.0.57   : 8266    (23 Bytes, sender=15)

Kommando-Paket (17 B):
  63 | SS | 05 | 05 | TT TT TT TT TT TT TT TT | CC | II II II II
  ^version ^sender ^msgCode ^payloadLen ^timeStamp(int64 LE) ^cmd ^info(int32 LE)

cmd:  11=mainLaser 12=subLaser 13=Aux 14=PixelColor 15=stateAbfrage
info: an/aus -> 1/0 ;  Farbe -> 0x00RRGGBB

Alle Mehrbyte-Werte: Little-Endian. Strukturen ohne Padding (gepackt).
```
