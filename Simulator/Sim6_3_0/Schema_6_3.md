# Schema Sim6_3_0 — Simulator (aus hwDef.h abgeleitet)

Fertiggerät M5 Cardputer (ESP32-S3), keine externe Verdrahtung. Die
Pindefinitionen in hwDef.h beschreiben den **internen** SD-Karten-Slot.

```
              ┌──────────────────────────────────────────────┐
   USB-C 5V ─►│               M5 Cardputer (ESP32-S3)        │
              │                                              │
              │  ┌─────────────┐        ┌──────────────────┐ │
              │  │ 1,14" LCD   │◄───────│ M5Canvas (Menü,  │ │
              │  │ (ST7789)    │        │ Statusausgaben)  │ │
              │  └─────────────┘        └──────────────────┘ │
              │  ┌─────────────┐                             │
              │  │ Tastatur    │  '1' Record an/aus          │
              │  │ (Matrix)    │  '2' Play Back               │
              │  └─────────────┘  '3' Datei löschen           │
              │                                              │
              │  microSD-Slot (SPI, 25 MHz):                 │
              │    GPIO40 ── SCK                             │
              │    GPIO39 ── MISO                            │
              │    GPIO14 ── MOSI                            │
              │    GPIO12 ── CS                              │
              └──────────────────────────────────────────────┘
```

## Pinbelegung (intern, SPI zur microSD)

| GPIO | Signal (hwDef) | Bemerkung |
|---:|---|---|
| 40 | SD_SPI_SCK_PIN | SPI-Takt |
| 39 | SD_SPI_MISO_PIN | Karte → ESP32 |
| 14 | SD_SPI_MOSI_PIN | ESP32 → Karte |
| 12 | SD_SPI_CS_PIN | Chip Select |

## Datenfluss

```
Aufnahme:  UDP-Multicast ──► catObserved ──► /data.bin (Header + Payload,
                                             variable Recordlänge)
Wiedergabe: /data.bin ──► broadcastRawMsg() ──► UDP-Multicast
            (Original-Sender und -Zeitstempel bleiben erhalten,
             20 ms Abstand pro Record)
```

## Hinweise

- microSD bis 16 GB FAT32 einlegen; ohne Karte bleibt das Programm im
  Setup stehen ("Card failed, or not present").
- Stromversorgung über USB-C oder den internen Akku des Cardputers —
  damit ist der Simulator das einzige mobil einsetzbare Gerät im System
  (praktisch zum Aufzeichnen direkt am Rasen).
- WLAN-Zugangsdaten kommen wie bei allen 6_3-Geräten aus `Credentials.h`.
