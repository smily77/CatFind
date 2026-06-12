# Schema Button6_3_0 (aus hwDef.h abgeleitet)

Fertiggerät, keine externe Verdrahtung nötig. Zwei Varianten per define:

## Variante 1: M5 Atom Matrix (Standard, ohne `#define Atom3S`)

Alle Verbindungen sind geräteintern:

```
              ┌─────────────────────────────────────┐
   USB-C 5V ─►│            M5 Atom Matrix           │
              │                                     │
              │  GPIO27 ──► 5x5 WS2812-Matrix       │  pixelPin 27, 25 Pixel,
              │             (LED-Anzeige)           │  GRB, Helligkeit 5/255
              │                                     │
              │  GPIO39 ◄── Taste (Frontfläche)     │  über M5.Btn eingelesen
              │                                     │
              │  (WLAN-Antenne intern)              │
              └─────────────────────────────────────┘
```

| GPIO | Signal | Richtung | Bemerkung |
|---:|---|---|---|
| 27 | pixelPin (WS2812-Matrix) | OUT | intern fest verdrahtet (M5Atom-Library) |
| 39 | Fronttaste | IN | intern, via `M5.Btn` |

## Variante 2: M5 Atom S3 (`#define Atom3S`)

```
              ┌─────────────────────────────────────┐
   USB-C 5V ─►│             M5 Atom S3              │
              │                                     │
              │  0,85"-LCD ◄── M5.Lcd               │  Vollflächige Farbanzeige
              │  Taste (Display drücken) ── M5.BtnA │
              └─────────────────────────────────────┘
```

Keine GPIO-Definitionen nötig — Display und Taste laufen über M5Unified.

## Anzeige-Logik (beide Varianten)

| Farbe | Bedeutung |
|---|---|
| Weiss | Zustand unbekannt (gerade Kommando gesendet / noch kein PA2-HB) |
| Rot | PA2 meldet readyToFire = scharf |
| Grün | PA2 meldet gesichert |

## Hinweise

- Stromversorgung ausschliesslich über USB-C (5 V); für den Gartenbetrieb
  eignet sich eine USB-Powerbank.
- Wird auf Atom S3 umgestellt, `#define Atom3S` im Sketchkopf aktivieren —
  hwDef.h wählt dann M5Unified statt M5Atom/FastLED.
