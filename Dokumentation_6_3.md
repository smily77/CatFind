# CatFind 6.3 — Systembeschreibung

Stand: 2026-06-12 · gilt für die 6_3-Programmversionen (Protokoll 0x63)

CatFind ist ein verteiltes System aus ESP32-Geräten, das eine Katze auf dem
Rasen erkennt, ihre Position bestimmt und sie mit einem gezielten Wasserstrahl
vertreibt. Die Geräte kommunizieren untereinander über WLAN mit einem
selbstdefinierten UDP-Protokoll (fixer Header + variabler Payload).

---

## 1. Überblick: Wer macht was

```
                    ┌──────────────────────── WLAN (192.168.0.x) ────────────────────────┐
                    │            UDP-Multicast 239.0.0.57:8266  ("alle hören mit")       │
                    │            UDP-Unicast  Port 23456        ("gezielt an ein Gerät") │
                    └─────────────────────────────────────────────────────────────────────┘
                         ▲              ▲               ▲              ▲             ▲
   Sensoren              │              │               │              │             │
  ┌──────────────┐  catObserved   ┌───────────┐   commandMsg     ┌──────────┐   HB  ┌──────────┐
  │ Radar (Dome/ │ ─────────────► │ PowerActor│ ◄─────────────── │ Display  │ ◄──── │ Manager  │
  │ MiniDome/    │   (Broadcast)  │  PA2i     │  (Unicast:       │ Udisp    │       │ (Status- │
  │ CompactDome) │                │ Servo +   │   Limits setzen, │ (Karte,  │       │  lampe,  │
  └──────────────┘                │ Ventil +  │   armFire)       │  Menü)   │       │  Log)    │
  ┌──────────────┐  catObserved   │ Laser-    │                  └──────────┘       └──────────┘
  │ Lidar LD06   │ ─────────────► │ Distanz   │ ◄─────────────── ┌──────────┐
  └──────────────┘                └───────────┘    cmdArmFire    │ Button   │
  ┌──────────────┐                      │          (Unicast)     │ (Scharf- │
  │ Simulator    │ ◄── zeichnet auf ────┘                        │ schalten)│
  │ (Cardputer)  │ ──── spielt ab ──► catObserved (Broadcast)    └──────────┘
  └──────────────┘
```

**Die Kette im Normalbetrieb:**

1. Ein **Sensor** (Radar oder Lidar) erkennt ein Objekt auf dem Rasen und
   broadcastet die Position als `catObserved`-Nachricht.
2. Der **PowerActor (PA2i)** empfängt die Position, richtet seinen Servo auf
   das Ziel, verifiziert die Distanz mit dem Laser-Distanzmesser und löst —
   wenn scharfgeschaltet und das Ziel innerhalb der Limits liegt — das
   Wasserventil aus. (Die Schiesslogik ist in 6_3 noch auskommentiert /
   in Arbeit, siehe REVIEW_6_3.md im PA2i-Ordner.)
3. **Display**, **Manager** und **Simulator** hören denselben Broadcast mit:
   das Display zeichnet die Position auf eine Karte, der Manager blinkt blau,
   der Simulator kann die Szene auf SD aufzeichnen.
4. Der **Button** schaltet den PowerActor per Unicast-Kommando scharf/unscharf;
   der aktuelle Zustand kommt als Heartbeat zurück und wird über die LED-Farbe
   angezeigt.
5. Alle Geräte senden periodisch einen **Heartbeat (HB)** — daraus lernen alle
   anderen automatisch die IP-Adressen (für Unicast) und sehen, wer lebt.

---

## 2. Geräte-Datenbank (device dB)

In `xComDef6_3.h` ist jedes Gerät mit einer festen ID (Array-Index) eingetragen:

