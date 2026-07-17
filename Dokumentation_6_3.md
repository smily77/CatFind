# CatFind 6.3 — Systembeschreibung

Stand: 2026-07-05 · gilt für die 6_3-Programmversionen (Protokoll 0x63)

CatFind ist ein verteiltes System aus ESP32-Geräten, das eine Katze auf dem
Rasen erkennt, ihre Position bestimmt und sie mit einem gezielten Wasserstrahl
vertreibt. Die Geräte kommunizieren untereinander über WLAN mit einem
selbstdefinierten UDP-Protokoll (fixer Header + variabler Payload).

---

## 1. Überblick: Wer macht was

```
  ┌─ Sensoren ──────────────┐        ┌─ Aktoren (Wasserwerfer) ─┐
  │ Radar Dome/Mini/Compact │        │ PA2i  (SMS-Servo)        │
  │ Lidar LD06              │        │ PA1_1 (Schrittmotor)     │
  │ Lidar C1  (welt-fähig)  │        └───────────▲──────────────┘
  │ Simulator (Replay)      │                    │ commandMsg (Unicast)
  └───────────┬─────────────┘                    │
              │ catObserved (+Welt-Koord.)        │
  ════════════▼═══════════════════════════════════╪═══════════════  UDP-Bus (WLAN 192.168.0.x)
    Multicast 239.0.0.57:8266  ·  Unicast :23456  ·  Text :8300
  ════════════▲═══════════════════▲═══════════════╪═══════════════
              │ HB / Status        │ settingsReport │ HB
  ┌───────────┴──┐  ┌──────────────┴─────┐  ┌───────┴────────────────┐
  │ Display Udisp│  │ Bedien-Display     │  │ Button / Touch-Remote  │
  │ (Karte/Menü) │  │  Einstellungen +   │  │ (scharf / kalibrieren) │
  │ LaserMarker  │  │  Steuerung         │  └────────────────────────┘
  └──────────────┘  └────────────────────┘
              │
  ┌─ Erkennung (Ebene 2) ────────────────────────────────────────┐
  │ Cat Identifier (18): Echtzeit-Modell auf catObserved         │──catDetected (2x)──► Bus
  │ Cat Cam (19): Vision-KI (Katze im Bild) + Fotos an den VPS   │──catObserved / Foto─►
  └──────────────────────────────────────────────────────────────┘
              │
  ┌───────────┴─────────────┐            ┌──── VPS (Docker, außerhalb LAN) ────┐
  │ Manager                 │  HTTP      │ Localizer :8080/localize            │
  │  Statuslampe, Log,      │═══/ingest═►│   (360-Bin-Scan → Welt-Pose)        │
  │  Karten-Server,         │◄═/commands═│ Dashboard :80/                      │
  │  VPS-Gateway            │            │   Treffer-/Statusvisualisierung,    │
  └─────────────────────────┘            │   kontinuierl. Track-Analysierer,   │
                                         │   Bilder-Tab, Modell-Parameter      │
                                         └──────────────▲──────────────────────┘
              LidarC1  (Boot: /localize) ───────────────┤ HTTP
              CatIdent (Parameter: /aparams.csv) ───────┤
              CatCam   (Fotos: POST /photo) ────────────┘
```

Der **Manager** (Gateway), der **LidarC1** (Boot-Lokalisierung), der
**Cat Identifier** (Modell-Parameter) und die **Cat Cam** (Foto-Upload)
verlassen per HTTP das LAN Richtung **VPS**; alle CatFind-Geräte untereinander
reden ausschließlich über den UDP-Bus.

**Die Kette im Normalbetrieb:**

1. Ein **Sensor** (Radar, LD06 oder welt-fähiger LidarC1) erkennt ein Objekt auf
   dem Rasen und broadcastet die Position als `catObserved` — relativ, und falls
   der Sensor eine gültige Welt-Pose hat (`validWorldPose`) zusätzlich in
   **Welt-Koordinaten** (`worldValid=1`, Kap. 4.1).
2. Ein **PowerActor** (PA2i mit SMS-Servo oder PA1_1 mit Schrittmotor) empfängt
   die Position, richtet seinen Werfer auf das Ziel, verifiziert die Distanz mit
   dem Laser-Distanzmesser und löst — wenn scharfgeschaltet und das Ziel
   innerhalb der Limits liegt — das Wasserventil aus. Eine Meldung wird verwendet,
   wenn sie Welt-Koordinaten trägt **und** der Aktor selbst welt-posiert ist, oder
   wenn sie aus seiner eigenen Koordinatengruppe stammt (Kap. 4.1). (Die
   Schiesslogik ist in 6_3 noch auskommentiert / in Arbeit, siehe REVIEW_6_3.md
   im PA2i-Ordner.)
3. **Display**, **Manager** und **Simulator** hören denselben Broadcast mit:
   das Display zeichnet die Position auf eine Karte, der Manager blinkt blau,
   der Simulator kann die Szene auf SD aufzeichnen.
4. Der **Button** schaltet einen PowerActor per Unicast-Kommando scharf/unscharf;
   der aktuelle Zustand kommt als Heartbeat zurück und wird über die LED-Farbe
   angezeigt. Über das **Bedien-Display** bzw. den **Steuerungs-Tab des
   VPS-Dashboards** lassen sich Anzeige-/Automatik-Settings und Aktionen jedes
   Geräts schalten (Kap. 5.12).
5. **Welt-Pose:** der LidarC1 lokalisiert sich beim Boot selbst über den **VPS**
   (Scan → Pose), das Radar bekommt seine Pose per **Co-Observation** mit einem
   welt-posierten Sensor (Kap. 4.1, 5.2). So arbeiten mehrere Aktoren in einem
   gemeinsamen Welt-System zusammen.
