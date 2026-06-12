# Schema Radar6_3_0 — Dome-Familie (aus hwDef.h abgeleitet)

Drei Hardware-Varianten, Auswahl per define im Sketchkopf. Gemeinsames
Prinzip: ESP32 liest einen HLK-Radarsensor (Multi-Target, 256 000 Baud)
über Serial2 und meldet Targets per UDP.

## Variante DomeDevice (`#define DomeDevice`, ID = Dome)

```
                        ┌──────────────────────────┐
        USB/5V ────────►│ 5V/VIN      ESP32        │
        GND    ────────►│ GND        (Dome)        │
                        │                          │
                        │ GPIO33 ◄─── TX ────────┐ │   Serial2 RX, 256000 Bd
                        │ GPIO12 ───► RX ──────┐ │ │   Serial2 TX (Konfig.)
                        │ GPIO25 ───► Laser    │ │ │
                        │ GPIO32 ───► DIN ──┐  │ │ │
                        └───────────────────┼──┼─┼─┘
                                            │  │ │
            ┌───────────────────┐           │  │ │
       5V ─►│ HLK-Radar (LD2450)│◄──────────┼──┘ │
            │ TX ───────────────┼───────────┼────┘
            └───────────────────┘           │
            ┌───────────────────┐           │
       5V ─►│ 2x WS2811 (RGB)   │◄──────────┘   minPix 0 = Status/HB
            └───────────────────┘               maxPix 1 = Target aktiv
```

| GPIO | Signal | Richtung | Bemerkung |
|---:|---|---|---|
| 33 | radar / S2_RX | IN | HLK TX, 256 000 Baud |
| 12 | S2_TX | OUT | HLK RX (nur falls Sensor konfiguriert wird) |
| 32 | pixelPin | OUT | 2 Pixel WS2811, Farbfolge RGB |
| 25 | laser | OUT | definiert, im Code nicht geschaltet |

HB-Periode 5000 ms, deadZone 500 mm.

## Variante MiniDomeDevice (`#define MiniDomeDevice`, ID = MiniDome) — Standard

```
                        ┌──────────────────────────┐
        USB/5V ────────►│ 5V/VIN      ESP32        │
        GND    ────────►│ GND      (MiniDome)      │
                        │                          │
                        │ GPIO34 ◄─── TX ────────┐ │   nur Eingang! (34 = input-only)
                        │ GPIO12 ───► RX ──────┐ │ │
                        │ GPIO32 ───► DIN ──┐  │ │ │
                        └───────────────────┼──┼─┼─┘
                                            │  │ │
            ┌───────────────────┐           │  │ │
       5V ─►│ HLK-Radar         │◄──────────┼──┘ │
            │ TX ───────────────┼───────────┼────┘
            └───────────────────┘           │
            ┌───────────────────┐           │
       5V ─►│ 2x WS2811 (RGB)   │◄──────────┘
            └───────────────────┘
```

| GPIO | Signal | Richtung | Bemerkung |
|---:|---|---|---|
| 34 | radar / S2_RX | IN | input-only-Pin, ideal für reines Mitlesen |
| 12 | S2_TX | OUT | HLK RX |
| 32 | pixelPin | OUT | 2 Pixel WS2811, RGB |

HB-Periode **500 ms** (Debug-Wert? s. REVIEW_6_3.md Nr. 2), deadZone 500 mm.

## Variante CompactDomeDevice (`#define CompactDomeDevice`, ID = CompactDome)

M5-Format ("auf PA M5PicoDome") — kompakter Aufbau mit einer einzelnen LED:

```
                        ┌──────────────────────────┐
        USB-C 5V ──────►│        ESP32 (M5 Pico)   │
                        │                          │
                        │ GPIO19 ◄─── TX ────────┐ │
                        │ GPIO12 ───► RX ──────┐ │ │
                        │ GPIO27 ───► DIN ──┐  │ │ │  1x WS2811 GRB (interne LED)
                        └───────────────────┼──┼─┼─┘
                                            │  │ │
            ┌───────────────────┐           │  │ │
       5V ─►│ HLK-Radar         │◄──────────┼──┘ │
            │ TX ───────────────┼───────────┼────┘
            └───────────────────┘           │
                              (interne LED)─┘
```

| GPIO | Signal | Richtung | Bemerkung |
|---:|---|---|---|
| 19 | radar / S2_RX | IN | HLK TX |
| 12 | S2_TX | OUT | HLK RX |
| 27 | pixelPin | OUT | 1 Pixel GRB, Helligkeit 100/255 |

HB-Periode 5000 ms, deadZone **1000 mm** (grössere Totzone wegen Montage).

## Datenfluss (alle Varianten)

```
HLK ──UART──► ESP32: Frame AA FF 03 00 + 3 Targets à 8 Byte (30 Bytes)
                     → x/y dekodieren (Vorzeichen-Sonderformat), y spiegeln
                     → toPaPol() → radius/angle
                     → Targets mit l > deadZone:
                          posPayload → UDP-Broadcast catObserved
              HB: radarHbPayload (inkl. deadZoneDist) alle periodeForHB
```

## Hinweise

- Der HLK-Sensor (LD2450-Klasse) braucht 5 V, seine UART-Pegel sind 3,3 V —
  direkt anschliessbar.
- GPIO12 ist ein Boot-Strapping-Pin des ESP32: hängt der HLK-RX daran und
  zieht ihn beim Boot hoch, kann der ESP32 im falschen Flash-Modus starten —
  falls Bootprobleme auftreten, S2_TX auf einen anderen Pin legen (wird nur
  fürs Konfigurieren des Sensors gebraucht).
- LED-Bedeutung: minPix grün blitzt bei jedem HB, maxPix blau solange ein
  gültiges Target gemeldet wird.