| ID | Name (define) | Typ | feste IP (letztes Oktett) | Hardware |
|---:|---|---|---:|---|
| 0 | Manager | MananagementDevice | 180 | ESP32 + 24er-LED-Ring |
| 1 | Dome | HLK | DHCP | ESP32 + HLK-Radar |
| 2 | MiniDome | HLK | DHCP | wie Dome, ohne Pixel |
| 3 | CompactDome | HLK | DHCP | M5 PicoDome |
| 4 | PA2i | PowerActor | 181 | ESP32 + Servo + Ventil + Laser |
| 5 | Disp7 | Screen | DHCP | Sunton 7" |
| 6 | CYD | Controller | DHCP | CYD 2.8" + Drehencoder |
| 7 | LD06 | Lidar | DHCP | ESP32 + LD06-Lidar |
| 8 | Schalter | onOffSchalter | DHCP | M5 Atom (Button) |
| 9 | Disp5 | Screen | DHCP | Sunton 5" + 8-Encoder |
| 10 | Core2 | Screen | DHCP | M5 Core2 + 8-Encoder |
| 11 | Tab5 | Screen | DHCP | M5 Tab5 |
| 12 | CYD35Z | Screen | DHCP | CYD 3.5" |
| 13 | Wave7z | Screen | DHCP | Waveshare 7" |
| 14 | Sim | MananagementDevice | DHCP | M5 Cardputer (neu in 6_3) |
| 15 | LaserMarker | Marker | 182 | ESP32-C3, Laser-Marker (siehe LaserMarker/API_LaserMarker6_3.md) |
| 16 | PA1_1 | PowerActor | 183 | älterer PowerActor mit Schrittmotor (PCF8574/A4988), Drehturm (siehe PowerActor1_1/PA1_1_6_3) |

- Das Feld `IP` enthält das **letzte Oktett** der Adresse (Netz ist fest
  192.168.0.x). Bei Geräten mit Eintrag 150–195 konfiguriert `setUpWifi()`
  eine statische IP, alle anderen nutzen DHCP.
- Empfängt ein Gerät einen HB, trägt es das im HB gemeldete Oktett automatisch
  in seine lokale Kopie der device dB ein (`device[sender].IP`). So kennt z.B.
  der Button die Adresse des PA2i, ohne dass sie irgendwo konfiguriert wäre.
- `Name` wird als OTA-Hostname verwendet (`setUpOTA()`), `type` dient zur
  Gruppierung (z.B. reagiert der Button auf HBs von **allen** PowerActor-Geräten,
  nicht auf eine bestimmte ID).

---

## 3. Das 6.3-Nachrichtenformat (fixer Header + variabler Payload)

### 3.1 Grundidee

In 6_2 hatten alle Multicast-Nachrichten denselben Struct (`mcDataStruct`),
in dem die Felder für alle Nachrichtenarten in einem verschachtelten `union`
übereinanderlagen — jedes Paket war gleich gross, der Inhalt je nach `msgCode`
anders zu interpretieren, und Kommandos hatten ein zweites, inkompatibles
Format (`ucDataStruct`).

In 6_3 besteht **jede** Nachricht (egal ob Broadcast oder Unicast) aus:

```
┌────────────────── msgHeader (12 Bytes, fix) ──────────────────┬─── Payload (0..64 Bytes, variabel) ───┐
│ version │ sender │ msgCode │ payloadLen │     timeStamp       │  Struct passend zum msgCode            │
│ 1 Byte  │ 1 Byte │ 1 Byte  │  1 Byte    │  8 Bytes (int64)    │  genau payloadLen Bytes                │
└─────────┴────────┴─────────┴────────────┴─────────────────────┴────────────────────────────────────────┘
```

| Header-Feld | Bedeutung |
|---|---|
| `version` | konstant `0x63`. Empfänger verwerfen Pakete mit fremder Version — dadurch stören sich 6_2- und 6_3-Geräte nicht gegenseitig (sie ignorieren sich). |
| `sender` | Geräte-ID des Absenders (Index in der device dB) |
| `msgCode` | Nachrichtenart (siehe 3.2) |
| `payloadLen` | Länge des folgenden Payloads in Bytes. Erlaubt Validierung und zukünftige Erweiterung. |
| `timeStamp` | `time_t` des Absenders (Sekunden seit 1970, via NTP). |

Wichtig: **Der Header wird beim Senden automatisch gefüllt** (`sendXMsg()` in
xComProc6_3.h). Ein Programm setzt nur noch die Payload-Felder und ruft
`broadcastMsg(...)` bzw. `unicastMsg(...)` auf — sender, timeStamp, Version und
Länge kann es gar nicht mehr falsch machen.

Alle Structs sind `__attribute__((packed))` — das Leitungsformat ist damit
exakt definiert (Little-Endian, da alle Geräte ESP32 sind):
msgHeader = 12, hbPayload = 5, radarHbPayload = 9, pa2HbPayload = 23,
posPayload = 25, cmdPayload = 5 Bytes.

### 3.2 Nachrichtenarten und ihre Payloads