6. **Erkennungs-Ebene 2:** der **Cat Identifier** (Kap. 5.13) hört alle
   welt-validen `catObserved` mit und lässt das Katzen-Erkennungsmodell
   **in Echtzeit** laufen (Streaming-Port des VPS-Referenzmodells
   `catmodel.py`). Bestätigt es einen Track als Katze, broadcastet er
   `catDetected` (doppelt, UDP-Verlustschutz) — der Manager blinkt **rot**,
   die **Cat Cam** (Kap. 5.14) macht ein Foto und lädt es zum VPS hoch
   (Tab „Bilder", mit zugeordneter Track-Nummer). Die Cat Cam erkennt mit
   ihrem eigenen Vision-KI-Modul zusätzlich Katzen **im Bild** und liefert
   dafür `catObserved` + Foto.
7. Alle Geräte senden periodisch einen **Heartbeat (HB)** — daraus lernen alle
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
| 16 | PA1_1 | PowerActor | 183 | älterer PowerActor mit Schrittmotor (PCF8574/A4988), Drehturm (siehe PowerActor1_1/PA1_1_6_3_0) |
| 17 | LidarC1 | Lidar | DHCP | ESP32-S3 + RPLidar C1, welt-fähig via VPS-Lokalisierung (siehe CF_LidarC1/C1Lidar6_3_0) |
| 18 | CatIdent | Detector | 184 | Seeed XIAO ESP32-S3 (IPEX-Antenne!), Echtzeit-Erkennungsmodell → `catDetected` (siehe CatIdentifier/CatId6_3_0, Kap. 5.13) |
| 19 | CatCam | Kamera | 185 | Seeed XIAO Vision AI Camera (XIAO ESP32-C3 + Grove Vision AI V2 + OV5647), Fotos → VPS (siehe CatCam/CatCam6_3_0, Kap. 5.14) |

- Die Anzahl der Einträge steht als **`deviceCount`** (derzeit 20) zur
  Verfügung — Schleifen über die Tabelle verwenden dieses define statt einer
  harten Zahl.
- Das Feld `IP` enthält das **letzte Oktett** der Adresse (Netz ist fest
  192.168.0.x). Bei Geräten mit Eintrag 150–195 konfiguriert `setUpWifi()`
  eine statische IP, alle anderen nutzen DHCP. **Achtung bei statischer IP:**
  `WiFi.config()` braucht das **4. Argument (DNS)** — ohne DNS hängt alles,
  was Hostnamen auflöst (z.B. NTP), endlos. CatIdent/CatCam machen es vor
  (eigene WiFi-Anmeldung mit DNS = Gateway und NTP mit Zeitbudget).
- Empfängt ein Gerät einen HB, trägt es das im HB gemeldete Oktett automatisch
  in seine lokale Kopie der device dB ein (`device[sender].IP`). So kennt z.B.
  der Button die Adresse des PA2i, ohne dass sie irgendwo konfiguriert wäre.
- `Name` wird als OTA-Hostname verwendet (`setUpOTA()`), `type` dient zur
  Gruppierung (z.B. reagiert der Button auf HBs von **allen** PowerActor-Geräten,
  nicht auf eine bestimmte ID).
- `group` ordnet jedes Gerät einer **relativen Koordinatengruppe** zu: ein Aktor
  und die fest mit ihm verbundenen Sensoren teilen dasselbe lokale
  Koordinatensystem. Definiert sind `groupNone (0)`, `groupPA2 (1)` =
  {PA2i, CompactDome, LD06}, `groupPA1_1 (2)` = {PA1_1, MiniDome} und
  `testGroup (3)` (derzeit `Dome`, zum Testen); alle übrigen Geräte sind
  `groupNone`. (Siehe Kap. 4.1.)

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
posPayload = 34, cmdPayload = 5, worldPosePayload = 13, settingsPayload = 6 Bytes.

> **Hinweis (globale Koordinaten, ab dieser 6_3-Erweiterung):** `posPayload`
> wurde von 25 auf 34 Bytes erweitert (Welt-Koordinatenfelder, siehe unten).
> Da sich die Payload-Größe ändert, müssen **alle** 6_3-Geräte gemeinsam neu
> geflasht werden (Empfänger prüfen `payloadLen == sizeof(posPayload)`). Alte
> Simulator-Aufnahmen mit 25-Byte-Records werden beim Replay verworfen.

### 3.2 Nachrichtenarten und ihre Payloads

| msgCode | Wert | Payload | Richtung (typisch) | Bedeutung |
|---|---:|---|---|---|
| `HB` | 1 | `hbPayload` / `pa2HbPayload` / `radarHbPayload` | Broadcast | Lebenszeichen + Zustandsdaten |
| `catObserved` | 2 | `posPayload` | Broadcast | Sensor hat ein Ziel erkannt |
| `measurement` | 3 | `posPayload` | Broadcast | Messpunkt (z.B. Lidar-Scan des PA2) |
| `catHit` | 4 | `posPayload` | Broadcast | PA2 hat auf diese Position geschossen |
| `commandMsg` | 5 | `cmdPayload` | Unicast | Steuerkommando an ein bestimmtes Gerät |
| `poseRequest` | 6 | — (kein Payload) | Unicast/Broadcast | "Melde deine Welt-Pose" (siehe Kap. 4.1) |
| `poseReport` | 7 | `worldPosePayload` | Unicast/Broadcast | Antwort/Annonce der eigenen Welt-Pose |
| `mapRequest` | 8 | `mapReqPayload` | Unicast | "Sende Karte vom Typ X" (an den Manager) |
| `mapInfo` | 9 | `mapInfoPayload` | Unicast | Karten-Metadaten (Antwort auf `mapRequest`) |
| `mapChunk` | 10 | `mapChunkMeta` + Daten | Unicast | ein Datenstück der Karte (variabel, siehe Kap. 4.2) |
| `settingsRequest` | 11 | — (kein Payload) | Broadcast/Unicast | "melde deine Einstellungen" (siehe Kap. 5.12) |
| `settingsReport` | 12 | `settingsPayload` | Broadcast | Welche Anzeige-/Automatik-Settings/Aktionen ein Gerät hat und wie sie stehen |
| `catDetected` | 13 | `catDetectedPayload` | Broadcast | Erkennungsmodell hat einen Track als **Katze bestätigt** (Ebene 2, Cat Identifier, Kap. 5.13) |

**posPayload** (34 Bytes) — Positionsmeldung. `x/y/radius/angle` sind **relativ**
(sensor-/aktorlokal); `worldX/worldY` sind **Welt-Koordinaten** und nur gültig,
wenn `worldValid==1`:

| Feld | Typ | Bedeutung |
|---|---|---|
| `x`, `y` | int32 | **relative** kartesische Position in mm (siehe Koordinatensysteme, Kap. 4) |
| `radius` | float | Distanz vom Drehpunkt in mm (relativ) |
| `angle` | float | Winkel in PA-Einheiten (0..4096, siehe Kap. 4) |
| `targetSpeed` | int32 | Zielgeschwindigkeit (nur Radar, cm/s) |
| `res` | int32 | Distanz-Auflösungswert des Radars |
| `worldX`, `worldY` | int32 | **Welt-Koordinaten** in mm (immer kartesisch); 0 wenn `worldValid==0` |
| `worldValid` | uint8 | 1 = `worldX/worldY` gültig (Sender hatte beim Senden eine `validWorldPose`) |
| `sensor` | uint8 | Target-/Sensorindex (Radar liefert bis zu 3 Targets: 0..2) |

**catDetectedPayload** (14 Bytes) — vom Cat Identifier bei bestätigter Katze
(msgCode 13). Wird ausgelöst, **sobald** der Modell-Score die Schwelle erreicht
(während die Katze noch im Erfassungsbereich ist), und **doppelt** gesendet
(UDP hat kein Resend; Empfänger dedupen über identische Payload ≤1,5 s):

| Feld | Typ | Bedeutung |
|---|---|---|
| `worldX`, `worldY` | int32 | Welt-Position der Katze bei Auslösung (mm) |
| `score` | uint8 | Modell-Score 0..100 (≥ Bestätigungsschwelle) |
| `flags` | uint8 | `catDetFlagStationary(0x01)` = sitzt gerade (koten?), `catDetFlagFusion(0x02)` = mehrere Sensoren |
| `trackMs` | uint32 | bisherige Track-Dauer bei Auslösung (ms) |
| `netMm` | int32 | bisherige Netto-Verschiebung des Tracks (mm) |

**worldPosePayload** (13 Bytes) — Welt-Pose eines Geräts (Antwort auf `poseRequest`):

| Feld | Typ | Bedeutung |
|---|---|---|
| `validWorldPose` | uint8 | 1 = Pose gültig |
| `worldX`, `worldY` | int32 | Welt-Position des Geräte-Ursprungs in mm |
| `heading` | float | Ausrichtung in PA-Einheiten (0..4096, 0 = welt-ausgerichtet) |

**settingsPayload** (6 Bytes) — Einstellungen & Aktionen eines Geräts (Antwort auf
`settingsRequest`). Bit-Indizes: `stgHbLed(0)`, `stgCatLed(1)`, `stgAutoCopyPose(2)`,
`stgAutoCalib(3)`, `stgLidarMotor(4)`, `stgCatDetLed(5)` (Manager: rotes Blinken beim
catDetected-Empfang), `stgCamAi(6)` (Cat Cam: Vision-KI-Erkennung an/aus); Aktionen
`actCopyPose(0)`, `actCalibrate(1)`, `actClearPose(2)`, `actReloadParams(3)` (CatIdent:
Modell-Parameter neu vom VPS), `actPhoto(4)` (CatCam: Foto jetzt):

| Feld | Typ | Bedeutung |
|---|---|---|
| `supported` | uint16 | Bitmaske: welche Anzeige-/Automatik-Settings das Gerät hat |
| `values` | uint16 | Bitmaske: aktueller on/off-Zustand je Setting |
| `actions` | uint16 | Bitmaske: welche Aktionen ausgelöst werden können |

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

Weitere Kommando-Codes sind **gerätespezifisch** und nicht an den PA2i gerichtet:
`cmdMainLaser`/`cmdSubLaser`/`cmdAux`/`cmdPixelColor`/`cmdMarkerState` (11–15, LaserMarker,
siehe `API_LaserMarker6_3.md`), `cmdWirelessPower`/`cmdExtPower` (16–17, PA1_1-Relais) und
`cmdCalibrate` (**18**) — an einen nicht selbst-lokalisierenden Sensor (z.B. das Radar)
gerichtet; `info` = Kalibrier-Fensterdauer in ms (0 = Geräte-Default). Startet dort den
**Co-Observation-Kalibriermodus** (Welt-Pose über einen mitbeobachtenden, welt-posierten
Sensor; siehe GesamtKonzeptCatFinder.md). `cmdClearPose` (**19**) — löscht die gespeicherte
Welt-Pose (NVS) und setzt `validWorldPose=false`; nötig, wenn das Gerät **bewegt** wurde
(grobe Pose-Fehler erkennt der Health-Check nicht selbst, Kap. 5.2). Beide werden z.B. vom
Touch-Remote in Kap. 5.11 ausgelöst. `cmdSetSetting` (**20**) — setzt ein Anzeige-/Automatik-
Setting (`info = (settingIndex<<1) | wert`), speichert es (falls persistiert) und annonciert
danach ein `settingsReport`. `cmdCopyPose` (**21**) — Aktion `actCopyPose`: übernimmt die
Welt-Pose eines Mitglieds der eigenen relativen Koordinatengruppe (siehe Kap. 5.12).
`cmdReloadParams` (**22**) — Aktion `actReloadParams`: der Cat Identifier lädt seine
Modell-Parameter neu vom VPS (`/aparams.csv`, Kap. 5.13) — Modell-Iteration ohne Neuflash.
`cmdTakePhoto` (**23**) — Aktion `actPhoto`: die Cat Cam macht sofort ein Foto und lädt es
zum VPS hoch (Kap. 5.14). Alle werden vom Touch-Bediendisplay (Kap. 5.12) und vom
VPS-Webinterface ausgelöst.

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
PowerActors** (= Position des Wasserwerfers):

**Kartesisch (mm):** x nach rechts, y nach vorne (vom PA2 aus auf den Rasen
gesehen). Die Radarsensoren liefern direkt x/y in mm (y wird beim Einlesen
gespiegelt, damit "vorne" positiv ist).

**PA-Polar ("4096er-Welt"):** Diese Winkelkonvention ist **geräteübergreifend**
(alle Aktoren und Sensoren teilen sie):

```
        0°-Richtung = geradeaus (+y)  ↔  PA-Winkel 2048
        4096 Einheiten = 360°  →  1 Einheit ≈ 0.088°
        links  von der Mitte: < 2048      rechts: > 2048
```

**Gerätespezifisch** sind hingegen die mechanischen Anschläge und der nutzbare
Bereich – diese gelten **nicht** allgemein, sondern hängen vom jeweiligen Aktor
und seiner Mechanik ab. Beim **PA2i** (SMS_STS-Servo, 4096 Schritte/Umdrehung)
gilt:

```
        mechanische Anschläge: leftStopp 760, rightStopp 3400
        nutzbarer Bereich (maxAngle 60°): 2048 ± 682
```

Der **PA1_1** (Schrittmotor-Turm, siehe 5.8) verwendet dieselbe 4096er-Welt,
aber einen anderen nutzbaren Bereich (`maxAngle 180°` → 2048 ± 2048) und
Reed-Referenzfahrt statt fester Servo-Anschläge. Wer Funktionen schreibt, die
auf Anschläge/Limits zugreifen, muss diese also **pro Aktor** behandeln, nicht
als Konstanten des Gesamtsystems.

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

### 4.1 Welt-Koordinaten (globales System) und relative Koordinatengruppen

Die obigen relativen Koordinaten gelten nur **innerhalb einer relativen
Koordinatengruppe** (Aktor + fest verbundene Sensoren, siehe `group` in Kap. 2).
Zusätzlich gibt es ein **globales Welt-System**, damit mehrere Aktoren
zusammenarbeiten können:

- **Welt-Koordinaten sind immer kartesisch** (`worldX`/`worldY`, **mm**). Es gibt
  bewusst keine globalen Polarkoordinaten.
- Jedes Gerät besitzt eine **Welt-Pose** (`worldPose myPose` in `xComDef6_3.h`):
  Ursprung `worldX/worldY` (mm) + `heading` (PA-Einheiten 0..4096, 0 =
  welt-ausgerichtet) + `mirror` (Drehsinn ±1, von `localToWorld`/`worldToLocal`
  berücksichtigt) + Flag `validWorldPose`. Letzteres ist nach Boot grundsätzlich
  **`false`**, bis eine Positionsbestimmung/Quickcheck es bestätigt: `loadPose()`
  lädt nur die Koordinaten, lässt `validWorldPose` aber auf `false`. **Ausnahme:**
  ein Sensor, der sich **nicht selbst lokalisieren** kann (Radar), aber im Normalfall
  **nicht bewegt** wird, vertraut beim Boot seiner gespeicherten NVS-Pose direkt
  (setzt `validWorldPose=true`), statt jedes Mal eine Kalibrierung zu verlangen.
  Sein „Quickcheck" ist dann der laufende **Health-Check** (siehe Kap. 5.2 und
  GesamtKonzeptCatFinder.md): ein mitbeobachtender welt-posierter Sensor deckt eine
  inzwischen falsche Pose auf und verwirft sie. Der Lidar dagegen verifiziert bei jedem
  Boot per VPS und braucht diese Vertrauensregel nicht.
