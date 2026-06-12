# Schema Manager6_3_0 (aus hwDef.h abgeleitet)

ESP32-DevBoard mit WS2811-LED-Ring (24 Pixel).

```
                        ┌──────────────────────────┐
        USB 5V ────────►│ 5V/VIN      ESP32        │
        GND    ────────►│ GND      (Manager, .180) │
                        │                          │
                        │ GPIO16 ──────────────────┼────► DIN ┐
                        └──────────────────────────┘          │
                                                              │
                       ┌──────────────────────────────────────┴───┐
        5V ───────────►│ +5V      LED-Ring WS2811, 24 Pixel       │
        GND ──────────►│ GND      Farbfolge GRB, Helligkeit 5/255 │
                       └──────────────────────────────────────────┘
```

## Pinbelegung

| GPIO | Signal | Richtung | Gegenstelle | Bemerkung |
|---:|---|---|---|---|
| 16 | pixelPin (WS2811-Daten) | OUT | DIN des LED-Rings | FastLED, GRB, 24 Pixel |

## Hinweise

- WS2811/WS2812-Ringe laufen mit 5 V; das 3,3-V-Datensignal des ESP32
  funktioniert meist direkt, sauber ist ein Pegelwandler (74AHCT125) und
  ein 330-Ω-Widerstand in der Datenleitung + 1000 µF Elko am Ring.
- `maxPix 230 > pixelNum 24` ist der "alle Pixel"-Trick der gemeinsamen
  Prozeduren — Achtung auf den setPixel(maxPix)-Punkt im Review
  (REVIEW_xCom6_3.md Nr. 4), bevor OTA produktiv genutzt wird.
- Sonst keine externe Beschaltung; WLAN/OTA/UDP laufen über das Funkmodul.