| msgCode | Wert | Payload | Richtung (typisch) | Bedeutung |
|---|---:|---|---|---|
| `HB` | 1 | `hbPayload` / `pa2HbPayload` / `radarHbPayload` | Broadcast | Lebenszeichen + Zustandsdaten |
| `catObserved` | 2 | `posPayload` | Broadcast | Sensor hat ein Ziel erkannt |
| `measurement` | 3 | `posPayload` | Broadcast | Messpunkt (z.B. Lidar-Scan des PA2) |
| `catHit` | 4 | `posPayload` | Broadcast | PA2 hat auf diese Position geschossen |
| `commandMsg` | 5 | `cmdPayload` | Unicast | Steuerkommando an ein bestimmtes Gerät |

**posPayload** (25 Bytes) — Positionsmeldung:

| Feld | Typ | Bedeutung |
|---|---|---|
| `x`, `y` | int32 | kartesische Position in mm (siehe Koordinatensysteme, Kap. 4) |
| `radius` | float | Distanz vom PA2-Drehpunkt in mm |
| `angle` | float | Winkel in PA-Einheiten (0..4096, siehe Kap. 4) |
| `targetSpeed` | int32 | Zielgeschwindigkeit (nur Radar, cm/s) |
| `res` | int32 | Distanz-Auflösungswert des Radars |
| `sensor` | uint8 | Target-/Sensorindex (Radar liefert bis zu 3 Targets: 0..2) |

**hbPayload** (5 Bytes) — Basis-Heartbeat, steht bei *allen* HB-Varianten am Anfang:

| Feld | Typ | Bedeutung |
|---|---|---|
| `ip` | uint8 | letztes Oktett der eigenen IP (für das automatische IP-Lernen) |
| `HBperiode` | uint32 | eigene Sendeperiode in ms (Empfänger können daraus "Gerät tot?" ableiten) |

**pa2HbPayload** (23 Bytes) = hbPayload + Zustand des PowerActors:

| Feld | Typ | Bedeutung |
|---|---|---|
| `readyToFire` | uint8 | 1 = scharfgeschaltet |
| `limitsActive` | uint8 | 1 = Schussfeld-Begrenzung aktiv |
| `leftLimit`, `rightLimit` | float | Winkelgrenzen in PA-Einheiten |
| `farLimit`, `nearLimit` | float | Distanzgrenzen in mm |

**radarHbPayload** (9 Bytes) = hbPayload + `deadZoneDist` (float, Totzonenradius
des Radars in mm).

**cmdPayload** (5 Bytes) — Kommandos (ersetzt den alten ucDataStruct):

| Feld | Typ | Bedeutung |
|---|---|---|
| `cmd` | uint8 | Kommando-Code (siehe unten) |
| `info` | int32 | Parameter zum Kommando |

| cmd-Code | Wert | Wirkung beim PA2i | info |
|---|---:|---|---|
| `cmdRichtung` | 1 | (reserviert: Servo drehen) | Winkel |
| `cmdLaser` | 2 | (reserviert: Laser an/aus) | — |
| `cmdAdjustAngle` | 3 | (reserviert: Winkelkorrektur) | Offset |
| `cmdTaste` | 4 | (reserviert) | — |
| `cmdArmFire` | 5 | readyToFire umschalten | — |
| `cmdSetLeftLimit` | 6 | linke Winkelgrenze setzen + speichern | PA-Winkel |
| `cmdSetRightLimit` | 7 | rechte Winkelgrenze setzen + speichern | PA-Winkel |
| `cmdSetFarLimit` | 8 | maximale Schussdistanz setzen + speichern | mm |
| `cmdSetNearLimit` | 9 | minimale Schussdistanz setzen + speichern | mm |
| `cmdChangeLimitActivation` | 10 | Limits aktiv/inaktiv umschalten + speichern | — |

(Die Codes hiessen in 6_2 `laser`, `taste`, `armFire` … — sie wurden mit dem
`cmd`-Präfix versehen, weil die alten Namen mit Pin-defines in den
hwDef-Dateien kollidierten.)

### 3.3 Warum HB-Varianten unterschiedlicher Länge?

