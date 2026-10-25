# Schema PA2i6_3_0 — PowerActor2 (Stand nach dem Umbau 2026-10)

ESP32-DevBoard mit Feetech-Servobus (Drehachse), LP40B-Laser-Distanzmesser,
IR-Positionskamera und 5 Relais (Ventil, Ziellaser, Spot, Speisung LD06-Lidar,
Speisung Radar Compact Dome). Alle Relais schalten **aktiv HIGH** (bestätigt).

Stand der Hardware nach dem Umbau; erprobt mit den Test-Sketches in
`PowerActor2/Development/AfterSerialRebuild/` (Step1 … Step5). Die Firmware
`PA2i6_3_0` selbst kennt Spot und IR-Kamera noch nicht.

```
                            ┌──────────────────────────────┐
        USB/5V ────────────►│ 5V/VIN          ESP32        │
        GND    ────────────►│ GND            (PA2, .181)   │
                            │                              │
   Servobus (Serial1,       │ GPIO18 (S_RXD) ◄──┬──────────┼──┐
   1 MBaud, half-duplex     │ GPIO19 (S_TXD) ─[1kΩ]┘       │  │
   über 1-kΩ-Widerstand)    │                              │  │
                            │ GPIO16 (S2_RX) ◄─────────────┼──┼──┐
   LP40B (Serial2,          │ GPIO17 (S2_TX) ──────────────┼──┼──┤
   115200 Baud)             │                              │  │  │
                            │ GPIO21 (SDA) ◄──────────────►┼──┼──┼──┐
   IR-Kamera (I2C, 0x58)    │ GPIO22 (SCL) ───────────────►┼──┼──┼──┤
                            │                              │  │  │  │
                            │ GPIO27 ── valve    (Relais)  │  │  │  │
                            │ GPIO25 ── laserPwr (Relais)  │  │  │  │
                            │ GPIO32 ── Spot     (Relais)  │  │  │  │
                            │ GPIO26 ── LD06Pwr  (Relais)  │  │  │  │
                            │ GPIO23 ── hlkPwr   (Relais)  │  │  │  │
                            │ GPIO5  ── intLed             │  │  │  │
                            └──────────────────────────────┘  │  │  │
                                                              │  │  │
        ┌───────────────────────────┐                         │  │  │
  6-12V►│ Feetech ST3020 Servo      │◄── DATA ────────────────┘  │  │
        │ ID 1, 0..4095 (±180°)     │                            │  │
        │ (Drehachse Wasserwerfer)  │                            │  │
        └───────────────────────────┘                            │  │
        ┌───────────────────────────┐                            │  │
   5V ─►│ LP40B Laser-Distanzmesser │◄───────────────────────────┘  │
        │ RX ◄── GPIO17             │                               │
        │ TX ──► GPIO16             │                               │
        └───────────────────────────┘                               │
        ┌───────────────────────────┐                               │
   5V ─►│ DFRobot SEN0158           │◄──────────────────────────────┘
        │ IR Positioning Camera     │
        │ SDA ◄─► GPIO21            │
        │ SCL ◄── GPIO22            │
        └───────────────────────────┘
        ┌───────────────────────────┐
  12V ─►│ Relais ──► Magnetventil   │◄── GPIO27
        │            (Wasser)       │
        ├───────────────────────────┤
        │ Relais ──► Ziellaser      │◄── GPIO25
        ├───────────────────────────┤
        │ Relais ──► Spot           │◄── GPIO32
        ├───────────────────────────┤
        │ Relais ──► Speisung LD06  │◄── GPIO26
        │            Lidar          │
        ├───────────────────────────┤
        │ Relais ──► Speisung Radar │◄── GPIO23
        │            Compact Dome   │
        └───────────────────────────┘
```

## Pinbelegung

