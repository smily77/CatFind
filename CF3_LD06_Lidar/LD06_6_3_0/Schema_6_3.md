# Schema LD06_6_3_0 (aus hwDef.h abgeleitet)

ESP32-DevBoard mit LD06-Lidar, WS2811-LED-Ring (29 Pixel), Laser und
Schalt-Ausgängen.

```
                          ┌────────────────────────────┐
        USB/5V ──────────►│ 5V/VIN        ESP32        │
        GND    ──────────►│ GND          (LD06)        │
                          │                            │
                          │ GPIO32 ◄─── TX  ────────┐  │
                          │ GPIO26 ───► PWM (opt.)──┤  │
                          │ GPIO27 ───► Power-      │  │
                          │             Schalter ─┐ │  │
                          └───────────────────────┼─┼──┘
                                                  │ │
              ┌───────────────┐   geschaltete 5V  │ │
   5V ───────►│ MOSFET/Relais ├───────────────────┼─┼──► +5V ┐
              │ (lidarPwr)    │                   │ │        │
              └───────▲───────┘                   │ │  ┌─────┴──────────────┐
                      └───────────────────────────┘ │  │  LD06 Lidar        │
                                                    │  │  TX  ─── 230400 Bd │
                                                    └──┤  PWM (Motor, opt.) │
                                            GND ──────►│  GND               │
                                                       └────────────────────┘

                          ┌────────────────────────────┐
                          │ GPIO23 ───► DIN  WS2811-Ring (29 Px, GRB, 5V)
                          │ GPIO4  ───► Laser (Ziel-/Statuslaser)
                          │ GPIO18 ───► ext. Relais
                          │ GPIO22 ───► ext1 (frei)
                          │ GPIO21 ───► ext2 (Servo, reserviert)
                          └────────────────────────────┘
```

## Pinbelegung

| GPIO | Signal (hwDef) | Richtung | Gegenstelle | Bemerkung |
|---:|---|---|---|---|
| 32 | dataInPin | IN | LD06 TX | Serial2 RX, 230400 Baud, 8N1 (LD06 sendet nur) |
| 26 | pwmPin | OUT | LD06 PWM | Motordrehzahl; im 6_3-Code **nicht angesteuert** (LD06 regelt selbst, wenn PWM offen/High) |
| 27 | lidarPwr | OUT | MOSFET/Relais | schaltet die Lidar-Versorgung (setup: HIGH) |
| 23 | pixelPin | OUT | WS2811 DIN | 29 Pixel, GRB |
| 4 | laser | OUT | Lasermodul | setup: LOW |
| 18 | extRelais | OUT | externes Relais | setup: LOW, im Code sonst ungenutzt |
| 22 | ext1 | OUT | frei | ungenutzt |
| 21 | ext2 | OUT | "Servo" | reserviert, ungenutzt |

## Datenfluss

```
LD06 ──UART──► ESP32: Sentence 0x54 0x2C, 47 Bytes, 12 Messpunkte
                      → Winkelfenster 250..330°, Distanz 500..6500 mm
                      → posPayload (angle in PA-Einheiten, x/y via toPaKart)
                      → UDP-Broadcast catObserved
```

## Hinweise

- Der LD06 braucht 5 V (ca. 300 mA Anlaufstrom des Motors) — deshalb die
  geschaltete Versorgung über `lidarPwr`; sein TX-Pegel ist 3,3 V und darf
  direkt an GPIO32.
- Es gibt bewusst keine TX-Leitung zum Lidar — der LD06 wird nicht
  konfiguriert, nur gelesen (`Serial2.begin(230400, SERIAL_8N1, dataInPin)`).
- Die PWM-Defines (pwmFreq 30 kHz, Kanal 0, 8 Bit) sind vorbereitet, aber im
  Code wird kein `ledcSetup/ledcAttach` aufgerufen — bei Bedarf für die
  Drehzahlregelung nachrüsten, sonst PWM-Leitung offen lassen.
- WS2811-Ring: 5 V, Datensignal siehe Hinweis im Manager-Schema
  (Pegelwandler empfohlen).
- Für laser/extRelais gilt: GPIO liefert max. ~12 mA — Module mit eigener
  Treiberstufe verwenden oder Transistor vorsehen.
