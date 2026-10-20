# MapDiscovery – Garten vermessen mit Laser-Schwenkkopf

Mit diesen Programmen wurde der Garten vermessen: Rasenkarte, No-shot-Zonen und
Landmarken in `../Map/` (z. B. `karte_022.csv`). Ein Schwenkkopf mit
Laser-Entfernungsmesser misst Grenzpunkte. Ein M5Stack Tab5 bedient ihn,
rechnet die Punkte in Weltkoordinaten um und speichert sie als CSV.

Übernommen aus dem früheren Arduino-Projekt `Arduino/Tab5/`, und zwar nur
die zuletzt benutzte Fassung „WM“ (*With Marker*). Die Vorgänger
(`PanTiltController`, `PanTiltKarte`, `PanTiltHead` ohne Marker) und die
Test- und Referenz-Sketches (`PanTiltDemo`, `RotaryLine`, `RotaryLine8`,
`SDTest`) sind nicht mitgekommen.

## Inhalt

| Datei / Ordner | Board | Zweck |
|---|---|---|
| `PanTiltHead_WM/` | Seeed XIAO ESP32-S3 (oder ESP32 classic) | Schwenkkopf: fährt die Pan/Tilt-Servos, misst mit dem Laser und schaltet den CatFind-LaserMarker |
| `PanTiltKarte_WM/` | M5Stack Tab5 (ESP32-P4 + ESP32-C6) + Unit 8Encoder | Bedienung, Kalibrierung, Kartierung, Auto-Scan, CSV auf SD |
| `TurnBaseConfig.h` | – | Gemeinsame Pins, Servo-IDs, Ursprünge und Tilt-Servotyp. Wird vom Head als `../TurnBaseConfig.h` eingebunden und muss deshalb hier liegen bleiben. |
| `Schema.pdf` | – | Schaltplan des Aufbaus |

## Systemübersicht

```
 Tab5 (PanTiltKarte_WM)                 XIAO (PanTiltHead_WM)               LaserMarker (.182)
 Encoder + Touch + Karte   --UDP 4210-->  Servos + SEN0366-Laser  --UDP 23456-->  Hilfslaser/Pixel
                           <--UDP 4211--  (Antwort auf jedes Paket)
```

- Beide Geräte sind Stationen im Haus-WLAN. Die Zugangsdaten kommen aus
  `#include <Credentials.h>` (lokale Library `MyLGFXConfigs`) und liegen
  **nicht** im Repo.
- Der Tab5 sendet `ControlMsg` als UDP-Broadcast an Port 4210. Der Head
  antwortet mit `ResultMsg` direkt an die Absender-IP, Port 4211. Pairing ist
  nicht nötig.
- **Marker:** Der Tab5 setzt nur das Flag `markerOn`. Den CatFind-LaserMarker
  (ESP32-C3, feste IP `192.168.0.182`, Port 23456) steuert **ausschliesslich der
  Head**. Er schaltet MainLaser, Aux und Pixel (weiss) ein, damit der angepeilte
  Punkt gut sichtbar ist. Die 17-Byte-Kommandos baut der Head selbst zusammen,
  CatFind-Header braucht es dafür nicht. Das Protokoll steht in
  `../LaserMarker/API_LaserMarker6_3.md`.

## Protokoll (binär, packed, in beiden Sketches identisch)

```cpp
struct ControlMsg {      // Tab5 -> Head, 12 Bytes
    float   panDeg;      // -170..+170
    float   tiltDeg;     // -30..+30
    uint8_t laserOn;     // Entfernungsmesser-Laser
    uint8_t doMeasure;   // jetzt Einzelmessung
    uint8_t approach;    // 1 = Anti-Backlash-Anfahrt
    uint8_t markerOn;    // 1 = LaserMarker ein
};
struct ResultMsg {       // Head -> Tab5, 11 Bytes
    float   distanceM;     // 0 wenn ungültig
    float   panActualDeg;  // Ist-Winkel laut Servo-Rückmeldung
    uint8_t laserState;
    uint8_t valid;
    uint8_t markerState;
};
```

## Hardware

| Teil | Typ | Details |
|---|---|---|
| Pan | Feetech ST3020 (ST-Protokoll) | ID 2, Origin 2048, 4096 Ticks/360° |
| Tilt | Feetech SC15 (SC-Protokoll) | ID 4, Origin 680, 1024 Ticks/180°, Endanschläge im Servo-EEPROM |
| Servo-Bus | Half-Duplex-UART, 1 Mbit/s | TX über 1 kΩ auf die Datenleitung, RX direkt. ESP32-S3: TX=1, RX=2. ESP32: TX=16, RX=17 |
| Entfernungsmesser | DFRobot SEN0366 / GJD-01, 9600 Baud | ESP32-S3: RX=7, TX=8. ESP32: RX=26, TX=25 |
| Encoder | M5Stack Unit 8Encoder an Port A | SDA=53, SCL=54, `Wire.begin(53,54,100000)` von Hand |
| SD-Karte im Tab5 | über **SPI**, nicht SDMMC | SCK=43, MISO=39, MOSI=44, CS=42 |

Der Tilt-Servotyp lässt sich in `TurnBaseConfig.h` umstellen
(`TILT_SERVO_SC` / `TILT_SERVO_ST`). Origin und Umrechnung wechseln dann
automatisch mit.