| GPIO | Signal | Richtung | Gegenstelle | Bemerkung |
|---:|---|---|---|---|
| 18 | S_RXD | IN | Servo DATA (direkt) | Serial1, 1 000 000 Baud |
| 19 | S_TXD | OUT | Servo DATA über 1 kΩ | Feetech-Protokoll (FeetechBusServos-Lib), kein Busadapter mehr |
| 16 | S2_RX | IN | LP40B TX | Serial2, 115 200 Baud |
| 17 | S2_TX | OUT | LP40B RX | Betrieb: Einzelmessung auf Anfrage (Laser nicht dauernd an); Test-Sketches: Dauermessung ~50–100 Hz; Protokoll SEN0586 |
| 21 | SDA | IN/OUT | SEN0158 SDA | I2C, Adresse 0x58 |
| 22 | SCL | OUT | SEN0158 SCL | I2C, 100 kHz |
| 27 | valve | OUT | Relais → Magnetventil | aktiv HIGH |
| 25 | laserPwr | OUT | Relais → Ziellaser | aktiv HIGH; Test-Sketches: automatisch EIN bei Lidar < 0,5 m |
| 32 | Spot | OUT | Relais → Spot | aktiv HIGH, neu |
| 26 | LD06Pwr | OUT | Relais → Speisung LD06-Lidar | aktiv HIGH |
| 23 | hlkPwr | OUT | Relais → Speisung Radar Compact Dome | aktiv HIGH |
| 5 | intLed | OUT | interne LED | ungenutzt |

## IR-Positionskamera (SEN0158)

Wiimote-Kamera, erkennt bis zu 4 IR-Punkte; gedacht als Hilfe bei der
Pose-Findung, falls es anders nicht geht.

| Eigenschaft | Wert |
|---|---|
| Auflösung | x 0..1023, y 0..767, Punktgröße 0..15 |
| Kein Punkt | x = 1023 |
| Einrichtung | Register 0x30=01, 0x30=08, 0x06=90, 0x08=C0, 0x1A=40, 0x33=33 (erweiterter Modus) |
| Abfrage | 0x36 schreiben, 16 Byte lesen; je Punkt 3 Byte: x lo, y lo, `YYXXSSSS` |

## Mechanik / Servo

| Konstante | Wert | Bedeutung |
|---|---:|---|
| servoId | 1 | Bus-ID des ST3020 |
| Mittelstellung | 2048 | = geradeaus (+y), 0° |
| Bereich | 0..4095 | ±180°, derzeit ohne mechanische Anschläge |
| maxAngle | 60° | nutzbarer Halbwinkel der Firmware PA2i6_3_0 → Limits-Default 2048 ± 682 |
| maxDist | 8000 mm | maximale Zieldistanz |
| initDeadZone | 500 mm | minimale Zieldistanz |

## Datenfluss (Firmware PA2i6_3_0)

```
catObserved (UDP) ──► Servo auf angle drehen ──► LP40B-Distanz messen
                                                   │
                  Distanz ≈ radius (±150 mm)? ─────┤
                  readyToFire && Limits ok? ───────┴──► valve auf → catHit (UDP)
                                                        (Logik in 6_3 noch auskommentiert)
```

## Hinweise

- Der Servobus ist **half-duplex TTL**: statt eines Busadapters gehen RX
  (GPIO18) direkt und TX (GPIO19) über 1 kΩ auf die eine DATA-Leitung des
  Servos. Servo-Versorgung (6–12 V) getrennt vom ESP32 führen, GND gemeinsam.
- Der LP40B misst im Betrieb nur auf Anfrage (Einzelmessung), damit sein
  Laser nicht dauernd läuft; die Dauermessung der Test-Sketches ist nur zum Testen.
- Magnetventil und Spot nie direkt an den GPIO: Relais/MOSFET mit
  Freilaufdiode, eigene Versorgung, gemeinsamer GND.
- Test-Sketch Step5 schaltet alle Relais AUS, wenn 3 s kein Display-Kommando
  kommt (Ventil bleibt nie offen hängen).
- GPIO21/22 sind die Standard-I2C-Pins des ESP32; bei langen Leitungen
  100 kHz beibehalten.
- GPIO16/17 sind auf manchen ESP32-Modulen (WROVER mit PSRAM) intern belegt —
  beim Boardwechsel prüfen.
