# EPF1 – Drehturm-Plattform fürs CatFinder-Netz

**Stand 2026-11-14: Bus-Firmware `EPF1_6_3_0` geschrieben (Geräte-ID 23, DHCP), noch nicht auf dem Turm.**

EPF1 ist ein neues Gerät im CatFinder-Netz: ein **Drehturm** (Pan + Tilt), auf dem
verschiedene Elemente montiert werden können, z. B. Lidar, Lichter oder Laser.

- **Mechanik und Servos** sind die gleichen wie beim Schwenkkopf von MapDiscovery
  (`../MapDiscovery/`, `TurnBaseConfig.h`). Beim EPF1 ist aber **auch der Tilt ein
  ST-Servo** (`#define TILT_SERVO_ST`).
- Die Umschaltung `TILT_SERVO_ST` / `TILT_SERVO_SC` bleibt erhalten. So kann der
  MapDiscovery-Kopf mit SC15-Tilt später als **EPF1b** laufen. Origin und
  Umrechnung wechseln automatisch mit (`EPF1Config.h`).
- **Elektronik** in der Bauart des LaserMarkers: ESP32-C3 Super Mini, gleiche Pins
  wie `../LaserMarker/LaserMarker6_3/hwDef.h`. Neu ist, dass die UART-Pins 20/4 als
  Half-Duplex-Servo-Bus dienen.

Schema: `Schema_EPF1.pdf`. Der Scan ist **gespiegelt** (Schrift spiegelverkehrt).

## Pinbelegung (ESP32-C3 Super Mini)

| GPIO | Funktion | Bemerkung |
|---|---|---|
| 20 | Servo-Bus TX | über **1 kΩ** auf die Datenleitung (Kabel GE) |
| 4 | Servo-Bus RX | direkt auf die Datenleitung |
| 3 | TTL-Laser-Modul (IN) | HIGH = an (wie LaserMarker `mainLaser`) |
| 21 | BS170-Gate → Laserdiode an 5 V | HIGH = an (`subLaser`). GPIO21 ist U0TXD: das ROM-Bootlog lässt die Diode beim Booten kurz flackern |
| 7 | AUX-Stecker (gegen GND) | HIGH = an |
| 10 | WS2812, 5 mm (IN/VCC/GND/OUT) | NEO_RGB wie LaserMarker |
| 0 / 1 | I2C SDA / SCL | Kabel BR / GR, für Erweiterungen am Turm |
| 5V / G | Versorgung | Kabel RT (+5 V) / BL (GND) |
| 2, 8, 9 | frei | Strapping-Pins, nicht beschalten |

## Kabel (laut Schema_EPF1)

| Farbe | Signal |
|---|---|
| BL | GND |
| RT | +5 V |
| BR | SDA |
| GR | SCL |
| GE | Servo-Datenleitung |
| OR | +12 V (Servo-Speisung, auf der C3-Platine nicht benutzt) |

Die Farben gelten auch für den MapDiscovery-Turm. Die abweichenden Farben auf Seite 5
des MapDiscovery-Schemas sind die internen Adern, nicht das Turmkabel.

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
| `EPF1Config.h` | Pins, Servo-IDs, Origins, Tilt-Typ-Schalter. Gemeinsam für Test und Firmware |
| `EPF1_HwTest/` | Inbetriebnahme ohne WLAN: Servos, Laser, AUX, Pixel, I2C über den USB-Monitor |
| `EPF1_6_3_0/` | Bus-Firmware (Gerät #23): Turm auf 0, TOFSense-F, Rundumscan → VPS `/localize` → Welt-Pose |
| `Schema_EPF1.pdf` | Schaltplan (gespiegelter Scan) |

## Erste Inbetriebnahme

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc EPF1/EPF1_HwTest
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --port COMx EPF1/EPF1_HwTest
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
- **TOFSense-F an I2C** (Modul vorher per NAssistant auf IIC stellen), gesucht wird
  0x08..0x17. Frame ab Register 0x20: system_time, dis (mm), dis_status,
  signal_strength. Gültig = Status 0, Stärke > 0, 100..15000 mm.
- **Pose:** 30 s nach dem Boot (und per Aktion „Kalibrieren“) Rundumscan Pan
  −170..+170° in 1°-Schritten, je Schritt Median aus 2 neuen Messungen, Bin = Ist-Winkel.
  Der Scan geht wie beim LidarC1 an den VPS-Localizer (`:8080/localize`), Übernahme
  nur bei conf=HOCH und inlier ≥ 0,6. Der Rohscan steht im VPS-Debug-Fenster
  (`EPF1 scan bin:mm ...`).
- Kommandos an #23: `cmdCalibrate` 18 = Scan, `cmdClearPose` 19, `cmdRichtung` 1 =
  Pan auf info/10 Grad, `cmdAdjustAngle` 3 = neue Pan-0 in Ticks (gespeichert).
- Seriell 115200: `c` Scan, `d` TOF lesen, `p` Servos, `a <grad>`, `0`, `z <ticks>`, `f/F`.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc EPF1/EPF1_6_3_0
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --port COMx EPF1/EPF1_6_3_0
```

Geräte mit alter Firmware (`deviceCount` 23) verwerfen Nachrichten von #23. Das schadet
nicht, solange sie von EPF1 nichts brauchen. Neu geflasht sind Manager und VPS.

## Offen

1. Pan-0 am realen Turm anpassen (`cmdAdjustAngle` / seriell `z`).
2. Endlagen Pan/Tilt messen und in `EPF1Config.h` eintragen.
3. Reicht der TOFSense-F-Scan (ein Strahl, 1°) für eine sichere Pose?
4. Aufgabe im Netz nach der Pose (Laser/Licht nachführen, Lidar-Detektion).
