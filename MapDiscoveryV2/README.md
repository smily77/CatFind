# MapDiscoveryV2 – Garten vermessen mit Laser-Schwenkkopf

Komplett neu geschriebene Fassung von [`../MapDiscovery`](../MapDiscovery)
(V1 bleibt unverändert). Hardware, Bedienung, Ablauf und Aussehen sind
gleich wie in V1. Neu geschrieben ist die Software darunter.

| Ordner | Board | Zweck |
|---|---|---|
| `PanTiltHead_WM_V2/` | ESP32 classic (Labor-Turm) oder XIAO ESP32-S3 | Schwenkkopf: Servos, Entfernungsmesser **SEN0366 oder X40** (automatisch erkannt), LaserMarker |
| `PanTiltKarte_WM_V2/` | M5Stack Tab5 + Unit 8Encoder | Bedienung, Kalibrierung, Kartierung, Auto-Scan, CSV auf SD |
| `Common/` | – | `MDV2Config.h` (Pins, Servo-IDs, Ursprünge, Tilt-Typ) und `MDV2Protocol.h` (UDP-Protokoll) |
| `tools/` | PC (Python 3, pyserial) | Testwerkzeuge, siehe unten |

## Was gegenüber V1 anders ist

**Sichtbar (vom User so gewählt)**
- **Tilt-Korrektur:** In die Karte geht die waagrechte Distanz `d · cos(Tilt)`.
  Tilt ist der vom Servo gelesene Ist-Wert. Antwortet der Tilt-Servo nicht,
  nimmt die Karte den Sollwert.
- **Auto-Scan-Schrittweite wählbar:** Der Knopf rechts neben AUTO SCAN schaltet
  0,5 → 1 → 2 → 5° um. Die Wahl bleibt über einen Neustart erhalten (Standard 2°).

**Darunter**
- **Protokoll mit Auftragsnummern:** Jede Antwort gehört eindeutig zu einem
  Auftrag. Verspätete Antworten können keine Messung mehr verfälschen.
  Geht ein Paket verloren, wird es wiederholt. Ein wiederholter Messauftrag
  löst keine zweite Messung aus.
- **Messablauf im Head:** Bei Anti-Backlash zuerst die Vorfahrt, dann die
  Zielfahrt. Erst wenn Pan am Ziel steht (±0,5°), folgen Ausschwingen,
  Lesen der Ist-Winkel, Messung (bis zu 3 Versuche) und die Antwort. Während
  einer Messung bleibt der Kopf stehen.
- **Auto-Scan** wartet nicht mehr eine feste Zeit, sondern auf die Antwort des
  Heads. Es gibt bis zu 1000 Punkte statt 100. Ist die Karte voll, kommt
  eine Meldung statt eines stillen Verlusts.
- Doppelte Punkte (gleicher Pan ±0,1°) werden immer nach dem Pan-Winkel
  erkannt, also nach demselben Massstab wie bei der Sortierung.
- Encoder-Drehungen während einer Messung, im Edit-Modus und im Scan werden
  verworfen. In V1 sammelten sie sich an und sprangen danach auf einmal.
- Die Karte findet den Head per Netz-Broadcast (z. B. 192.168.0.255) und
  spricht ihn danach direkt an. `255.255.255.255` kommt beim Tab5 nicht an.
- Anzeige über Puffer im PSRAM, daher kein Flackern.

**Gleich geblieben:** Knöpfe, Encoder (0 Pan grob 3°, 1 Pan fein 0,2°,
7 Tilt 0,5°, 6 Punkt wählen im Edit-Modus), Zustände (Origin → X-Achse →
Kartieren / Auto-Scan / Edit / Anfahrt prüfen), CSV-Format `/karte_NNN.csv`
(`# PanTiltKarte  Kopf=(x,y)  phi=<rad>`, `# X,Y`, Punkte in Metern),
LaserMarker-Ansteuerung durch den Head.

**Nicht kompatibel mit V1:** Head und Karte V2 sprechen ein neues Protokoll.
Beide Geräte müssen dieselbe Version haben.

## Protokoll (kurz)

Die Karte sendet `MDV2CmdMsg` (16 B) an Port 4210, der Head antwortet mit
`MDV2ReplyMsg` (24 B) an Absender-IP und -Port (4211). Beide beginnen mit der
Kennung `MDV2`.

- **SET** = Sollzustand (Pan, Tilt, Laser, Marker). Die Karte sendet ihn bei
  jeder Änderung und sonst jede Sekunde als Lebenszeichen. Der Head antwortet
  `DONE`, während einer Messung `BUSY`.