- Ein Sensor mit `validWorldPose` sendet in `posPayload` zusätzlich
  `worldX/worldY` und setzt `worldValid=1`. Ohne valide Pose bleibt
  `worldValid=0` (und `worldX/worldY=0`).
- **Adressierung der Meldungen:** Ein Aktor verwendet eine Beobachtung, wenn
  entweder (a) Welt-Koordinaten mitgeliefert wurden **und** er selbst eine
  `validWorldPose` hat, oder (b) die Meldung aus **seiner eigenen relativen
  Koordinatengruppe** stammt. Andernfalls ignoriert er sie. (Die konkrete
  Empfangs-/Feuerlogik ist Gegenstand späterer Aufgaben.)
- **Pose-Abfrage:** `poseRequest` (msgCode 6, ohne Payload) fragt ein Gerät nach
  seiner Welt-Pose; es antwortet mit `poseReport` (msgCode 7, `worldPosePayload`).
  So kann z.B. ein auf Welt-Koordinaten eingestelltes Display, das nur eine
  relative Meldung erhält, ein Gruppenmitglied mit valider Pose anfragen und
  selbst umrechnen.

Umrechnung Welt ↔ lokal (in `xComProc6_3.h`, reine 2D-Translation + Rotation um
`heading`):

| Funktion | Richtung |
|---|---|
| `localToWorld(xl,yl, pose, xw,yw)` | lokal → Welt (mm) |
| `worldToLocal(xw,yw, pose, xl,yl)` | Welt → lokal (mm) |

Persistenz der eigenen Pose (NVS, Preferences-Namespace `"pose"`):

| Funktion | Zweck |
|---|---|
| `savePose(pose)` | Ist-Pose ins NVS schreiben |
| `loadPose(pose)` | Ist-Pose lesen (`validWorldPose` bleibt nach Boot `false`) |

### 4.2 Karten-Verteilung (Manager → Geräte)

Karten liegen zentral im **LittleFS des Managers** und werden über das
UDP-xCom-Protokoll an anfragende Geräte verteilt. Ablauf (alles Unicast):

```
Sensor ──mapRequest(mapType)──►  Manager
Sensor ◄──mapInfo(version,crc,len,chunkSize,chunkCount)── Manager
Sensor ◄──mapChunk(idx,data)──   Manager   (chunkCount Stück, in Reihenfolge)
```

- **mapType-Enum:** `mapNoShot (1)` = Schusszonen-Karte (`/noshot.csv`, lädt der
  Aktor/Lidar); `mapRasen (2)` = **RasenKarte**/Rasen-Umriss (`/rasen.csv`,
  laden die Radare — sie melden mit gültiger Welt-Pose nur Ziele innerhalb,
  Kap. 5.2). Beide Polygon-CSV in Welt-mm.
- **`mapReqPayload`** (1 B): `mapType`.
- **`mapInfoPayload`** (15 B): `mapType`, `version` (aus dem CSV-Header `vN`),
  `fileCrc` (CRC32 über die ganze Datei), `totalLen`, `chunkSize` (`mapChunkBytes`
  = 48), `chunkCount`.
- **`mapChunk`**: `mapChunkMeta` (`mapType`, `version`, `chunkIndex`, `dataLen`)
  **+** `dataLen` Datenbytes — variable Payloadlänge `sizeof(mapChunkMeta)+dataLen`.