Der Trick des 6_3-Designs: alle drei HB-Payloads beginnen mit demselben
`hbPayload`. Ein Empfänger, den nur "wer lebt, welche IP" interessiert
(z.B. das automatische IP-Lernen in `initMcUdp()`), liest mit
`getHbPayload()` nur die ersten 5 Bytes — egal welcher Gerätetyp gesendet
hat. Ein Empfänger, der mehr wissen will (Button, Display), versucht mit
`getPayload(msg, pa2HbPayload)` die Vollversion zu lesen; das schlägt per
Längenvergleich fehl, wenn die Nachricht von einem anderen Gerätetyp kam.
So ersetzt die Payload-Länge den alten Union — ohne dass je ein Feld
fehlinterpretiert werden kann.

### 3.4 Transportwege

| Kanal | Adresse/Port | Zweck |
|---|---|---|
| Multicast ("Broadcast") | 239.0.0.57 : **8266** | alle Sensor-/Statusmeldungen; jedes Gerät hört mit |
| Unicast | 192.168.0.x : **23456** | gezielte Kommandos an genau ein Gerät |
| Text-Multicast | 239.0.0.57 : **8300** | freie Textmeldungen (`sendUdpTextln`) für Debug |

Beide Binärkanäle verwenden **dasselbe** xMsg-Format; der einzige Unterschied
ist die Zieladresse. Sender brauchen für Unicast nur die Geräte-ID:
`unicastMsg(commandMsg, cmd, device[PA2i].IP)` — das letzte Oktett kommt aus
der (per HB gelernten) device dB; ist es noch 0 (Gerät nie gesehen), gibt
`unicastMsg()` `false` zurück und sendet nichts.

### 3.5 Empfangsweg im Detail

Der Empfang läuft über **AsyncUDP-Callbacks** (Interrupt-/LwIP-Task-Kontext),
nicht über Polling:

1. `initMcUdp()` / `initUnicast()` registrieren je einen `onPacket`-Callback.
2. Der Callback ruft `parseXMsg()` auf: Mindestlänge, Versionsbyte,
   payloadLen ≤ 64 und Gesamtlänge == Header + payloadLen werden geprüft.
   Alles andere wird kommentarlos verworfen (z.B. 6_2-Pakete oder Fremdverkehr).
3. Bei HB wird sofort die IP in die device dB übernommen.
4. Die Nachricht landet in `lastMcMsg` bzw. `lastUcMsg`, das Flag
   `mcDataReceived` / `ucDataReceived` wird gesetzt.
5. Die `loop()` des Sketches prüft das Flag, kopiert sich die Nachricht,
   setzt das Flag zurück und wertet sie mit `getPayload()` typsicher aus.

```cpp
// typisches Empfangsmuster in jedem Programm:
if (mcDataReceived) {
  xMsg m = lastMcMsg;          // kopieren
  mcDataReceived = false;      // Flag freigeben
  if (m.header.msgCode == catObserved) {
    posPayload pos;
    if (getPayload(m, pos)) {  // prüft: payloadLen == sizeof(posPayload)
      // ... pos.x, pos.y, pos.radius, pos.angle verwenden
    }
  }
}
```

### 3.6 Sende-API (xComProc6_3.h)

```cpp
broadcastMsg(catObserved, posStruct);          // Struct per Multicast an alle
broadcastMsg(catHit);                          // Nachricht ohne Payload
unicastMsg(commandMsg, cmdStruct, lastOctet);  // Struct gezielt an ein Gerät
broadcastRawMsg(xmsg);                         // fertige xMsg unverändert weitersenden
                                               // (Simulator-Playback: Original-sender
                                               //  und -timeStamp bleiben erhalten)
```

Die Template-Varianten ermitteln `payloadLen` automatisch aus `sizeof(T)` —
Längenfehler sind damit konstruktiv ausgeschlossen.

---

## 4. Koordinatensysteme

Das System rechnet in zwei Welten; Ursprung ist immer der **Drehpunkt des
PowerActor-Servos** (= Position des Wasserwerfers):

**Kartesisch (mm):** x nach rechts, y nach vorne (vom PA2 aus auf den Rasen
gesehen). Die Radarsensoren liefern direkt x/y in mm (y wird beim Einlesen
gespiegelt, damit "vorne" positiv ist).

**PA-Polar ("4096er-Welt"):** Der Servo (SMS_STS) arbeitet mit 4096 Schritten
pro Umdrehung. Winkelkonvention:

