# LaserMarker ExtUsage — Standalone-I2C-Firmware

Dieser Ordner enthält ein eigenständiges Arduino-Programm, das die LaserMarker-6_3-Hardware als **I2C-Slave-Gerät** bereitstellt. Das Programm ist absichtlich unabhängig vom CatFind-Projekt: keine CatFind-Header, kein UDP, kein WiFi, kein OTA und keine CatFind-Records.

## Dateien

| Datei | Zweck |
|---|---|
| `LaserMarkerI2CSlave/LaserMarkerI2CSlave.ino` | Arduino-Sketch für den ESP32-C3 Super Mini |
| `README.md` | Diese vollständige I2C-API-Dokumentation |

## Hardware

| Funktion | ESP32-C3 GPIO | Wirkung |
|---|---:|---|
| I2C SDA | GPIO0 | I2C-Datenleitung zum externen Controller |
| I2C SCL | GPIO1 | I2C-Taktleitung zum externen Controller |
| mainLaser | GPIO3 | HIGH = TTL-Laser-Modul an |
| subLaser | GPIO21 | HIGH = MOSFET/Laserdiode an |
| Aux | GPIO7 | HIGH = Aux-Ausgang an |
| Pixel | GPIO10 | 1× WS2812 NeoPixel, externes Farbformat RGB |

> Wichtig: Der I2C-Bus benötigt passende Pull-up-Widerstände auf SDA und SCL. Viele Controller-Boards haben diese bereits integriert; sonst müssen sie extern ergänzt werden.

## Pixel-Anzeige und Prioritäten

Der NeoPixel hat zwei Aufgaben: Er kann direkt per Pixel-Befehl gesetzt werden und dient zusätzlich als Sicherheits-/Statusanzeige für aktive Laser, wenn kein Pixel-Farbwert aktiv ist.

Die Priorität ist:

1. **Pixel-Befehl:** Wenn per `cmd = 14` eine Farbe ungleich `0x000000` gesetzt wurde, zeigt der Pixel exakt diese Farbe. Laser-Indikatoren werden dann nicht eingeblendet.
2. **mainLaser-Indikator:** Wenn der Pixel per Befehl aus ist (`0x000000`) und `mainLaser` eingeschaltet ist, blinkt der Pixel schwach rot mit Wert `25`.
3. **subLaser-Indikator:** Wenn der Pixel per Befehl aus ist, `mainLaser` aus ist und `subLaser` eingeschaltet ist, leuchtet der Pixel voll weiß mit Wert `255`.
4. **Alles aus:** Wenn kein Pixel-Befehl aktiv ist und kein Laser eingeschaltet ist, bleibt der Pixel aus.

Bei gleichzeitig eingeschaltetem `mainLaser` und `subLaser` gewinnt der `mainLaser`-Indikator, solange kein aktiver Pixel-Befehl gesetzt ist.

## I2C-Grunddaten

| Eigenschaft | Wert |
|---|---|
| Rolle des LaserMarkers | I2C Slave / Target |
| Standard-Adresse | `0x2A` als 7-Bit-Adresse |
| Standard-Takt | 100 kHz |
| Schreibtelegramm | exakt 5 Bytes |
| Lesetelegramm | 16 Bytes Status |
| Byte-Reihenfolge | Little-Endian für alle Mehrbyte-Werte |

Die Adresse kann bei Bedarf im Sketch über `I2C_ADDRESS` geändert werden.

## Befehle schreiben

Der Master schreibt immer genau **5 Bytes** an die I2C-Adresse `0x2A`:

| Byte | Feld | Typ | Bedeutung |
|---:|---|---|---|
| 0 | `cmd` | `uint8_t` | Befehlsnummer |
| 1..4 | `info` | `int32_t`, Little-Endian | Parameter zum Befehl |

### Befehlsnummern

Die wichtigsten Befehlsnummern sind identisch mit der bisherigen UDP-API, damit vorhandene Steuerlogik leicht portiert werden kann.