## Bedienung PanTiltKarte_WM

**Encoder**

| Encoder | Funktion |
|---|---|
| 0 | Pan grob, 3° je Raste |
| 1 | Pan fein, 0,2° je Raste |
| 7 | Tilt, 0,5° je Raste |
| 6 | Punkt wählen im Edit-Modus (LED grün) |

Grenzen: Pan ±170°, Tilt ±30°.

**Ablauf (Zustände)**

1. **ORIGIN MESSEN** (`S_CALIB_O`): Den Laser auf den Ursprung O=(0,0) richten und messen.
2. **X-ACHSE MESSEN** (`S_CALIB_A`): Einen Punkt A auf der x-Achse messen. Daraus
   berechnet die Karte Position und Ausrichtung des Kopfs (`headX`, `headY`, `headPhi`).
3. **Kartieren** (`S_MAP`):
   - **PUNKT MESSEN** speichert einen Grenzpunkt mit dem *Ist*-Pan-Winkel
     (`panActualDeg`) in Weltkoordinaten. Die Punkte werden nach Pan-Winkel
     sortiert und als Polygon gezeichnet. Bei gleichem Winkel (±0,1°) ersetzt der
     neue Punkt den alten.
   - **AUTO SCAN** (`S_AUTO_SCAN`): fährt den Pan-Bereich zwischen dem kleinsten
     und grössten bisherigen Punkt in 2°-Schritten ab und misst bei jedem Schritt.
     Dafür braucht es mindestens 2 Punkte. **SCAN STOPPEN** bricht ab.
   - **EDIT** (`S_EDIT`): Mit Encoder 6 einen Punkt wählen, **PUNKT LOESCHEN** löscht ihn.
   - **LETZTEN LOESCHEN** macht die letzte Änderung rückgängig.
   - **KARTE SPEICHERN** schreibt `/karte_NNN.csv` (fortlaufende Nummer) auf die
     SD-Karte. Dafür braucht es mindestens 3 Punkte.
   - **ANFAHRT PRUEFEN** fährt die aktuelle Position mit Anti-Backlash-Anfahrt neu
     an und misst, **ohne** zu speichern. So lässt sich die Wiederholgenauigkeit prüfen.
4. Die Knöpfe **LASER EIN/AUS** und **MARKER EIN/AUS** gibt es in allen Zuständen.

**CSV-Format** (Meter, Weltkoordinaten bezogen auf O und die x-Achse durch A):

```
# PanTiltKarte  Kopf=(x,y)  phi=<rad>
# X,Y
1.2345,0.6789
...
```

## Bekannte Eigenheiten

- **Getriebespiel Pan:** Mit `approach=1` fährt der Head erst
  `BACKLASH_BACKOFF_DEG` (5°) hinter das Ziel und dann immer von unten an.
  Danach liest er die Ist-Position. Der Auto-Scan nutzt die Anfahrt nur beim
  ersten Punkt, weil er danach monoton aufsteigend läuft. Manuelles Messen
  positioniert direkt.
- **Ist-Winkel statt Sollwinkel:** Der Head liest nach jeder Bewegung
  `readPresentPosition()`. Fällt die Bus-Abfrage aus, nimmt er den Sollwinkel.
- **Laser-Messung:** `singleMeasurement()` blockiert bis zu 1 s. Der Head
  versucht es bis zu 3-mal und misst nur bei `doMeasure=1`.
- **SC15 `returnDelayUnits` = 3:** Sonst kollidieren die Antworten auf dem
  geteilten Bus mit denen des ST3020.
- **ST- und SC-Servos sind verschiedene Klassen** (`FeetechST3020` und
  `FeetechSC15`, Little- bzw. Big-Endian). Wer sie verwechselt, bekommt falsche
  Positionen ohne Fehlermeldung.
- **Tab5-SD über SPI:** SDMMC teilt sich die Peripherie mit dem WLAN-SDIO zum
  ESP32-C6. `SD_MMC.begin()` nach dem WLAN-Start lässt das WLAN abstürzen.
- **Tab5 ohne ESP-NOW** (RISC-V-Core), deshalb läuft alles über WLAN-UDP.
- **Unit 8Encoder Kaltstart:** Bootet der STM32 in den Bootloader (I2C 0x54),
  holt ihn das Byte `0x77` an 0x54 zurück in die App (0x41).

## Libraries

M5Unified (≥ 0.2.14), M5GFX (≥ 0.2.20), M5UNIT_8Encoder, FeetechBusServos
(smily77/SerialServo), LaserRangefinder_SEN0366 (smily77, 1.1.0), lokale
`MyLGFXConfigs` mit `Credentials.h`.

## Kompilieren und Hochladen

```bash
# Head (XIAO ESP32-S3) – zuerst
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 MapDiscovery/PanTiltHead_WM
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32S3 --port COM3 MapDiscovery/PanTiltHead_WM

# Bedienung (Tab5)
arduino-cli compile --fqbn m5stack:esp32:m5stack_tab5 MapDiscovery/PanTiltKarte_WM
arduino-cli upload  --fqbn m5stack:esp32:m5stack_tab5 --port COM5 MapDiscovery/PanTiltKarte_WM
```

Die Ports (COM3/COM5) stammen aus der damaligen Einrichtung und können abweichen.