```
        0°-Richtung = geradeaus (+y)  ↔  PA-Winkel 2048
        4096 Einheiten = 360°  →  1 Einheit ≈ 0.088°
        links  von der Mitte: < 2048      rechts: > 2048
        mechanische Anschläge: leftStopp 760, rightStopp 3400
        nutzbarer Bereich (maxAngle 60°): 2048 ± 682
```

Umrechnungsfunktionen in xComProc6_3.h:

| Funktion | Richtung | Einheit |
|---|---|---|
| `toPol(x,y, phi,r)` | kartesisch → polar | Grad |
| `toKart(x,y, phi,r)` | polar → kartesisch | Grad |
| `toPaPol(x,y, phi,r)` | kartesisch → polar | **PA-Einheiten** (0..4096, 2048=Mitte) |
| `toPaKart(x,y, phi,r)` | polar → kartesisch | PA-Einheiten |

In `posPayload` steht deshalb beides: `angle`/`radius` in der PA-Welt (direkt
servotauglich) und `x`/`y` kartesisch (direkt kartentauglich fürs Display).
Der Sender füllt beide konsistent (Radar: misst x/y, rechnet mit `toPaPol` um;
Lidar: misst Winkel/Distanz, rechnet mit `toPaKart` um).

---

## 5. Die Programme im Einzelnen

Jedes Programm folgt demselben Grundgerüst:

```cpp
#include <xComDef6_3.h>     // Protokoll- und Geräte-Definitionen (aus CommonFiles)
#include "hwDef.h"          // gerätespezifische Pins/Konstanten
... eigene Globals, ID festlegen ...
#include <xComProc6_3.h>    // gemeinsame Prozeduren (brauchen ID/DEBUG/Globals)
// hwProc.ino             // gerätespezifische Funktionen (eigene Datei im Ordner)
```

`setup()` ist überall gleich aufgebaut: Hardware initialisieren →
`setUpWifi(device[ID].IP)` → `initMcUdp()` / `initUnicast()` /
`initText2Udp()` → optional `setUpOTA()` / `setUpTime()`.

### 5.1 Manager6_3_0 — Statuszentrale

- Hört den Multicast mit und loggt jede Nachricht lesbar auf Serial
  (`printSensorData()`: Zeitstempel, Sender, alle Payload-Felder).
- LED-Ring: **grün** blitzt bei jedem empfangenen HB, **blau** (500 ms) bei
  `catObserved` — man sieht dem Gerät von weitem an, ob das System lebt und
  ob gerade etwas detektiert wird.
- Feste IP .180, OTA aktiv.

### 5.2 Radar6_3_0 — Bewegungssensor (HLK-Radar, "Dome"-Familie)

- Eine Quelldatei für drei Hardware-Varianten; Auswahl per define am
  Dateianfang (`DomeDevice` / `MiniDomeDevice` / `CompactDomeDevice`) —
  hwDef.h setzt dann ID, Pins, HB-Periode und Totzone passend.
- Liest den HLK-Radarsensor über Serial2 (256000 Baud). Frameformat:
  Header `AA FF 03 00`, dann 3 Targets à 8 Bytes (x, y, Geschwindigkeit,
  Auflösung als 16-Bit-Werte mit Vorzeichen-Sonderkodierung), 30 Bytes
  gesamt. `readRadar()` synchronisiert byteweise auf den Header,
  `calculateRadarPositions()` dekodiert die 3 Targets.
- Für jedes aktive Target ausserhalb der Totzone (`l > deadZone`, gegen
  Falschauslösung direkt am Sensor) wird ein `catObserved`-posPayload
  gebroadcastet: x/y direkt vom Radar, angle/radius via `toPaPol()`,
  `sensor` = Targetindex (0..2), Geschwindigkeit und res vom Radar.
- Heartbeat: `radarHbPayload` mit der eigenen Totzone — die Empfänger wissen
  damit, in welchem Nahbereich dieser Sensor blind ist.
- LED: grüner Blitz bei HB, blaue LED solange ein gültiges Target da ist.

### 5.3 LD06_6_3_0 — Lidar-Sensor

- LD06-Lidar an Serial2 (230400 Baud), Motor über `lidarPwr` geschaltet.
  Sentence-Format: `54 2C`, 47 Bytes, 12 Messpunkte mit Start-/Endwinkel;
  `calculateLidarData()` interpoliert die Winkel der Zwischenpunkte
  (inkl. Drehrichtungs-Korrektur `360 - winkel`).