| Funktion | `cmd` dez / hex | `info` | Wirkung |
|---|---:|---|---|
| mainLaser schalten | 11 / `0x0B` | `0` = aus, ungleich `0` = an | setzt GPIO3 |
| subLaser schalten | 12 / `0x0C` | `0` = aus, ungleich `0` = an | setzt GPIO21 |
| Aux schalten | 13 / `0x0D` | `0` = aus, ungleich `0` = an | setzt GPIO7 |
| Pixel-Farbe setzen | 14 / `0x0E` | `0x00RRGGBB` | setzt den WS2812-Pixel |
| Zustand abfragen | 15 / `0x0F` | egal | ändert keine Ausgänge; der Master liest danach den Status |
| Alles aus | 16 / `0x10` | egal | standalone-Erweiterung: mainLaser, subLaser, Aux und Pixel aus |

### Farbformat

Für `cmd = 14` ist `info` eine 24-Bit-Farbe im Format `0x00RRGGBB`:

- Rot: Bits 16..23
- Grün: Bits 8..15
- Blau: Bits 0..7

Beispiele:

| Farbe | `info` | 5-Byte-Schreibtelegramm |
|---|---:|---|
| Schwarz / Pixel aus | `0x000000` | `0E 00 00 00 00` |
| Rot | `0xFF0000` | `0E 00 00 FF 00` |
| Grün | `0x00FF00` | `0E 00 FF 00 00` |
| Blau | `0x0000FF` | `0E FF 00 00 00` |
| Orange | `0xFF8000` | `0E 00 80 FF 00` |

## Status lesen

Der Master kann jederzeit 16 Bytes von Adresse `0x2A` lesen. Der LaserMarker liefert immer den aktuellen Zustand:

| Offset | Feld | Typ | Bedeutung |
|---:|---|---|---|
| 0 | `version` | `uint8_t` | I2C-API-Version, aktuell `1` |
| 1 | `lastEvent` | `uint8_t` | letztes Ereignis, siehe Ereigniscodes |
| 2 | `lastError` | `uint8_t` | `0` oder letzter Fehlercode |
| 3 | `mainLaser` | `uint8_t` | `0` aus, `1` an |
| 4 | `subLaser` | `uint8_t` | `0` aus, `1` an |
| 5 | `aux` | `uint8_t` | `0` aus, `1` an |
| 6 | `r` | `uint8_t` | Pixel-Rot 0..255 |
| 7 | `g` | `uint8_t` | Pixel-Grün 0..255 |
| 8 | `b` | `uint8_t` | Pixel-Blau 0..255 |
| 9 | `lastCommand` | `uint8_t` | zuletzt akzeptierter oder fehlerhafter Befehl |
| 10..11 | reserviert | `uint8_t[2]` | aktuell `0`, für spätere Erweiterungen |
| 12..15 | `messageCounter` | `uint32_t`, Little-Endian | Summe aus erfolgreichen Befehlen und Fehlern seit Boot |

Die Statusfelder `r`, `g` und `b` enthalten den zuletzt per Pixel-Befehl gesetzten Sollwert. Bei `0,0,0` kann der tatsächlich sichtbare Pixel trotzdem temporär als Laser-Indikator blinken oder weiß leuchten, siehe „Pixel-Anzeige und Prioritäten“.

### Ereigniscodes

| Code | Name | Bedeutung |
|---:|---|---|
| 1 | `EVENT_BOOT` | Gerät wurde gestartet, noch kein Befehl verarbeitet |
| 2 | `EVENT_COMMAND_OK` | letzter Befehl wurde ausgeführt |
| 3 | `EVENT_BAD_LENGTH` | Master hat nicht exakt 5 Bytes geschrieben |
| 4 | `EVENT_UNKNOWN_COMMAND` | unbekannter `cmd`-Wert wurde ignoriert |

## Ablaufempfehlung für Master-Systeme

