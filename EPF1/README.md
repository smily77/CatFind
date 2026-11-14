# EPF1 – Drehturm-Plattform fürs CatFinder-Netz

**Stand 2026-11-14 abends: Bus-Firmware `EPF1_6_3_0` (Gerät #23, DHCP) in der Hecke, Betriebsart IR (Strahler), Schalter AUTO.**

EPF1 ist ein neues Gerät im CatFinder-Netz: ein **Drehturm** (Pan + Tilt), auf dem
verschiedene Elemente montiert werden können, z. B. Lidar, Lichter oder Laser. Die
Firmware hat dafür **Betriebsarten** (`EPF1_MODE_...` in `EPF1Config.h`, Code je in
`EPF1_6_3_0/mode<Name>.ino`). Betriebsarten: **Lidar** (TOFSense-F), **X40** (Laser-Entfernungsmesser), **IR** (Strahler).

### Betriebsart IR (`EPF1_MODE_IR`, `modeIR.ino`)

- Schaltleitung = **SDA des Slave-Ports (GPIO7, Turmkabel GE)**: HIGH = Strahler ein, LOW = aus.
- Schalter wie Dome-C1 (`stgIrLight` + `stgIrAuto`, persistiert, VPS/Display: AUS / EIN / AUTO):
  AUS = aus, EIN = dauernd ein, **AUTO = Katze anleuchten**: jede `catObserved` eines anderen
  Sensors schaltet 60 s ein; mit Welt-Koordinaten und gültiger EPF1-Pose dreht der Turm
  in Richtung der Katze (Nachführung alle ≥ 0,3 s, Totband 2°). Danach aus und zurück auf 0.
- Standort Hecke: **Pan nur ±85°** um die 0 (`MODE_PAN_LIMIT_DEG`, gilt für alle Pan-Befehle).
- Seriell `i` = IR-Status. Meldungen „IR-Strahler …“ / „IR zielt auf Katze …“ im Debug-Fenster
  (gehen dort gelegentlich verloren, direkt auf 239.0.0.57:8300 sind sie vollständig).

- **Mechanik und Elektronik** = „Turn Base Compact“ von MapDiscovery
  (`../MapDiscovery/PanTiltHead_WM`, `TurnBaseConfig.h`): **Seeed XIAO ESP32-S3**.
  Beim EPF1 ist der Tilt ein ST-Servo (`#define TILT_SERVO_ST`), die Umschaltung
  `TILT_SERVO_ST` / `TILT_SERVO_SC` bleibt für den MapDiscovery-Kopf (**EPF1b**).
- Schema: `Schema_EPF1.pdf` (gedreht gescannt).

## Pinbelegung (XIAO ESP32-S3, aus dem Schema)

| GPIO | Funktion | Bemerkung |
|---|---|---|
| 1 (D0) | Servo-Bus TX | über **1 kΩ** auf die Servo-Datenleitung |
| 2 (D1) | Servo-Bus RX | direkt auf die Datenleitung |
| 7 / 8 (D8/D9) | Slave-Port / Turmkabel „SIS“ | UART oder I2C je nach Aufsatz. TOFSense-F: I2C, SDA 7, SCL 8 (2026-11-14 erkannt) |
| 5 / 6 (D4/D5) | I2C-AUX-Stecker | SDA/SCL-Zuordnung noch prüfen |
| 4 (D3) | WS2812 (5 mm) | aus dem Schema gelesen, noch nicht bestätigt |

## Turmkabel (laut Schema)

| Farbe | Signal |
|---|---|
| GR | +12 V |
| BR | Servo-Datenleitung |
| OR | SIS (SCL) |
| GE | SIS (SDA) |
| RT | +5 V |
| BL | GND |

## Servos

| Achse | Typ | ID | Origin | Umrechnung | Grenze (vorläufig, aus MapDiscovery) |
|---|---|---|---|---|---|
| Pan | Feetech ST3020 | 2 | 2048 | 4096 Ticks/360° | ±170° |
| Tilt | ST (EPF1) / SC15 (EPF1b) | 4 | 2048 / 680 | 4096/360° bzw. 1024/180° | ±30° |

- Servo-Bus mit 1 Mbit/s, Library `FeetechBusServos` (smily77/SerialServo).
- ST und SC sind verschiedene Klassen (`FeetechST3020` / `FeetechSC15`), weil ihre
  Byte-Reihenfolge verschieden ist. Wer sie verwechselt, bekommt falsche Positionen
  ohne Fehlermeldung.
- Tilt antwortet mit `returnDelayUnits = 3`, damit sich die Antworten auf dem
  geteilten Bus nicht überlagern.
- Getriebespiel Pan: MapDiscovery fährt deshalb immer von der gleichen Seite an
  (`panApproach`, 5° Rückzug). Das lässt sich übernehmen, wenn es genau sein muss.

## Dateien

| Datei | Zweck |
|---|---|
| `EPF1Config.h` | Pins, Servo-IDs, Origins, Tilt-Typ, Betriebsart. Gemeinsam für Test und Firmware |
| `EPF1_HwTest/` | Inbetriebnahme ohne WLAN: Servos, Servo-IDs, Pixel, I2C über den USB-Monitor |
| `EPF1_6_3_0/` | Bus-Firmware (Gerät #23): Kern + `modeScan.ino` (Rundumscan → VPS `/localize`) mit Entfernungsmesser `rangerTOF.ino` (TOFSense-F, `EPF1_MODE_LIDAR`) oder `rangerX40.ino` (X40-Laser 40 m, `EPF1_MODE_X40`, Library smily77/Laser_RangeFinder_X40, Versorgung 2–3,3 V!) |
| `EPF1_Stell/` | CYD28 mit Drehknopf (COM3): Turm drehen, „NULL HIER“ (2× tippen) setzt die Pan-0, „AUF 0“, „SCAN“. UDP 4330, findet EPF1 per Broadcast |
| `Schema_EPF1.pdf` | Schaltplan (gespiegelter Scan) |

## Erste Inbetriebnahme

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 EPF1/EPF1_HwTest
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32S3 --port COMx EPF1/EPF1_HwTest
```

Monitor auf 115200 Baud. Beim Start fährt der Test **nichts** an. Er pingt nur
die Servos und meldet ihre Ist-Position. Danach mit `?` die Befehle anzeigen.
Reihenfolge:

1. `p`: beide Servos antworten? Fehlt einer, ID prüfen (Pan 2, Tilt 4).
2. `f`: Drehmoment aus, Turm von Hand drehen, mit `p` die Richtung prüfen.
3. `0`: auf Origin fahren, dann `a 30`, `t 10` usw.
4. `L/l`, `S/s`, `X/x`, `w r g b o`: einzelne Ausgänge prüfen.
5. `i`: I2C-Scan, zeigt angeschlossene Turm-Erweiterungen.

## Bus-Firmware EPF1_6_3_0

- **Gerät #23 `EPF1`**, Typ `Turret`, DHCP, `deviceCount` 24. Der LaserMarker (#15)
  bleibt ein eigenes Gerät und wird vom Turm nur gespeist.
- Beim Start fährt der Turm **auf 0** (Pan-0 aus NVS, Default 2048 Ticks; Tilt 0).
- **TOFSense-F** am Slave-Port, beim Start automatisch gesucht: UART 921600 (beide
  Richtungen), dann I2C 0x08..0x17 (beide Richtungen). Dieses Modul: I2C 0x08.
  Gültig = **Status 1** (empirisch: alle Raummessungen 0,5–2,7 m hatten Status 1),
  Stärke > 0, 100..15000 mm.
- **Pose:** 30 s nach dem Boot (und per Aktion „Kalibrieren“) Rundumscan Pan
  −170..+170° in 1°-Schritten (~40 s), je Schritt Median aus 3 neuen Messungen, Bin = Ist-Winkel.
  Der Scan geht wie beim LidarC1 an den VPS-Localizer (`:8080/localize`), Übernahme
  nur bei conf=HOCH und inlier ≥ 0,6. Der Rohscan steht im VPS-Debug-Fenster
  (`EPF1 scan bin:mm ...`).
- Kommandos an #23: `cmdCalibrate` 18 = Scan, `cmdClearPose` 19, `cmdRichtung` 1 =
  Pan auf info/10 Grad, `cmdAdjustAngle` 3 = neue Pan-0 in Ticks (gespeichert).
- Seriell 115200: `c` Scan, `d` TOF lesen, `s` TOF suchen, `p` Servos, `b` Servo-IDs,
  `a <grad>`, `t <grad>`, `0`, `z <ticks>`, `f/F`.
- **Achtung Quality-Gate:** Drinnen hat der Localizer zwei Wohnzimmer-Scans mit
  conf=HOCH und inlier 0,65 bewertet, die Firmware hat diese falschen Posen übernommen.
  Die Gartenkarte ist offenbar zu gutmütig für kurze Scans. Ergebnis draussen gegen
  den bekannten Standort prüfen.

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 EPF1/EPF1_6_3_0
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32S3 --port COMx EPF1/EPF1_6_3_0
```

Geräte mit alter Firmware (`deviceCount` 23) verwerfen Nachrichten von #23. Das schadet
nicht, solange sie von EPF1 nichts brauchen. Neu geflasht sind Manager und VPS.

## Offen

1. Tilt-Servo: am Bus antwortet nur ID 2 (Pan), kein Servo mit ID 4.
2. Pan-0 am realen Turm anpassen (`cmdAdjustAngle` / seriell `z`).
3. Endlagen Pan/Tilt messen und in `EPF1Config.h` eintragen.
4. Reicht der TOFSense-F-Scan (ein Strahl, 1°) für eine sichere Pose?
5. Aufgabe im Netz nach der Pose (Laser/Licht nachführen, Lidar-Detektion).