- `isDataInRange()` filtert auf das Beobachtungsfenster: Winkel 250°–330°
  (mit korrekter Behandlung des 0°-Übergangs) und Distanz 500–6500 mm —
  d.h. der Lidar überwacht einen festen Sektor des Rasens.
- Treffer werden in die PA-Welt umgerechnet
  (`angle = (450 - winkel) * 2048/180` mappt die Lidar-Gradwelt auf die
  4096er-Servowelt) und als `catObserved` gebroadcastet; x/y via `toPaKart()`.

### 5.4 PA2i6_3_0 — PowerActor (der Wasserwerfer)

Hardware: SMS_STS-Servo (Serial1, 1 MBaud) dreht den Werfer, LP40B-Laser-
Distanzmesser (Serial2) verifiziert die Zieldistanz, Pins für Ventil
(`valve`) und Ziellaser (`laserPwr`).

Zustand: der komplette einstellbare Zustand des Geräts **ist** sein
HB-Payload — die globale Variable `pa2HbPayload hbState` hält readyToFire,
limitsActive und die vier Limits. Beim Boot:

1. `assembleHBmsg()` füllt IP/Periode und lädt die Limits per
   `loadHBFromVault()` aus dem NVS-Flash (Preferences-Namespace "settings") —
   Einstellungen überleben also Stromausfall. `readyToFire` startet bewusst
   immer mit `false` (Sicherheit: nach Reboot nie scharf).
2. `heardBeat()` broadcastet `hbState` alle `periodeForHB` (5 s) — alle
   Geräte sehen so jederzeit den echten Zustand des Werfers.

Kommandoverarbeitung (`commandMsg` per Unicast, siehe Tabelle in 3.2):
Limits werden übernommen **und** sofort via `saveHBToVault()` persistiert;
`cmdArmFire` toggelt nur (bewusst nicht persistiert). Nach jedem Kommando
wird der HB **zweimal sofort** gesendet — als schnelle Bestätigung an
Button/Display (UDP-Paketverlust-Absicherung).

Zielverfolgung (derzeit auskommentiert, Konzept): bei `catObserved` →
`servoGoTo(angle)` (mit Anschlag-Begrenzung 760..3400) → Laserdistanz messen →
stimmen gemeldete und gemessene Distanz auf ±150 mm überein, gilt das Ziel
als bestätigt → feuern + `catHit` broadcasten. Zusätzlich gibt es
`scanWithLidar()`: dreht den Servo über den Bereich und broadcastet
`measurement`-Punkte — damit lässt sich vom Display aus eine "Karte" der
festen Hindernisse aufnehmen.

### 5.5 Udisp6_3_0 — Karten-Display mit Menü

- Eine Quelldatei für 7 Display-Varianten (CYD28/CYD35/Sunton7/Sunton5/
  M5Core2/M5Tab5/Wave7), Auswahl per define; dispDef.h setzt Auflösung,
  Encoder-Typ usw., dispDevLoGFX.h bindet den passenden LovyanGFX-Treiber ein.
- Zeichnet in **Sprite-Layern** (limitLayer, gridLayer, detectLayer,
  hitLayer, …), die per `pushRotateZoom` gedreht/verschoben auf den Schirm
  projiziert werden — der Pivot-Punkt (PA2-Drehpunkt) liegt unten in der
  Bildmitte, der Rasen "fächert" nach oben auf. Mit PSRAM wird über einen
  projectorLayer komponiert, ohne PSRAM direkt aufs Display.
- Empfang:
  - `HB` vom `targetSystem` (PA2i): zeichnet die Limits — zwei Winkelstrahlen
    (left/right) und zwei Distanzbögen (far/near). Das Display zeigt also
    immer den **vom PA2 bestätigten** Zustand.
  - `catObserved`: setzt einen roten Punkt an x/y (skaliert mit xScale/yScale
    aus `calcScale()`).
- Bedienung: Drehencoder + Taste. Tastendruck öffnet das Menü
  (left/right/far/near limit, display shift/angle, erase limits, back);
  in einer Limit-Seite wird der Wert per Encoder eingestellt (live als
  Vorschaulinie/-bogen gezeichnet) und beim Tastendruck als `commandMsg`
  per Unicast an den PA2 geschickt. "erase limits" sendet
  `cmdChangeLimitActivation`.

### 5.6 Button6_3_0 — Scharfschalter

