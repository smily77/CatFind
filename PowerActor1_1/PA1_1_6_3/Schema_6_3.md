# Schema PA1_1_6_3 (aus Adjustment-hardware.h + Infrastruktur-Tests + Schematics/Scannen.pdf)

Älterer PowerActor (PA1b): ESP32, Drehturm über Schrittmotor. Stepper-Treiber
**A4988**, angesteuert über Port-Expander **PCF8574** (I2C). Winkelgeber
**AS5600** am selben I2C-Bus. Riemenantrieb Pulley 20 : Zahnkranz 130 (= 6.5).

```
                        ┌─────────────────────────────────────┐
        5V/USB ────────►│ 5V                ESP32 (.183)       │
        GND    ────────►│ GND                                 │
                        │                                     │
                        │ GPIO25 ── DIN ─────────────────────►│ 2x WS2812 (RGB)
                        │ GPIO12 ── water         (Relais) ──►│ Wasserventil = Feuer
                        │ GPIO13 ── extPower      (Relais) ──►│ externe Versorgung
                        │ GPIO14 ── wirelessPower (Relais) ──►│ Strom Turm-Sensoren (an@Boot)
                        │ GPIO18 ── laser ───────────────────►│ Ziellaser
                        │ GPIO33 ◄─ taster   (INPUT_PULLUP)   │
                        │ GPIO26 ◄─ reedLeft (INPUT_PULLUP) ──┤ Reed links  (am Turm)
                        │ GPIO32 ◄─ reedRight(INPUT_PULLUP) ──┤ Reed rechts (am Turm)
                        │                                     │
                        │ GPIO21 ── SDA ─┐                    │
                        │ GPIO19 ── SCL ─┴─ I2C 100kHz ───────┼─┬─────────────┬───────────┐
                        └─────────────────────────────────────┘ │             │           │
                                                                 ▼             ▼           │
                                                        ┌───────────────┐ ┌─────────┐      │
                                                        │ PCF8574 @0x20 │ │ AS5600  │      │
                                                        │  P0 DIR       │ │ Winkel- │◄ Magnet
                                                        │  P1 STEP      │ │ geber   │ (Stepper-
                                                        │  P2 SLP       │ └─────────┘  Welle)
                                                        │  P3 RST       │
                                                        │  P4 MS3       │   ┌──────────────┐
                                                        │  P5 MS2  ─────┼──►│ A4988        │──► Schrittmotor
                                                        │  P6 MS1       │   │ Treiber      │    │ Pulley 20T
                                                        │  P7 EN        │   └──────────────┘    ▼
                                                        └───────────────┘                   Zahnkranz 130T
                                                                                            = Drehturm
```

## Pinbelegung ESP32

| GPIO | Signal (hwDef) | Richtung | Bemerkung |
|---:|---|---|---|
| 25 | pixelPin | OUT | 2x WS2812 Status (FastLED, RGB) |
| 12 | water | OUT | Relais Wasserventil = **Feuer-Aktor** |
| 13 | extPower | OUT | Relais externe Versorgung |
| 14 | wirelessPower | OUT | Relais Strom Turm-Sensoren (**HIGH bei Boot**) |
| 18 | laser | OUT | Ziellaser (an während Feuer) |
| 33 | taster | IN (Pullup) | lokaler Taster |
| 26 | reedLeft | IN (Pullup) | Reed-Kontakt links, am Drehturm |
| 32 | reedRight | IN (Pullup) | Reed-Kontakt rechts, am Drehturm |
| 21 | pinSDA | I/O | I2C SDA (PCF8574 + AS5600) |
| 19 | pinSCL | OUT | I2C SCL |

## PCF8574 @ 0x20 → A4988

| PCF8574-Pin | A4988 | Funktion |
|---:|---|---|
| P0 | DIR | Drehrichtung (dirLeft=HIGH, dirRight=LOW) |
| P1 | STEP | Schritt-Puls |
| P2 | SLP | Sleep (HIGH = aktiv) |
| P3 | RST | Reset (HIGH = aktiv) |
| P4 | MS3 | Mikroschritt |
| P5 | MS2 | Mikroschritt |
| P6 | MS1 | Mikroschritt |
| P7 | EN | Enable (LOW = aktiv) |

Mikroschritt-Default: 1/16. MS1..MS3 siehe `setMicrostep()`.

## Mechanik / Winkel

| Größe | Wert |
|---|---|
| Pulley Stepper | 20 Zähne |
| Zahnkranz Turm | 130 Zähne |
| Übersetzung | 130/20 = 6.5 |
| Vollschritte/Umdrehung Motor | 200 (1.8°-Motor, `stepsPerRev`) |
| Mikroschritte/Turm-Umdrehung | 200·16·130/20 = **20800** |
| PA-Welt | 0..4096 (2048 = geradeaus), 1 PA-Einheit ≈ 5.08 Mikroschritte |

## Hinweise

- **AS5600 sitzt aktuell auf der Stepper-Welle** (relativ), geplant ist später
  die Montage direkt am Drehturm (zum Erkennen übersprungener Schritte/Zähne).
  Die Position wird daher derzeit über Schrittzählung + Reed-Referenz geführt;
  der AS5600 wird nur ausgelesen/geloggt.
- **Reed-Kontakte am Turm** dienen der Referenzfahrt (Homing). Die genaue
  Zuordnung Nullpunkt/Bereich ist noch zu bestätigen (siehe Kommentar in
  `hwProc.ino` → `homeTurret()`).
- **Relais** schalten Lasten mit eigener Versorgung — Treiberstufe/Freilauf
  vorsehen; gemeinsamer GND.
- **Partition:** Wegen WiFi+OTA+FastLED+Stepper-Libs in der Arduino IDE das
  Schema **„Minimal SPIFFS (1.9MB APP with OTA)"** wählen (das Default
  „1.2MB APP" reicht nicht). Verifiziert: 70 % von 1.9 MB.
- Benötigte Bibliotheken: FastLED, **PCF8574 library (Renzo Mischianti)**,
  AS5600 (Rob Tillaart), Streaming, sowie die ESP32-Bordmittel
  (WiFi/AsyncUDP/ArduinoOTA/Wire/Preferences).