Der Empfänger sammelt die Chunks der Reihe nach in einem **statischen
RAM-Puffer** (`MAP_RX_BUF_BYTES`, 8 KB; größere Karten streamen in `<pfad>.tmp`),
prüft am Ende `fileCrc`/`totalLen` und schreibt erst dann die Zieldatei.
**Warum RAM statt Datei:** LittleFS-Schreiben mitten im Transfer blockiert zig ms
(Flash-Erase); mit dem Single-Paketpuffer des Empfängers und ohne Resend ging
dabei reproduzierbar ein Chunk verloren und der ganze Transfer scheiterte. Der
Manager lässt zusätzlich 10 ms zwischen den Chunks. Stimmt `version`/`fileCrc`
bereits mit der lokalen Kopie überein, wird der Download übersprungen. Schlägt
ein Transfer trotzdem fehl (kein Resend — z. B. direkt nach dem Boot), wiederholt
das Gerät periodisch; der Fortschritt ist als Text-Multicast im VPS-Debug
sichtbar („Karte 2: lade vom Manager … / Download OK / unvollständig").

Gemeinsame Prozeduren in `xComProc6_3.h`:

| Funktion | Seite | Zweck |
|---|---|---|
| `crc32Bytes(d,n)` | beide | CRC32 (zlib-kompatibel) |
| `mapFileInfo(path, v, crc, len)` | Manager | Version/CRC/Länge einer LittleFS-Karte |
| `serveMap(mapType, path, reqOctet)` | Manager | `mapInfo` + alle `mapChunk`s senden |
| `requestMap(mapType, mgrOctet)` | Sensor | `mapRequest` senden |
| `mapBeginRx(state, info, path)` | Sensor | Empfang starten (nach `mapInfo`) |
| `mapFeedChunk(state, msg)` | Sensor | Chunk verarbeiten (0=weiter,1=fertig,-1=Fehler) |
| `acquireMap(mapType, path, mgrOctet, waitMs)` | Sensor | Komplettablauf: Cache laden, Version prüfen, ggf. Download |
| `acquireNoShot(path, mgrOctet, waitMs)` | Sensor | Kurzform `acquireMap(mapNoShot, …)` |
| `loadNoShot(path)` / `insideNoShot(x,y)` | Sensor | Polygon-Karte in RAM laden / Welt-Punkt-Test (generisch für No-Shot UND Rasen) |
| `mapAnnounceOutdated(msg, mapType, path)` | Sensor | unaufgefordertes `mapInfo` (Announce) auswerten: weicht es von der lokalen Version/CRC ab? |

#### Karten-Quelle: VPS-Editor statt Firmware-Defaults

Karten liegen **nicht** mehr im Firmware-Quellcode (siehe `MapConcept.md` im
Repo-Root für das vollständige Konzept). Einziger regulärer Weg, eine Karte zu
ändern, ist der VPS-Editor im Analyse-Tab; er läuft über den bestehenden
`/commands`-Kanal (Pull statt Push, der VPS kann den Manager nicht direkt
erreichen):

```
Editor speichert  ──►  VPS: Karte als "pending" ablegen
                        │
/commands-Poll (Manager, alle paar Sekunden)
                        │  Kommando cmdMapFetch (info=mapType)
                        ▼
Manager: HTTP GET /maps/<typ>?pending=1 vom VPS
                        │
Annahme-Code (gwAcceptMap, gatewayProc.ino):
  validieren (loadNoShot-Ringparser) → Version = alte+1, CRC neu,
  Header schreiben → atomar ins LittleFS (Temp + Rename)
                        │
                        ├─► broadcastMsg(mapInfo, …)   Announce an alle Geräte
                        └─► HTTP POST /mapsync (gwPushMap)  Bestätigung an den VPS
                                                             → VPS-Spiegel + Git-Commit nach
                                                               Controller/Manager6_3_0/data/<typ>.csv
                                                               (Source of Truth) + Map/backup/<typ>.csv
                                                               + Map/backup/history/<typ>_v<N>.csv
```

- **Annahme-Code** (`gwAcceptMap` in `gatewayProc.ino`): Rohdaten (Ring-Punkte,
  ohne gültigen Versions-Header — den vergibt ausschließlich der Manager)
  werden über denselben Ring-Parser wie beim Laden validiert (`loadNoShot`
  auf eine Temp-Datei). Bei Fehler: ablehnen, alte Karte bleibt unverändert
  aktiv — es gibt keinen Zustand „kaputte Karte“.
- **Announce statt Warten:** nach jeder angenommenen Karte broadcastet der
  Manager unaufgefordert `mapInfo` (Multicast). Geräte vergleichen mit ihrer
  lokalen Version/CRC (`mapAnnounceOutdated`) und rufen bei Abweichung ihr
  `acquireMap()` zeitnah erneut auf, statt bis zum nächsten periodischen
  Versuch zu warten.
- **Fangnetz:** zusätzlich prüfen Radar (`Radar6_3_0.ino`, `serviceRasenMap`)
  und LidarC1 (`hwProc.ino`, `serviceNoShotMap`) auch mit bereits geladener
  Karte periodisch nach (`MAP_RECHECK_MS`, stündlich) — ein UDP-Announce kann
  verlorengehen, es gibt kein Resend.
- **Kartenstand-Spiegel:** der Manager meldet Version/CRC beider Karten
  leichtgewichtig in jedem `/ingest`-Push mit (`"maps":[…]`, aus einem Cache,
  kein LittleFS-Zugriff im Hot Path). Weicht der VPS-Spiegel ab (z. B. weil
  jemand die Karte am Manager vorbei per Notweg geändert hat), stößt der VPS
  per `cmdMapPush` einen Re-Sync an — der Manager postet dann die volle CSV an
  `/mapsync`. Details, Endpunkte und Sicherheitsüberlegungen: `MapConcept.md`.
- **Notweg ohne VPS:** `Controller/Manager6_3_0/data/noshot.csv` bzw.
  `.../data/rasen.csv` im Repo von Hand anpassen (das ist die Source of
  Truth — genau der Ordner, aus dem das Arduino/ESP32-Tooling das
  LittleFS-Image baut) und per USB oder OTA flashen. Bewusst kein eigener
  Netzwerk-Upload-Endpunkt am Manager (kleinere Angriffsfläche für die
  Schuss-Freigabezone im lokalen Netz). Nach dem Reboot annonciert der
  Manager seinen Kartenstand sofort (`gwAnnounceMaps()` in `ensureMaps()`),
  Sensoren laden also zeitnah nach, nicht erst nach dem stündlichen
  Fangnetz. Ausführliche Schritt-für-Schritt-Anleitung (USB **und** OTA,
  inkl. konkreter Befehle): `Controller/Manager6_3_0/KartenUpload.md`.

**Welches Programm nutzt welche Karte** (Details/Troubleshooting in
`KartenUpload.md`):

| Programm | Karte | Wofür |
|---|---|---|
| Manager6_3_0 | beide (Master) | hält beide im LittleFS, verteilt per `mapRequest`/`mapInfo`/`mapChunk` |
| C1Lidar6_3_0 (LidarC1) | NoShot | Fire-Gating (`insideNoShot` vor jedem `catObserved`) |
| Radar6_3_0 (HLK-Radar) | Rasen | Relevanzfilter (Ziele außerhalb werden nicht gemeldet) |
| LD06_6_3_0, alle übrigen Programme | keine | keine Polygon-Karte im Einsatz |

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
  `catObserved`, **rot** (500 ms) bei `catDetected` (bestätigte Katze vom
  Cat Identifier) — man sieht dem Gerät von weitem an, ob das System lebt,
  ob gerade etwas detektiert wird und ob das Modell eine Katze bestätigt hat.
  Die drei Empfangs-Anzeigen sind je einzeln schaltbar (`stgHbLed`,
  `stgCatLed`, `stgCatDetLed`). `catDetected` wird doppelt gesendet — der
  Manager dedupt über identische Payload, damit nur einmal geblinkt und einmal
  ins VPS-Debug geloggt wird.
- Feste IP .180, OTA aktiv.
- **Karten-Server:** hält No-Shot- und RasenKarte im LittleFS (`/noshot.csv`,
  `/rasen.csv`) und beantwortet `mapRequest` per `serveMap()` (siehe Kap. 4.2).
  Keine eingebetteten Default-Karten mehr im Sketch — einzige Quelle ist der
  VPS-Editor (`gwAcceptMap`/`gwFetchMap`/`gwPushMap`, `gatewayProc.ino`; Notweg
  ohne VPS: Repo-CSV anpassen + neu flashen). Ohne Karte im LittleFS antwortet
  der Manager auf `mapRequest` schlicht nicht.
- **VPS-Gateway (Treffervisualisierung):** der Manager lauscht ohnehin auf dem
  Bus und leitet `catObserved`, `HB` und die Text-Debug-Meldungen gebündelt per
  HTTP-POST an das VPS-Dashboard weiter (`ipVPS:80/ingest`, ~alle 1,5 s,
  `gatewayProc.ino`). Pro Event geht `sender`, `sensor`, Welt- und
  Relativkoordinaten sowie die Koordinatengruppe (`device[sender].group`) mit —
  und seit der Analyse-Erweiterung zusätzlich die **Empfangszeit `ms`**
  (`millis()`, für ms-genaue Event-Zeiten auf dem VPS trotz 1,5-s-Bündelung),
  der **`targetSpeed`** des Radars sowie pro Push `now_ms` und ein
  **Drop-Zähler** (`dropped`, vom Burst-Schutz verworfene Events — bei
  Lidar-Sonnen-Bursts ist die Menge das Erkennungsmerkmal).
  Fällt der Manager aus, läuft das lokale Netz weiter — nur die Visualisierung
  pausiert. (Burst-Verlust durch den Single-Buffer-Empfang ist für die
  akkumulierende Karte unkritisch.)
- **Steuer-Gateway (Rückkanal):** der Manager reicht zusätzlich die `settingsReport`
  der Geräte an den VPS weiter (`/ingest`, Feld `settings`) und **pollt** `GET /commands`,
  um vom VPS-Webinterface angeforderte Kommandos auf den lokalen Bus zu geben
  (`gwInjectCommand`: Unicast `commandMsg`, bzw. `target 255` = Broadcast `settingsRequest`).
  So kann das Webinterface Geräte steuern, obwohl der VPS die 192.168.0.x-Geräte nicht
  direkt erreicht (siehe Kap. 5.12).
- **Eigene Anzeige-Settings:** der Manager hat selbst drei schaltbare Anzeige-Settings
  (HB-Empfang grün, catObserved-Empfang blau, catDetected-Empfang rot), im NVS gespeichert.

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

**Welt-Pose per Co-Observation (Radar kann sich nicht selbst lokalisieren).** Da das
Radar keine eigene Lokalisierung hat, erhält es seine Welt-Pose, indem es **gleichzeitig
mit einem welt-posierten Lidar dieselbe laufende Person** beobachtet; der VPS registriert
beide Bahnen und liefert die Transformation (Details: GesamtKonzeptCatFinder.md, Aufgabe
„Welt-Pose per Co-Observation kalibrieren"). Im Sketch:

- **Auslösung dreifach:** per **Knopf** (`cmdCalibrate` vom Touch-Remote, Kap. 5.11),
  **automatisch** bei anhaltender Co-Observation ohne gültige Pose, oder als
  **Re-Kalibrierung** nach erkanntem Pose-Drift.
- **Boot-Verhalten:** vorhandene NVS-Pose wird **direkt vertraut** (`validWorldPose=true`) —
  das Radar wird im Normalfall nicht bewegt (siehe Kap. 4.1). Kein gespeicherter Wert →
  `false`, dann startet Auto/Knopf eine Kalibrierung.
- **Health-Check (nur bei Auto-Kalibrierung „an"):** trifft eine welt-valide `catObserved`
  eines anderen Sensors ein und hat das Radar gerade ein eigenes Ziel, vergleicht es dessen
  (per `localToWorld` in die Welt gerechnete) Position mit dem gemeldeten Welt-Punkt.
  Anhaltendes Residuum zwischen Gate (~0,6 m) und Assoziationsgrenze (~1,5 m) ⇒ Pose
  driftet ⇒ `validWorldPose=false`. Größere Abweichungen gelten als „anderes Ziel" und
  werden ignoriert (dann hilft der Knopf). Der Check läuft **nur, wenn `stgAutoCalib`
  eingeschaltet ist** — nur dann kann sich das Radar nach einem Pose-Verwurf selbst wieder
  kalibrieren; bei Auto-Kalibrierung „aus" bliebe eine (womöglich falsch assoziierte)
  Verwerfung dauerhaft bestehen.
- **Nach Kalibrierung:** eigene `catObserved` tragen zusätzlich `worldX/worldY`
  (`worldValid=1`) via `localToWorld`.
- **RasenKarte-Gating:** sobald eine gültige Welt-Pose vorliegt, bezieht das Radar die
  **RasenKarte** vom Manager (`mapRasen`, gechunktes UDP wie die No-Shot-Karte, LittleFS-
  Cache mit Versions-/CRC-Abgleich, Retry alle 60 s) und meldet nur noch Ziele **innerhalb
  des Rasens** — Nachbargrundstück/Straße im 7-m-Radarkegel erzeugen sonst Dauer-
  Störungen. Kalibrier-Sammlung und Health-Check arbeiten weiter mit allen Zielen. Ohne
  Karte oder ohne Pose wird ungefiltert gemeldet (fail-open). Die **No-Shot-Karte** lädt
  das Radar bewusst nicht — es schießt nicht; No-Shot prüft der Aktor.
- **Einstellungen (Kap. 5.12):** HB-/Target-Anzeige schaltbar, Auto-Kalibrierung und
  Auto-Pose-Übernahme schaltbar (NVS); Aktionen „Kalibrieren", „Pose kopieren" und
  „Pose löschen" (`cmdClearPose`, verwirft die gespeicherte Welt-Pose, siehe 5.11).

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
Manager testen, ohne auf eine echte Katze zu warten. Seit dem 2. Ausbau
(2026-07) verwaltet er **mehrere Szenen** und ist für **Langzeitaufnahmen**
(über Nacht / mehrere Tage) ausgelegt.

- **9 Szenen-Dateien** `/scene1.bin` … `/scene9.bin` auf der SD-Karte; die
  **Tasten 1–9** wählen die aktive Szene. Eine alte Einzeldatei `/data.bin`
  wird beim Boot automatisch zu Szene 1 umbenannt (Recordformat unverändert:
  Header + Payload, variable Recordlänge).
- **Aufzeichnen** (Taste `r`, Umschalter): jede empfangene
  `catObserved`-Nachricht wird an die aktive Szene angehängt — **nur**
  `catObserved`, keine HBs (langzeittauglich: wenige Ereignisse = kleine
  Datei). Jeder Record wird einzeln geschrieben und die Datei geschlossen —
  bei Stromausfall gehen frühere Records nicht verloren. SD-Schreibfehler
  (z.B. volle Karte) werden im Display gemeldet statt still ignoriert.
- **Abspielen** (Taste `p`): sendet die aktive Szene Record für Record mit
  `broadcastRawMsg()` unverändert — **Original-Sender und -Zeitstempel
  bleiben erhalten**, die Empfänger können die Wiedergabe nicht von einer
  echten Detektion unterscheiden (Abstand fix 20 ms pro Record;
  Fortschrittsbalken, Abbruch mit beliebiger Taste). Vor dem Abspielen wird
  eine laufende Aufnahme automatisch beendet, damit sich der Simulator nicht
  selbst aufzeichnet; mitgehörte eigene Pakete werden danach verworfen.
- **Löschen** (Taste `d`, **zweimal** innerhalb 3 s — Schutz für mühsam
  aufgezeichnete Langzeit-Szenen): entfernt die aktive Szene.
- **Langzeitbetrieb:** WLAN-Überwachung mit automatischem Reconnect und
  Multicast-Rejoin (nächtlicher Router-Ausfall wird überbrückt); Display
  schaltet nach 2 min ohne Taste dunkel (erster Tastendruck weckt nur auf);
  NTP-Sync mit Timeout statt Endlosschleife (mobiles Gerät evtl. ohne
  Internet — Records tragen ohnehin die Zeitstempel der Original-Sender).
- **UI:** farbige Statusseite mit großen Fonts statt Scrolltext — Kopfzeile
  (Uhrzeit, WLAN-Status, grüner Punkt = Multicast-Verkehr in den letzten
  2 s), Szene + großer Record-Zähler, Statuszeile (blinkendes REC seit
  HH:MM, Zeit des letzten Records — lebt die Langzeitaufnahme noch?) und
  Tastenhilfe.
- **OTA aktiv** (Hostname `Simulator`); einen eigenen HB sendet der
  Simulator weiterhin nicht.
- Seit 6_3 nutzt der Simulator dieselben gemeinsamen Header wie alle anderen
  Programme (vorher: eigene udpDef/udpProc in Version 5_4), holt die
  WLAN-Zugangsdaten aus `Credentials.h` und ist als `Sim` in der device dB.

### 5.8 PA1_1_6_3_0 — PowerActor mit Schrittmotor (der "ältere" Werfer, ID 16)

Zweiter PowerActor-Bautyp ("PA1b"), der parallel zum PA2i existiert und auf
denselben `catObserved`-Strom reagiert. Er dreht den Werfer **nicht** mit einem
SMS_STS-Servo, sondern mit einem **Schrittmotor**:

- ESP32 (feste IP .183) → **PCF8574 (I2C @0x20)** → **A4988**-Treiber →
  Schrittmotor. Riemenübersetzung Pulley 20T : Zahnkranz 130T = 6.5; mit
  1/16-Mikroschritt ergibt das **20 800 Mikroschritte / Turmumdrehung**
  (1 PA-Einheit ≈ 5.08 Mikroschritte).
- **Positionsführung:** über Schrittzählung + **Reed-Referenzfahrt (Homing)**.
  Ein **AS5600**-Winkelgeber sitzt aktuell nur auf der Stepperwelle und wird
  vorerst lediglich ausgelesen/geloggt (geplant: Montage am Turm zur Erkennung
  übersprungener Schritte).
- **Winkelwelt:** dieselbe PA-Polar-Konvention wie PA2 (0..4096, 2048 =
  geradeaus), aber mit anderem nutzbarem Bereich: `maxAngle 180°` → Limits-
  Default 2048 ± 2048 (statt PA2: 60° / ± 682). Siehe Korrektur in Kap. 4.
- **Aktoren:** Wasserventil (= Feuer) und Ziellaser über Relais; zusätzlich
  Relais für externe Versorgung und Strom der Turmsensoren (bei Boot an);
  2× WS2812 als Status-Pixel.
- Protokoll-, HB- und Kommando-Verhalten entsprechen dem PA2i (Typ
  `PowerActor`); der Button reagiert daher automatisch auch auf diesen Aktor.
  PA1_1 ist der konkrete **zweite Aktor** für die Mehr-Aktor-Ausbaurichtung.

### 5.9 LaserMarker6_3 — Zielmarkierer/Indikator (ID 15)

Einfaches Spezialgerät auf **ESP32-C3 Super Mini** (feste IP .182, Gerätetyp
`Marker`). Stellt schaltbare Ausgänge bereit: **mainLaser** und **subLaser**,
einen frei verwendbaren **Aux**-Schaltausgang und ein **WS2812-Pixel**
(beliebige RGB-Farbe). Dient als per Netzwerk steuerbare Zielmarkierung bzw.
Statusanzeige.

Besonderheit: Der Marker ist **vollständig ohne die gemeinsamen Header
bedienbar** — die byte-genaue Netzwerk-API (welche UDP-Pakete man selbst packen
muss) ist in `LaserMarker/API_LaserMarker6_3.md` dokumentiert. Damit kann ihn
auch ein Fremdprogramm steuern, das nicht zur CatFind-Serie gehört.

### 5.10 C1Lidar6_3_0 — welt-fähiger Lidar-Sensor (ID 17)

RPLidar C1 auf **ESP32-S3** (Serial1, 460800), 2× WS2812. Erster Sensor mit
**Welt-Koordinaten** (`validWorldPose`). Ordner `CF_LidarC1/` (mehrteiliger
Sketch: `C1Lidar6_3_0.ino` + `hwDef.h` + `hwProc.ino` + `localizeProc.ino`).
Die generischen Bausteine (VPS-Lokalisierung `vpsLocalize`/`resolvePose`,
No-Shot-Beschaffung `acquireNoShot`, mirror-fähige Pose-Persistenz/Transformation)
liegen in `xComProc6_3.h` und stehen jedem welt-fähigen Gerät zur Verfügung; im
Sketch bleibt nur das Gerätespezifische (Scan aus dem Lidar-Hintergrund bauen).
Komplett neu gedacht ggü. der Vorläufer-Firmware
(`Lidar_C1_Prog/…/C1_Lidar_V2_OTA_Ueberw3`): **keine Wand/Nische mehr nötig**,
der Sensor steht frei auf dem Rasen.

Boot-Ablauf:
1. WLAN (DHCP) + OTA + LittleFS, Pose aus NVS lesen (Namespace `"c1pose"`,
   inkl. Drehsinn).
2. **20 s Hintergrund lernen** (rundum, per-Bin-Mindestabstände) → Perimetermodell.
3. **Welt-Pose per VPS:** den 360-Bin-Hintergrund als `{"scan":[…]}` per
   **HTTP-POST** an den Lokalisierungsdienst (`ipVPS:8080/localize`, siehe
   `VPS/localizer/`) schicken; Antwort = Pose (X, Y, Heading, Drehsinn,
   Konfidenz). NVS-Pose wird gegen die VPS-Pose plausibilisiert, sonst die
   VPS-Pose übernommen (in NVS gespeichert). Setzt `myPose.validWorldPose`.
4. **No-Shot-Karte:** lokale Kopie (LittleFS) gegen Version/CRC des Managers
   prüfen, bei Abweichung neu laden (`requestMap`), sonst (oder bei Manager-
   Ausfall) die alte Karte verwenden (siehe Kap. 4.2).
5. **Überwachung:** je Umdrehung die markierten Punkte (näher als der
   Hintergrund) zum Zentroid zusammenfassen, in Welt-Koordinaten umrechnen und
   **nur wenn im schießbaren Bereich** (`insideNoShot`) als `catObserved`
   broadcasten — mit relativen **und** Welt-Koordinaten (`worldValid=1`).

Status-Pixel: blau = Kalibrierung, gelb = VPS-Lokalisierung, violett =
No-Shot-Karte, aus = bereit, rot = nicht lokalisiert; bei Detektion im
schießbaren Bereich rot + Richtungs-Pixel. Nach der Init ein knapper
Text-Multicast (`LidarC1 loc=… x=… y=… h=… ns=…`).

Einstellungen (Kap. 5.12): Detektions-Anzeige (catObserved) schaltbar und im NVS
gespeichert; **Lidar-Motor** an/aus (`lidar.stop()`/`startScan()`), Default an und
**nicht** persistiert — der Sensor bootet immer mit laufendem Motor.

> **„Motor aus" = Sensor auf Idle.** Beim RPLidar C1 gibt es **keinen separaten
> Motor-Pin/PWM**: Dreh-Motor und Messkern hängen gemeinsam am Scan/Stop-Kommando.
> `lidar.stop()` sendet `RPLIDAR_CMD_STOP` (0x25), was den C1 **physisch herunterdreht
> und gleichzeitig das Sampling stoppt** (verifiziert in der Library `Lidar/src/RPLidar.cpp`).
> In der `loop()` greift bei Motor aus sofort `if (!motorRunning) return;`
> (`C1Lidar6_3_0.ino`) — es werden keine Messungen verarbeitet, keine Detektion,
> kein `catObserved` gesendet. Damit deckt „Motor aus" **beide** Zwecke ab:
> Fehl-Ereignisse bei starker Sonneneinstrahlung unterbinden **und** die drehende
> Mechanik schonen. Der Sensor bleibt im Netz präsent (Strom, WLAN, Heartbeat,
> reagiert auf Settings), misst/detektiert aber nichts — echtes Idle.
> Zu beachten: (1) **nicht persistiert** — nach Reboot/Stromausfall läuft der Motor
> wieder; ein per Display/VPS gesetztes Idle muss danach erneut gesetzt werden.
> (2) Beim Wiedereinschalten läuft **keine** Neu-Lokalisierung; die gecachte Welt-Pose
> (NVS) wird weiterverwendet — nur korrekt, wenn der C1 in der Idle-Zeit **nicht bewegt**
> wurde. (3) **Nicht schnell toggeln:** manche C1-Einheiten hängen bei „STOP/SCAN-Churn";
> `startScan()` fängt das über einen RESET-Retry (mit `delay(2500)`) ab.

> **VPS-Lokalisierungsdienst** (`VPS/localizer/`): Docker-Container mit einem
> HTTP-Dienst, der den 360-Bin-Scan mit `Map/RasenKarte.csv` (direkt aus dem
> GitHub-Repo geladen) global matcht (portiert aus
> `Lidar_C1_Prog/Position_estimate/lidar_localize.py`). Kartenänderung =
> `git push`, kein VPS-Eingriff. Erreichbar über `ipVPS` aus `Credentials.h`.
>
> **VPS-Dashboard / Treffervisualisierung** (`VPS/dashboard/`): zweiter
> Docker-Container (Port 80, `http://<VPS-IP>/`). Zeigt — gespeist vom Manager
> als Gateway (siehe 5.1) — ein scrollendes System-Debug-Fenster, die zuletzt
> aktiven Geräte (HB), eine pro Minute zusammengefasste Ereignisliste (Zeit,
> **Trefferzahl** und meldende Sensoren) und Karten der `catObserved` in Welt-
> bzw. relativen Gruppen-Koordinaten (Farbe je Sensor/Ziel, Reset-Button). State
> der Live-Ansichten im RAM (kumuliert bis Reset).
>
> **Tab „Analyse"** (Aufgabe „VPS-Modellierung der Katzenerkennung",
> GesamtKonzeptCatFinder.md): zeichnet alle `catObserved` **persistent** in
> eine SQLite-DB im Docker-Volume `/opt/catfinder/data/` auf (REC-Schalter =
> Pause/Append, Zustand persistent; überlebt Container-Neubauten, nichts in
> GitHub). UI: Die **Zeitleiste zeigt genau das gewählte Zeitfenster**
> (Ereignisdichte je Sender, Zeit-Gitter, Aufnahme-Band, Labels, rote
> CatDetected-Punkte); Ziehen = verschieben, Mausrad = strecken/stauchen,
> „Alles" = ganze Aufnahme, Shift+Ziehen = Bereich für Label. Welt-Karte mit
> Pan/Zoom, Sensoren einzeln ausblendbar, Labels (Katze/Störungsarten)
> persistent. **Erfassungsbereiche**: die Sensoren tragen ihren nominellen
> Bereich in `xComDef6_3.h` (`covLeftDeg/covRightDeg/covRangeMm`; HLK-Radar
> −60…+60° 7 m, Lidar 360° 12 m); der Manager reicht die `poseReport` der
> Geräte per Gateway-Push weiter (`poses`, alle 5 min per
> `poseRequest`-Broadcast angefragt), der VPS legt daraus die Sektoren in die
> Karte („Erfassung"-Checkbox; „Geräte ⟳" liest xComDef sofort neu + fragt
> Posen an; Fallback ohne Posen: empirische Abdeckung). Das **Modell**
> (`catmodel.py`: Track-Bildung, Sturm-Erkennung je Sensor, Score-System,
> Sensor-Gewichte — Lidar allein bestätigt nie —, Fusion, Randlogik an der
> Gesamt-Abdeckung) markiert bestätigte Tracks als **CatDetected** mit
> **rotem Punkt am Auslösezeitpunkt** (bevor die Katze den Erfassungsbereich
> verlässt). Dieses Modell ist die Referenz für den Cat Identifier (Kap. 5.13),
> der es in Echtzeit auf dem ESP32 fährt und `catDetected` sendet.
> **Kontinuierliche Erkennung, unabhängig von der Betrachtung:** ein
> Hintergrund-Thread arbeitet die Aufnahme fortlaufend in Häppchen ab
> (`analyze_stream`), vergibt **fortlaufende Track-Nummern** und legt fertige
> Tracks persistent in SQLite ab — `/amodel` liest nur noch, Zoom/Fenster
> haben keinen Einfluss mehr auf das Ergebnis. Parameter- oder Mäher-Label-
> Änderungen lösen automatisch einen Neuaufbau aus (Bewertungen/Klebungen
> überleben über den stabilen Track-Schlüssel). **Manuelle Track-Bewertung**:
> je Track „🐱 Katze"/„🧍 Person"/„🐦 Vogel"/„🤖 Mäher"/„🦗 Insekt"/„⛈ Sturm"/
> „✕ Störung"/„🚫 sicher keine Katze" (persistent; keine Markierung =
> einverstanden; alles außer Katze zählt als „keine Katze"), oben die
> **Übereinstimmung Modell↔Mensch**; Modell-Flags als Chips. Der angeklickte
> Track wird **gelb** in Karte und Zeitleiste hervorgehoben; mehrere Tracks
> lassen sich **zusammenkleben** (gehören zum selben Tier). Knopf
> **„Modell-Check"** (`/avalidate`) prüft das Modell gegen alle bewerteten
> Tracks; die Trackliste ist als **CSV** exportierbar (`/atracks.csv`).
> **RoboMäher**: Label „Mäher" = Analyse-Ausschlussfenster (aufgezeichnet
> und sichtbar bleibt alles — gut zum Ausleuchten der Erfassungsbereiche);
> zusätzlich verwirft das Modell Tracks mit Weg > `max_path_mm` (40 m)
> automatisch als „Mäher/Person?". Alle Schwellwerte in `model_params.json`
> im Volume, ohne Rebuild im UI änderbar (Endpunkte `/rec /density /adata
> /amodel /aparams(.csv) /alabels /amark /amerge /atracks(.csv) /avalidate
> /devreload /acoverage`).
>
> **Tab „Bilder"** (Cat Cam, Kap. 5.14): Foto-Kacheln der Kamera (neueste
> zuerst) mit Auslöser (CatDetected / Kamera-KI / manuell), KI-Score und der
> **zugeordneten Track-Nummer**; Klick öffnet die Großansicht. Endpunkte
> `POST /photo`, `GET /photos`, `GET /photo/<id>`; Fotos im Volume
> `/data/photos`, Metadaten in SQLite.

### 5.11 radarCalibrationButton — Touch-Fernbedienung für die Radar-Kalibrierung

Kleines Remote-Gerät, das die **Co-Observation-Kalibrierung** eines Radars per
Knopfdruck steuert (siehe Kap. 4.1 und GesamtKonzeptCatFinder.md, Aufgabe
„Welt-Pose per Co-Observation kalibrieren"). Basiert auf `Udisp6_3_0` (gleiches
**CYD35**-LovyanGFX-Profil inkl. XPT2046-Touch), ist aber UI-reduziert auf
**Zielauswahl + zwei Touch-Buttons**. Ordner `Displays/radarCalibrationButton/`.

- **Hardware:** CYD 3.5" (ID `CYD35Z`, classic ESP32, ST7796-Panel + resistiver
  XPT2046-Touch), DHCP. Upload per **USB (COM9)**; OTA ebenfalls aktiv.