- M5 Atom (LED-Matrix) bzw. Atom S3 (kleines Display, per `#define Atom3S`).
- Tastendruck → `cmdArmFire` per Unicast an den PA2 (Adresse aus der per HB
  gelernten device dB) und Anzeige auf **weiss** ("Zustand unbekannt").
- Der nächste PA2-HB (kommt nach einem Kommando sofort) bestimmt die Farbe:
  **rot** = scharf, **grün** = gesichert. Die Anzeige zeigt also nie den
  vermuteten, sondern immer den vom PA2 bestätigten Zustand — und reagiert
  auf HBs *aller* Geräte vom Typ PowerActor, nicht nur einer festen ID.

### 5.7 Sim6_3_0 — Simulator/Rekorder (M5 Cardputer)

Der Simulator ist das Test-Werkzeug: er konserviert echte Katzen-Ereignisse
und spielt sie beliebig oft wieder ab — damit lassen sich PA2, Display und
Manager testen, ohne auf eine echte Katze zu warten.

- **Aufzeichnen** (Taste 1): jede empfangene `catObserved`-Nachricht wird
  als Header + Payload (variable Recordlänge!) an `/data.bin` auf der
  SD-Karte angehängt.
- **Abspielen** (Taste 2): liest Record für Record (erst Header, dann
  `payloadLen` Bytes) und sendet ihn mit `broadcastRawMsg()` unverändert —
  **Original-Sender und -Zeitstempel bleiben erhalten**, die Empfänger können
  die Wiedergabe nicht von einer echten Detektion unterscheiden (Abstand
  derzeit fix 20 ms pro Record).
- **Löschen** (Taste 3): entfernt die Datei.
- Seit 6_3 nutzt der Simulator dieselben gemeinsamen Header wie alle anderen
  Programme (vorher: eigene udpDef/udpProc in Version 5_4), holt die
  WLAN-Zugangsdaten aus `Credentials.h` und ist als `Sim` in der device dB.

---

## 6. Gemeinsame Infrastruktur (xComProc6_3.h)

| Funktion | Zweck |
|---|---|
| `setUpWifi(lastOctet)` | WLAN verbinden; Oktett 150–195 → statische IP, sonst DHCP; blinkt rot während des Verbindens |
| `setUpTime()` | NTP-Zeit holen (pool.ntp.org / time.nist.gov, Zeitzone Europa mit Sommerzeit) — Basis für die Header-Timestamps |
| `setUpOTA()` | Arduino-OTA mit Hostname aus der device dB → Firmware-Updates über WLAN, kein Kabel nötig |
| `initMcUdp()` / `initUnicast()` / `initText2Udp()` | die drei UDP-Kanäle (siehe 3.4/3.5) |
| `broadcastMsg` / `unicastMsg` / `broadcastRawMsg` | Senden (siehe 3.6) |
| `parseXMsg` / `getPayload` / `getHbPayload` | Validieren und typsicheres Auslesen |
| `printSensorData` / `printCmdData` / `printTimePreamble` | Debug-Ausgabe beliebiger Nachrichten |
| `initPixel` / `setPixel` / `allPixel` | FastLED-Helfer (nur wenn `containLed` definiert; der Trick `maxPix > pixelNum` ⇒ "alle Pixel") |
| `toPol` / `toKart` / `toPaPol` / `toPaKart` | Koordinatenumrechnung (Kap. 4) |
| `writeComment` / `writelnComment` | Debug-Ausgabe-Hooks — jedes Programm definiert selbst, wohin (Serial, Display, Canvas) |

`Credentials.h` (eigene Arduino-Library auf dem Entwicklungsrechner, **nicht
im Repo**) liefert `ssid`/`password` — Zugangsdaten stehen damit in keinem
Sketch.

---

## 7. Versionierungs- und Dateikonzept

```
CatFind/                          (Repo 1 — die Programme)
├── Controller/Manager6_3_0/      ← Master der gemeinsamen Dateien:
│   ├── xComDef6_3.h                 Definitionen (Protokoll, device dB)
│   ├── xComProc6_3.h                Prozeduren  (WiFi/UDP/OTA/Koordinaten)
│   └── Manager6_3_0.ino + hwDef.h + hwProc.ino
├── Button/Button6_3_0/           ← je Programm: Ordnername = Sketchname
├── CF3_LD06_Lidar/LD06_6_3_0/       (Arduino-IDE-Pflicht), hwDef.h für Pins,
├── PowerActor2/PA2i6_3_0/           hwProc.ino für Gerätefunktionen
├── Radar_HKL/Radar6_3_0/
├── Displays/Udisp6_3_0/          ← + dispDef.h/dispDevLoGFX.h/dispProcLoGFX.ino
├── Simulator/Sim6_3_0/
└── Tests/                        ← vom Versionsschema ausgenommen

CommonFiles/                      (Repo 2 — wird als Arduino-Library eingebunden)
├── xComDef6_3.h  → Symlink auf CatFind/Controller/Manager6_3_0/xComDef6_3.h
└── xComProc6_3.h → Symlink auf .../xComProc6_3.h
```