1. I2C-Bus initialisieren.
2. Optional 16 Statusbytes lesen und prüfen, ob `version == 1` ist.
3. Befehl als 5 Bytes schreiben.
4. Kurz warten, z. B. 2 bis 10 ms.
5. 16 Statusbytes lesen.
6. Prüfen, ob `lastEvent == 2`, `lastError == 0`, `lastCommand` dem gesendeten Befehl entspricht und die Zustandsbytes den erwarteten Wert enthalten.

## Arduino-Master-Beispiel

```cpp
#include <Wire.h>

static constexpr uint8_t MARKER_ADDR = 0x2A;
static constexpr uint8_t CMD_MAIN_LASER = 11;
static constexpr uint8_t CMD_PIXEL_COLOR = 14;

void writeCommand(uint8_t cmd, int32_t info) {
  Wire.beginTransmission(MARKER_ADDR);
  Wire.write(cmd);
  Wire.write((uint8_t)(info & 0xFF));
  Wire.write((uint8_t)((info >> 8) & 0xFF));
  Wire.write((uint8_t)((info >> 16) & 0xFF));
  Wire.write((uint8_t)((info >> 24) & 0xFF));
  Wire.endTransmission();
}

void readStatus(uint8_t status[16]) {
  Wire.requestFrom(MARKER_ADDR, (uint8_t)16);
  for (uint8_t i = 0; i < 16 && Wire.available(); i++) {
    status[i] = Wire.read();
  }
}

void setup() {
  Wire.begin();
  writeCommand(CMD_PIXEL_COLOR, 0x00FF8000); // Pixel orange
  delay(5);
  writeCommand(CMD_MAIN_LASER, 1);           // mainLaser an
}

void loop() {}
```

## Raspberry-Pi-/Linux-Beispiel mit Python

```python
from smbus2 import SMBus
import time

ADDR = 0x2A
CMD_MAIN_LASER = 11
CMD_PIXEL_COLOR = 14
CMD_ALL_OFF = 16

def command(bus, cmd, info=0):
    info &= 0xFFFFFFFF
    payload = [
        cmd,
        info & 0xFF,
        (info >> 8) & 0xFF,
        (info >> 16) & 0xFF,
        (info >> 24) & 0xFF,
    ]
    bus.write_i2c_block_data(ADDR, payload[0], payload[1:])

with SMBus(1) as bus:
    command(bus, CMD_PIXEL_COLOR, 0x00FF8000)
    time.sleep(0.01)
    command(bus, CMD_MAIN_LASER, 1)
    time.sleep(0.01)
    status = bus.read_i2c_block_data(ADDR, 0, 16)
    print(status)
    command(bus, CMD_ALL_OFF, 0)
```

Hinweis: Manche Linux-I2C-Bibliotheken senden ein erstes Byte als „Register“. Das passt hier gut, weil dieses Byte direkt als `cmd` interpretiert wird und die folgenden vier Bytes `info` sind.

## Sicherheit

- Nach Boot/Reset sind mainLaser, subLaser, Aux und Pixel aus.
- Es gibt bewusst kein automatisches Laser-Timeout, damit das Verhalten exakt steuerbar bleibt. Das Master-System muss Laser rechtzeitig wieder ausschalten.
- Für sichere Anwendungen sollte der Master regelmäßig den Status lesen und bei Kommunikationsfehlern ein `CMD_ALL_OFF` oder einzelne Aus-Kommandos senden.
- Laser können Augen gefährden. Während Tests sollten Laserleistung, Strahlrichtung und Einschaltdauer begrenzt werden.

## Build-Hinweise

Benötigte Arduino-Komponenten:

- ESP32-Boardpaket mit ESP32-C3-Unterstützung
- Bibliothek `Adafruit NeoPixel`

In der Arduino IDE den Ordner `LaserMarkerI2CSlave` öffnen und als Board einen ESP32-C3 auswählen.