- **Zielauswahl:** oben eine Chip-Reihe mit **allen HLK-Radaren aus der
  Gerätetabelle** (`device[].type == HLK`: Dome, Mini_Dome, Compact_Dome — neue
  Radare erscheinen ohne Code-Änderung). Antippen wählt das Ziel; jeder Chip
  zeigt, ob die IP schon per HB gelernt wurde (`.37` grün bzw. „kein HB").
- **Funktion:** Zwei Schaltflächen senden dem **gewählten** Radar per **Unicast**
  eine `commandMsg`:
  - **KALIBRIEREN** → `cmdCalibrate` (`info` = 45000 ms) — startet den Kalibriermodus.
    Nach dem Senden **zählt der Button das 45-s-Fenster sichtbar herunter**
    („laeuft … 37s", ocker statt grün); sekündlich wird nur der Button neu
    gezeichnet (kein Flackern), das Ergebnis meldet das Radar danach per
    Text-Multicast in die Statuszeile.
  - **POSE LÖSCHEN** → `cmdClearPose` — verwirft die gespeicherte Welt-Pose
    (`validWorldPose=false`); für den Fall, dass das Radar **bewegt** wurde und der
    Health-Check die grobe Abweichung nicht selbst erkennt (Kap. 5.2).
  Die Ziel-IPs werden wie üblich aus den HBs gelernt; ist das gewählte Radar noch
  nicht gesehen, zeigt das Display einen Hinweis statt zu senden.
- **Rückmeldung:** Das Gerät lauscht auf den **Text-Multicast** (Port 8300) und
  zeigt die Statusmeldungen des Radars an (`calib Knopf …`, `calib OK/FAIL …`,
  `Pose geloescht …`).
- Sendet selbst **keinen HB** (reines Bediengerät) und taucht daher nicht in der
  HB-/Geräteliste auf — wie die anderen Displays.

### 5.12 Bedienung_Einstellungen — Touch-Bediendisplay für Einstellungen & Steuerung

Bedien-Display, mit dem sich die **Anzeige-/Automatik-Einstellungen** und **Aktionen**
aller aktiven Geräte per Touch bedienen lassen (siehe GesamtKonzeptCatFinder.md, Aufgabe
„Einstellung und Steuerung der CatFinder Elemente"). Ordner
`Displays/Bedienung_Einstellungen/`.

- **Vollständige Kopie des Display-Prototyps** (`Udisp6_3_0`): dieselbe Display-Auswahl
  per `#define` (CYD28/CYD35/Sunton7/Sunton5/M5Core2/M5Tab5/Wave7), dieselben
  LovyanGFX-Profile (`dispDevLoGFX.h`) und Bildschirm-Definitionen (`dispDef.h`). Das
  Karten-/Menü-Betriebsprogramm (Sprite-Layer, Encoder-Menü) wurde entfernt und durch eine
  **autoskalierte Touch-Bedienung** ersetzt (keine abgespeckte Variante — der volle
  Prototyp bleibt Basis).
- **Bedienmodell:** Jedes Gerät führt eine Bitmaske `mySettings` (`deviceSettings`):
  `supported`/`values`/`actions`. WELCHE Bits ein Gerät hat, legt seine `hwDef.h` über
  `STG_SUPPORTED`/`STG_DEFAULT`/`STG_PERSIST`/`STG_ACTIONS` fest. `initSettings()` lädt die
  persistierten Bits aus dem NVS-Namespace `"devcfg"`; nicht persistierte Bits (Lidar-Motor)
  starten auf Default. Setting-Indizes `stgHbLed/stgCatLed/stgAutoCopyPose/stgAutoCalib/
  stgLidarMotor/stgCatDetLed/stgCamAi`, Aktionen `actCopyPose/actCalibrate/actClearPose/
  actReloadParams/actPhoto`.
- **Seite 1 (Geräteauswahl):** listet die Geräte, die per `settingsReport` ihre
  Fähigkeiten gemeldet haben (Broadcast `settingsRequest` beim Start und periodisch). Touch
  wählt ein Gerät → Seite 2.
- **Seite 2 (Details):** je unterstütztem Setting ein **AN/AUS-Schalter** (Touch →
  `commandMsg/cmdSetSetting`, `info=(idx<<1)|wert`) und je Aktion ein **Button** (Touch →
  `cmdCopyPose` bzw. `cmdCalibrate`). Der Zustand wird optimistisch gesetzt und durch das
  zurückkommende `settingsReport` bestätigt. Ziel-IPs werden aus den HBs gelernt.
- **Anzeige-Settings betreffen nur die LED/Anzeige**, nicht die Funktion: die Geräte prüfen
  `settingOn(...)` nur vor dem Setzen der Pixel; Broadcasts/Detektion laufen weiter. Beim
  Manager sind es die Empfangs-Anzeigen (HB grün / catObserved blau).
- **Hardware/Build:** Default **CYD35** (per USB, wie das Touch-Remote in 5.11); OTA aktiv.
  Das Display sendet keinen HB (Bediengerät) und teilt sich die ID des jeweiligen
  Screen-Typs.

> **VPS-Webinterface (Steuerungs-Tab):** Alternativ zur Display-Bedienung bietet das
> VPS-Dashboard einen Tab **„Steuerung"** (umschaltbar wie die Karten) mit denselben
> Schaltern/Aktionen je aktivem Gerät. Da der VPS die 192.168.0.x-Geräte nicht direkt
> erreicht, wirkt der **Manager als Steuer-Gateway**: er meldet die `settingsReport` an den
> VPS (`/ingest`) und **pollt** `GET /commands` (CSV „target,cmd,info"), um vom Webinterface
> (`POST /command`) angeforderte Kommandos auf den lokalen Bus zu geben. `target 255` =
> Broadcast `settingsRequest`, `target 254` = Broadcast `poseRequest` (Welt-Posen für die
> Erfassungssektoren). Kommandos mit **target = Manager** führt der Manager **lokal** aus
> (`handleCommonMsg`) — ein UDP-Unicast an die eigene IP loopt nicht in den eigenen Socket
> zurück, darum reagierten seine eigenen Schalter früher nicht.

### 5.13 CatId6_3_0 — Cat Identifier (Echtzeit-Katzenerkennung, ID 18)

Eigenständiges Gerät, das das Katzen-Erkennungsmodell **in Echtzeit auf dem Bus**
laufen lässt — der Streaming-Port des VPS-Referenzmodells `catmodel.py`. Ziel:
die Erkennung, die bisher offline im Analyse-Tab lief, live auf einem ESP32.
Ordner `CatIdentifier/CatId6_3_0/`. Hardware: **Seeed XIAO ESP32-S3**, feste IP
.184.

- **Eingang:** hört alle `catObserved` mit `worldValid=1` vom Bus und füttert sie
  ins Modell (`catTrack.ino`): Track-Bildung (Gate + Stitching), kohärente
  Bewegungsphase als Pflicht, Score 0..100, Sturm-/Burst-Unterdrückung je Sensor,
  Mäher-K.o. über die Weglänge (`max_path_mm`). Der Erfassungsrand wird
  **geometrisch** aus den Sektoren der device dB (`covLeft/covRight/covRange`) und
  den per `poseReport` mitgehörten Welt-Posen berechnet (wie `build_coverage_geo`
  auf dem VPS) — beim Boot fragt CatIdent einmal per `poseRequest` nach.
- **Kausaler Unterschied zum VPS:** dort wird ein *fertiger* Track als Ganzes
  bewertet; hier muss entschieden werden, **während** die Katze noch im Feld ist.
  Darum fließen nur die zum Zeitpunkt bekannten Merkmale ein (kein Austritts-Term),
  und die Bestätigung feuert, sobald der Score die Schwelle erreicht.
- **Ausgang:** bei Bestätigung wird `catDetected` **doppelt** gebroadcastet
  (UDP-Verlustschutz) — der Manager blinkt rot, die Cat Cam fotografiert.
- **Modell-Parameter** kommen vom VPS (`GET /aparams.csv`, flache `key=value`-Liste
  — dieselben Werte wie im Analyse-Tab „Parameter…") und werden im LittleFS
  gecacht (`/catparams.csv`), überleben also VPS-Ausfälle/Reboots. Die Aktion
  **„Parameter laden"** (`cmdReloadParams`, VPS-Steuerung/Display) holt sie neu —
  so lässt sich das Modell iterieren, **ohne den ESP32 neu zu flashen**.
- **Onboard-LED** (GPIO21, active-low): HB-Blitz (`stgHbLed`) und CatDetected-Blitz
  (`stgCatLed`).

> **Antenne:** der XIAO ESP32-S3 hat **keine** PCB-Antenne, nur eine IPEX-Buchse —
> ohne angesteckte Antenne ist er praktisch funkblind. Statische IP erfordert
> `WiFi.config()` **mit DNS** (Kap. 2), sonst hängt die NTP-Synchronisation.

### 5.14 CatCam6_3_0 — Cat Cam (Vision-KI-Kamera + Fotos, ID 19)

**Seeed XIAO Vision AI Camera**: ein XIAO ESP32-C3 (führt dieses Programm) +
Grove Vision AI V2 (Himax WiseEye2 — das KI-Modell läuft **dort**, deployt per
SenseCraft-Web) + OV5647 (5 MP, 62° FOV). Der C3 spricht das Vision-Modul per
**I2C** über die SSCMA-Bibliothek an. Ordner `CatCam/CatCam6_3_0/`, feste IP .185.
Zwei Aufgaben:

1. **Foto bei `catDetected`:** hört die Bestätigungen des Cat Identifiers mit
   (Doppelsendung wird dedupliziert), macht ein Foto und lädt es zum VPS hoch
   (`POST /photo`, Base64-JPEG + Metadaten). Foto-Mindestabstand, damit ein
   Detektions-Schauer nicht die Kamera flutet.
2. **Eigene KI-Erkennung** (schaltbar über `stgCamAi`): fragt das Vision-Modul im
   Takt (~4 Hz, `AT+INVOKE`). Sieht es eine **Katze im Bild**, broadcastet die
   Kamera `catObserved` (mit `worldValid=0` — sie kennt keine Welt-Position; das
   Ereignis ist Zusatz-Evidenz + Zeitmarke, KI-Score im `res`-Feld) und lädt beim
   ersten Auftauchen einer Sichtung ein Foto hoch.

- **Katzen-Klasse automatisch:** welche Klassen-ID „Katze" ist, hängt vom
  deployten Modell ab. CatCam liest beim Start die Modell-Info aus (Base64-JSON),
  sucht in der Klassenliste den Eintrag mit „cat" und stellt sich darauf ein —
  funktioniert damit für „Cat Detection", „Pet Detection" (cat/dog) und
  COCO-Modelle ohne Neuflash (Fallback: Klasse 15 = COCO-Katze). Das aktive
  Modell und die gefundene Klasse meldet sie ins VPS-Debug.
- **Aktion „Foto jetzt"** (`cmdTakePhoto`) für Test/Ausrichtung.
- **Auto-Reconnect:** ist das Vision-Modul beim Boot noch nicht bereit, versucht
  CatCam alle 5 s neu, sich zu verbinden (statt bis zum nächsten Reboot „Modul
  fehlt" zu melden).
- Zwei Eigenheiten der SSCMA-Bibliothek 1.0.3 werden umgangen: ihr `invoke()`
  kann „Bild ohne DIFFERED" nicht ausdrücken und ihr internes Timeout (1 s) ist
  für ~30 kB Base64 über I2C zu knapp — der Foto-Abruf sendet das AT-Kommando
  daher selbst und sammelt die Antwort über die öffentliche Lese-API ein.
- Der XIAO ESP32-C3 hat **keine** User-LED; WLAN nur über die IPEX-Antenne
  (wie CatIdent), statische IP mit DNS (Kap. 2).

> **Modell aufspielen:** Ab Werk trägt das Vision-Modul „Face Detection" (erkennt
> keine Katzen). Ein Katzen-/Pet-Modell wird über die **SenseCraft-AI-Weboberfläche**
> (Chrome, Web-Serial) auf den **USB-Port des Vision-Moduls** deployt — *nicht* auf
> den des ESP32-C3. Danach erkennt CatCam die Katzen-Klasse automatisch.

> **VPS-Bilder-Tab:** Das Dashboard hat einen Tab **„Bilder"** (`GET /photos`,
> `GET /photo/<id>`): Foto-Kacheln (neueste zuerst) mit Auslöser (CatDetected /
> Kamera-KI / manuell), KI-Score und der **zugeordneten Track-Nummer** des
> kontinuierlichen Analysierers (über Zeitüberlappung). Klick öffnet die
> Großansicht. Fotos liegen im Docker-Volume (`/data/photos`), Metadaten in SQLite.

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
| `localToWorld` / `worldToLocal` | Umrechnung lokal ↔ Welt (Kap. 4.1) |
| `savePose` / `loadPose` | Welt-Pose im NVS speichern/lesen (Kap. 4.1) |
| `crc32Bytes` | CRC32 (zlib-kompatibel) für die Karten-Übertragung |
| `mapFileInfo` / `serveMap` | Karte aus LittleFS analysieren / gechunkt senden (Kap. 4.2) |
| `requestMap` / `mapBeginRx` / `mapFeedChunk` | Karte anfordern / empfangen (Kap. 4.2) |
| `loadNoShot` / `insideNoShot` | No-Shot-Polygon(e) aus LittleFS laden / Welt-Punkt im schießbaren Bereich? (Point-in-Polygon) |
| `initSettings` / `saveSettings` / `settingOn` | Geräte-Einstellungen (NVS "devcfg", STG_*-Masken aus hwDef) laden/speichern/abfragen (Kap. 5.12) |
| `sendSettingsReport` / `sendPoseReport` | eigene Einstellungen / Welt-Pose broadcasten |
| `handleCommonMsg` | settingsRequest/poseRequest/cmdSetSetting generisch behandeln |
| `copyPoseFromGroup` | Welt-Pose eines Gruppenmitglieds übernehmen (Aktion `actCopyPose`) |
| `acquireNoShot` | No-Shot-Karte generisch vom Manager beziehen/cachen (für jedes welt-fähige Gerät) |
| `vpsLocalize` / `resolvePose` | Scan an den VPS → Pose / Pose gegen NVS plausibilisieren + speichern (`vpsLocalize` nur mit `USE_VPS_LOCALIZE`) |
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
├── PowerActor1_1/PA1_1_6_3_0/    ← Schrittmotor-PowerActor (ID 16, siehe 5.8)
│                                    daneben: Development/ Infrastructur_test/ Tests/
├── Radar_HKL/Radar6_3_0/
├── Displays/Udisp6_3_0/          ← + dispDef.h/dispDevLoGFX.h/dispProcLoGFX.ino
│   ├── ../radarCalibrationButton/ ← Touch-Remote: startet Radar-Kalibrierung (CYD35, siehe 5.11)
│   └── ../Bedienung_Einstellungen/ ← Touch-Bediendisplay Einstellungen/Steuerung (Kopie des Prototyps, siehe 5.12)
├── Simulator/Sim6_3_0/
├── LaserMarker/
│   ├── LaserMarker6_3/           ← Zielmarkierer (ID 15, siehe 5.9)
│   └── API_LaserMarker6_3.md     ← byte-genaue Netzwerk-API
├── CF_LidarC1/C1Lidar6_3_0/      ← welt-fähiger Lidar (ID 17, ESP32-S3, siehe 5.10)
├── CatIdentifier/CatId6_3_0/     ← Echtzeit-Katzenerkennung (ID 18, XIAO ESP32-S3, siehe 5.13)
├── CatCam/CatCam6_3_0/           ← Vision-KI-Kamera + Fotos (ID 19, XIAO Vision AI, siehe 5.14)
├── VPS/localizer/                ← Docker-Lokalisierungsdienst (HTTP, Pose aus Scan+Karte)
├── VPS/dashboard/                ← Docker-Treffervisualisierung + Analysierer + Bilder (Web, Port 80)
└── Tests/                        ← vom Versionsschema ausgenommen

(Die 6_2-Ordner — PA2i6_2_0, Radar6_2_0, … — existieren weiterhin parallel.)

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
3. Der **Cat Identifier** verfolgt die `catObserved` als Track. Erreicht der
   Modell-Score die Schwelle (kohärente Bewegung, Erfassungsrand, Fusion …),
   broadcastet er `catDetected` (doppelt) → **Manager blinkt rot**, die
   **Cat Cam** macht ein Foto und lädt es zum VPS hoch (Bilder-Tab, mit
   Track-Nummer). Erkennt das Vision-Modul der Cat Cam eine Katze im Bild,
   liefert sie zusätzlich `catObserved` + Foto.
4. PA2 (Ziel-Logik, in Arbeit): Servo auf `angle` drehen, Laserdistanz
   gegen `radius` prüfen, bei Bestätigung und `readyToFire && limitsActive`-
   Freigabe: Ventil auf → `catHit`-Broadcast → Display könnte den Treffer
   auf dem hitLayer markieren. (Perspektivisch schießt der Aktor auf
   `catDetected` statt auf rohe `catObserved` — validiertes Ziel.)

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
