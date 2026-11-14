# EPF1 – Drehturm-Plattform fürs CatFinder-Netz

**Stand 2026-11-14: vorbereitet, noch nicht angeschlossen, noch nicht programmiert.**

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

> **Vor dem Anschliessen prüfen:** Im MapDiscovery-Schema (Seite 5, „Turn Base
> Compact“) sind die Farben anders belegt: GR = +12 V, BR = Servo, OR = SCL,
> GE = SDA. Liegt +12 V auf SDA/SCL oder an der Servo-Datenleitung des C3, ist er
> sofort kaputt.

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

## Offen (mit dem User klären, bevor die Bus-Firmware entsteht)

1. **Bus-Identität:** Eigene Geräte-ID in `xComDef6_3.h`? Heute gilt
   `deviceCount 23`. Eine neue ID verlangt deviceCount 24, und dann müssen alle
   Geräte neu geflasht werden, die den Absender prüfen. Ältere Firmware verwirft
   IDs ≥ deviceCount. Oder ersetzt EPF1 den LaserMarker (#15, .182), also dieselbe
   C3-Platine?
2. **Feste IP:** neu (z. B. .189), oder .182 übernehmen?
3. **Aufgabe im Netz:** Was macht EPF1 zuerst? Zum Beispiel Licht/Laser auf
   catTracked nachführen wie die PowerActors, Lidar-Scans oder Markieren für die
   Kalibrierung. Welche Elemente kommen zuerst auf den Turm?
4. **Welt-Pose:** gleiches Vorgehen wie bei den anderen Geräten (Pose-Kommandos
   24–27, Manuelle Pose im VPS)?
5. **Kabelfarben** gegen den realen Aufbau prüfen (siehe Warnung oben).
6. Endlagen Pan/Tilt am realen Turm messen und in `EPF1Config.h` eintragen.
