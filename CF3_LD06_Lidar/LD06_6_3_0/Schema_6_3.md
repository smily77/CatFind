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

## Datenfluss (seit 2026-10-29, wie LidarC1)

```
LD06 ──UART 230400──► ESP32: Paket 0x54 0x2C, 47 Bytes, 12 Punkte, CRC-8 (Poly 0x4D) geprueft
   Winkel = 360 - LD06-Rohwinkel (gegen den Uhrzeigersinn), < 40 cm (Drehturm) und
   Signal < 150 verworfen
   Boot: 20 s Hintergrund lernen (Ring blau) -> 360-Bin-Scan an VPS :8080/localize (gelb)
         -> No-Shot-Karte vom Manager (violett) -> Pose per poseReport melden
   danach Ruhe (Motor aus, nur HB); ohne gueltige Pose Ring rot, alle 5 min neuer Versuch
   Zielerkennung (stgActive AN): Hintergrund neu lernen, je Umdrehung Schwerpunkt der
   Punkte >= 30 cm vor dem Hintergrund -> catObserved (lokal + Welt), nur in der No-Shot-Karte
   Aktion "Kalibrieren" (cmdCalibrate): Hintergrund + Pose neu
   Rohdaten-Stream fuer Step6_CalibTool: "LD06SCAN" an UDP 4320 -> Punkte an 4321
```

## Pixelring

- Pixel 0 zeigt vom Drehturm weg (LD06-Winkel ~270 Grad, Turm bei ~90 Grad), die Nummern
  laufen von oben gesehen gegen den Uhrzeigersinn (`RING_PIX0_DEG`/`RING_DIR` in hwDef.h,
  mit `RING_TEST` nachpruefbar).
- Phasen: blau = Hintergrund lernen, gelb = Pose per VPS, violett = No-Shot-Karte,
  rot = nicht lokalisiert, dunkel = bereit. HB: jeder 4. Pixel kurz gruen.
- Ziel: Pixel in Zielrichtung, rot = gemeldet (in der No-Shot-Karte), orange = gesehen, aber
  ausserhalb (nicht gemeldet).
- Eine Wand naeher als ~70 cm verdeckt Ziele: sie muessen 30 cm vor dem Hintergrund und
  mindestens 40 cm vom LD06 entfernt sein.

## Hinweise

- Der LD06 braucht 5 V (ca. 300 mA Anlaufstrom des Motors) — deshalb die
  geschaltete Versorgung über `lidarPwr`; sein TX-Pegel ist 3,3 V und darf
  direkt an GPIO32.
- Es gibt bewusst keine TX-Leitung zum Lidar — der LD06 wird nicht
  konfiguriert, nur gelesen.
- Laser (GPIO4) und ext. Relais (GPIO18) werden nur sicher AUS gehalten.
