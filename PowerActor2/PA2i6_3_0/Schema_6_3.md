# Schema PA2i6_3_0 — PowerActor (aus hwDef.h abgeleitet)

ESP32-DevBoard mit SMS_STS-Servobus (Drehachse), LP40B-Laser-Distanzmesser,
Wasserventil, Ziellaser und Schaltausgängen.

```
                            ┌──────────────────────────────┐
        USB/5V ────────────►│ 5V/VIN          ESP32        │
        GND    ────────────►│ GND            (PA2i, .181)  │
                            │                              │
   Servobus (Serial1,       │ GPIO18 (S_RXD) ◄──┐          │
   1 MBaud, half-duplex     │ GPIO19 (S_TXD) ───┤          │
   über Busadapter)         │                   │          │
                            │ GPIO16 (S2_RX) ◄──┼──┐       │
   LP40B (Serial2,          │ GPIO17 (S2_TX) ───┼──┤       │
   115200 Baud)             │                   │  │       │
                            │ GPIO27 ── valve ──┼──┼──┐    │
                            │ GPIO25 ── laserPwr┼──┼──┼─┐  │
                            │ GPIO5  ── intLed  │  │  │ │  │
                            │ GPIO26 ── LD06Pwr │  │  │ │  │ (reserviert, LOW)
                            │ GPIO23 ── hlkPwr  │  │  │ │  │ (reserviert)
                            └───────────────────┼──┼──┼─┼──┘
                                                │  │  │ │
        ┌───────────────────────────┐           │  │  │ │
  6-12V►│ Servo-Busadapter / Treiber│◄──────────┘  │  │ │
        │ (TTL half-duplex)         │              │  │ │
        │   DATA ──► SMS_STS Servo  │              │  │ │
        │            ID 1, 0..4096  │              │  │ │
        │            (Drehachse     │              │  │ │
        │             Wasserwerfer) │              │  │ │
        └───────────────────────────┘              │  │ │
        ┌───────────────────────────┐              │  │ │
   5V ─►│ LP40B Laser-Distanzmesser │◄─────────────┘  │ │
        │ RX ◄── GPIO17             │                 │ │
        │ TX ──► GPIO16             │                 │ │
        └───────────────────────────┘                 │ │
        ┌───────────────────────────┐                 │ │
  12V ─►│ MOSFET/Relais ──► Magnet- │◄────────────────┘ │
        │ ventil (Wasser)           │                   │
        └───────────────────────────┘                   │
        ┌───────────────────────────┐                   │
   5V ─►│ Treiber ──► Ziellaser     │◄──────────────────┘
        └───────────────────────────┘
```

## Pinbelegung

| GPIO | Signal (hwDef) | Richtung | Gegenstelle | Bemerkung |
|---:|---|---|---|---|
| 18 | S_RXD | IN | Servobus-Adapter | Serial1, 1 000 000 Baud |
| 19 | S_TXD | OUT | Servobus-Adapter | SMS_STS-Protokoll (SCServo-Lib) |
| 16 | S2_RX | IN | LP40B TX | Serial2, 115 200 Baud |
| 17 | S2_TX | OUT | LP40B RX | Einzelmessung auf Anfrage (MODE_SINGLE) |
| 27 | valve | OUT | Ventiltreiber | Wasser marsch (im Code noch nicht geschaltet — TODO, s. Review) |
| 25 | laserPwr | OUT | Ziellaser | Einschalt-Blink in initServo |
| 5 | intLed | OUT | interne LED | im Code derzeit ungenutzt |
| 26 | LD06Pwr | OUT | — | reserviert ("not in use"), wird LOW gesetzt |
| 23 | hlkPwr | OUT | — | reserviert ("not in use"), kein definierter Pegel (s. Review) |

## Mechanik / Servo

| Konstante | Wert | Bedeutung |
|---|---:|---|
| servoId | 1 | Bus-ID des SMS_STS |
| Mittelstellung | 2048 | = geradeaus (+y), Boot fährt dorthin |
| leftStopp | 760 | mechanischer Anschlag links (Software-Klemmung in servoGoTo) |
| rightStopp | 3400 | mechanischer Anschlag rechts |
| maxAngle | 60° | nutzbarer Halbwinkel → Limits-Default 2048 ± 682 |
| maxDist | 8000 mm | maximale Zieldistanz |
| initDeadZone | 500 mm | minimale Zieldistanz |

## Datenfluss

```
catObserved (UDP) ──► Servo auf angle drehen ──► LP40B-Distanz messen
                                                   │
                  Distanz ≈ radius (±150 mm)? ─────┤
                  readyToFire && Limits ok? ───────┴──► valve auf → catHit (UDP)
                                                        (Logik in 6_3 noch auskommentiert)
```

## Hinweise

- Der SMS_STS-Bus ist **half-duplex TTL**: TX und RX des ESP32 gehen auf
  einen Busadapter (z.B. Waveshare Bus-Servo-Adapter), der daraus die eine
  DATA-Leitung des Servos macht. Servo-Versorgung (6–12 V je nach Typ)
  getrennt vom ESP32 führen, GND gemeinsam.
- Magnetventil nie direkt an den GPIO: MOSFET/Relais mit Freilaufdiode,
  eigene 12-V-Versorgung, gemeinsamer GND.
- Serial1 wird in initHw **und** initServo initialisiert (redundant,
  s. REVIEW_6_3.md) — beim Verdrahten irrelevant, beide nutzen 18/19.
- GPIO16/17 sind auf manchen ESP32-Modulen (WROVER mit PSRAM) intern belegt —
  beim Boardwechsel prüfen.