- **MEASURE** = anfahren und messen. Der Head antwortet sofort `ACCEPTED`,
  danach `DONE` mit Schrägdistanz, Ist-Pan, Ist-Tilt und Sensortyp, oder
  `FAILED` bzw. `NOSENSOR`.

Details stehen in `Common/MDV2Protocol.h`.

## Kompilieren und Hochladen

```bash
# Head – Labor-Turm (ESP32 classic) an COM18
arduino-cli compile --fqbn esp32:esp32:esp32 MapDiscoveryV2/PanTiltHead_WM_V2
arduino-cli upload  --fqbn esp32:esp32:esp32 --port COM18 MapDiscoveryV2/PanTiltHead_WM_V2
# (XIAO ESP32-S3: --fqbn esp32:esp32:XIAO_ESP32S3)

# Karte – Tab5 an COM19
arduino-cli compile --fqbn m5stack:esp32:m5stack_tab5 MapDiscoveryV2/PanTiltKarte_WM_V2
arduino-cli upload  --fqbn m5stack:esp32:m5stack_tab5 --port COM19 MapDiscoveryV2/PanTiltKarte_WM_V2
```

Libraries: FeetechBusServos, Laser_RangeFinder_X40 (`LaserRangefinderAuto`)
mit LaserRangefinder_SEN0366, M5Unified, M5GFX, M5UNIT_8Encoder und die
lokale `MyLGFXConfigs` (`Credentials.h`, nicht im Repo).

Der Head gibt seriell (115200) jede Messung mit Sensor, Distanz, Ist-Winkeln
und Dauer aus, zum Beispiel
`[2902] X40 1.456 m  Pan ist -0.09  Tilt 1.93  (Fahrt 1068 ms, Messung 619 ms)`.

## Testwerkzeuge (`tools/`)

| Datei | Zweck |
|---|---|
| `headtest.py [IP]` | prüft einen echten Head über UDP: Antworten, Messung, Wiederholung, Anfahrt, SET während Messung, Laser/Marker, Wiederholgenauigkeit |
| `headsim.py` | simulierter Head mit erfundenem Garten (bekannte Kopf-Pose, Pfosten O/A, Gartengrenze, Servo-Rundung, Tilt, 20 % Paketverlust) |
| `karte_e2e.py COM19 [PC-IP] --reset` | Ende-zu-Ende-Test der echten Karte gegen den Simulator, gesteuert über den seriellen Testzugang |
| `karte.py` | serieller Testzugang zur Karte (wird von `karte_e2e.py` benutzt) |
| `serlog.ps1 COM18 20` | serielle Ausgabe mitlesen, ohne das Board zurückzusetzen |

**Serieller Testzugang der Karte** (115200, eine Zeile je Befehl): `pan <deg>`,
`tilt <deg>`, `enc <i> <delta>`, `btn <laser|marker|act|scan|step|edit|undo|save|check>`,
`tap <x> <y>`, `head <ip|auto>`, `dump`, `restart`. Jede Statusmeldung
erscheint zusätzlich als `STATUS: ...`.

## Teststand 2026-11-16

- `headtest.py` gegen den Labor-Turm mit X40: 15/15 OK. Mit Anfahrt fünfmal
  dieselbe Stelle gemessen, die Distanz streut um 1 mm.
- `karte_e2e.py` (Tab5 gegen Simulator, 20 % Verlust): 0 Fehler. Kopf-Pose auf
  1 mm und 0,03° genau, Punkte mit Tilt -5° auf 1,5 mm genau auf der Grenze.
  Der Rest ist die Servo-Rundung.
- Tab5 mit dem echten Head (X40): Kalibrierung, Punkte und Auto-Scan laufen.
- **Noch offen:** Test mit SEN0366 und Feldtest mit echten Referenzpunkten.

## Bekannte Eigenheiten

- **X40:** Auf gutem Ziel misst er in etwa 0,6 s, auf dunklem oder schrägem Ziel
  in 3–7 s. Im Labor lieferte er einmal 34,11 m statt 0,75 m. Solche Ausreisser
  kommen vom Sensor selbst.
- **Tilt-Servo (SC15):** Bei Soll 0° steht er auf etwa ±1,9° (Totband). Für die
  Distanz ist das vernachlässigbar (0,5 mm pro Meter).
- **Tab5:** SD-Karte über SPI, nicht SDMMC (teilt sich die Peripherie mit dem
  WLAN). Beim Kaltstart kann der Unit 8Encoder im Bootloader hängen, das Byte
  `0x77` an 0x54 holt ihn zurück.