- Die gemeinsamen Header werden **einmal** im Manager-Ordner gepflegt; die
  Symlinks im CommonFiles-Repo (das im Arduino-libraries-Verzeichnis liegt)
  machen sie für alle Sketche als `#include <xComDef6_3.h>` verfügbar.
  Eine Änderung wirkt sofort auf alle Programme — deshalb trägt das Protokoll
  die Version im Dateinamen **und** im `version`-Byte jeder Nachricht.
- Versionswechsel = neue Ordner/Dateien **zusätzlich** (6_2 bleibt vollständig
  lauffähig daneben bestehen). Da 6_3-Geräte 6_2-Pakete verwerfen (Versions-
  Byte) und 6_2-Geräte 6_3-Pakete (falsche Paketgrösse), können beide
  Generationen gleichzeitig im selben WLAN laufen, ohne sich zu stören —
  miteinander reden können sie aber nicht: bei der Migration muss eine
  zusammenarbeitende Gerätegruppe gemeinsam umgestellt werden.
- Bekannte offene Punkte und Verbesserungsvorschläge stehen in
  `REVIEW_6_3.md` (je Programmordner) und
  `Controller/Manager6_3_0/REVIEW_xCom6_3.md` (gemeinsame Dateien).

---

## 8. Typische Abläufe Schritt für Schritt

### Boot eines Geräts
1. Hardware-Init (Pins, Serielle, Display/LEDs)
2. `setUpWifi()` — rot blinkend bis verbunden, statische IP falls konfiguriert
3. UDP-Kanäle initialisieren (ab jetzt laufen die Empfangs-Callbacks)
4. optional OTA + NTP-Zeit
5. gerätespezifisch (PA2: Vault laden, Servo auf Mitte; LD06: Motor an; …)

### "Eine Katze betritt den Rasen"
1. Radar erkennt Target → `catObserved`-Broadcast (alle ~100 ms, solange
   das Target aktiv ist); parallel ggf. der Lidar für seinen Sektor.
2. Manager blinkt blau, Display setzt rote Punkte auf die Karte (die
   Punktspur zeigt den Laufweg), Simulator zeichnet auf (falls Record an).
3. PA2 (Ziel-Logik, in Arbeit): Servo auf `angle` drehen, Laserdistanz
   gegen `radius` prüfen, bei Bestätigung und `readyToFire && limitsActive`-
   Freigabe: Ventil auf → `catHit`-Broadcast → Display könnte den Treffer
   auf dem hitLayer markieren.

### "Schussfeld einrichten" (vom Sofa aus)
1. Am Display Menü öffnen → "left limit" → Encoder drehen, der Strahl
   wandert live über die Karte → Taste drücken.
2. Display sendet `cmdSetLeftLimit` per Unicast an den PA2.
3. PA2 übernimmt den Wert, speichert ihn ins Flash und broadcastet sofort
   2x seinen HB mit den neuen Limits.
4. Das Display (und jedes andere) zeichnet die Limits aus diesem HB —
   was man sieht, ist garantiert das, was der PA2 gespeichert hat.

### "Scharfschalten"
1. Knopf am Button drücken → Anzeige weiss → `cmdArmFire` an PA2.
2. PA2 toggelt readyToFire, antwortet mit Sofort-HB.
3. Button zeigt rot (scharf) oder grün (gesichert). Nach einem Reboot des
   PA2 ist das System immer gesichert.

### "Testlauf ohne Katze"
1. Echte Szene einmal mit dem Simulator aufzeichnen (Taste 1, Katze laufen
   lassen, Taste 1).
2. Beliebig oft Taste 2: die Originalnachrichten (inkl. Original-Absender
   Dome/LD06) gehen wieder über den Multicast — PA2, Display und Manager
   reagieren wie beim Live-Ereignis.
