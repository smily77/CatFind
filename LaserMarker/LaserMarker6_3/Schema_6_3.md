# Schema LaserMarker6_3 (aus hwDef.h + Cirquit/Scannen.pdf)

Board: **ESP32-C3 Super Mini**. Handgezeichnetes Original:
`../Cirquit/Scannen.pdf`.

```
                         ┌────────────────────────────┐
        USB-C 5V ───────►│ 5V        ESP32-C3 Super    │
        GND      ───────►│ GND       Mini (.182)       │
                         │                            │
                         │ GPIO3  ── mainLaser ───────┼──► IN  ┌──────────────────┐
                         │                            │       │ TTL-Laser-Modul  │◄─5V
                         │                            │       │ (K1)             │  GND
                         │                            │       └──────────────────┘
                         │ GPIO21 ── subLaser ──[440Ω]┼──► G   ┌──────────────────┐
                         │                            │       │ BS170 MOSFET     │
                         │                            │       │  D ── Laserdiode │◄─5V
                         │                            │       │  S ── GND        │
                         │                            │       └──────────────────┘
                         │ GPIO7  ── Aux ─────────────┼──► Aux-Ausgang (Stecker)
                         │                            │
                         │ GPIO10 ── DIN ──[100Ω]─────┼──► 1x WS2812 NeoPixel ◄─5V/GND
                         │                            │
                         │ GPIO0  ── SDA ─┐           │
                         │ GPIO1  ── SCL ─┴── I2C ────┼──► Stecker (ungenutzt)
                         │ GPIO20 ── TxD ─┐           │
                         │ GPIO4  ── RxD ─┴── UART ───┼──► Stecker (ungenutzt)
                         └────────────────────────────┘
```

## Pinbelegung

| GPIO | Signal (hwDef) | Richtung | Gegenstelle | Bemerkung |
|---:|---|---|---|---|
| 3 | mainLaser | OUT | TTL-Laser-Modul (IN) | HIGH = Laser an |
| 21 | subLaser | OUT | BS170-Gate über 440 Ω | HIGH = Laserdiode an |
| 7 | Aux | OUT | Aux-Stecker | HIGH = an, frei verwendbar |
| 10 | pixelPin | OUT | WS2812 DIN über 100 Ω | 1 Pixel, RGB |
| 0 | SDA | — | I2C-Stecker | reserviert, ungenutzt |
| 1 | SCL | — | I2C-Stecker | reserviert, ungenutzt |
| 20 | TxD | — | UART-Stecker | reserviert, ungenutzt |
| 4 | RxD | — | UART-Stecker | reserviert, ungenutzt |

## Hinweise

- Laser/Module hängen an **5 V** mit eigener Treiberstufe (TTL-Modul bzw.
  BS170-MOSFET); der ESP32-GPIO liefert nur das Steuersignal. Gemeinsamer GND.
- Der **subLaser** wird über den BS170 geschaltet (Gate-Widerstand 440 Ω);
  Laserdiode mit passendem Vorwiderstand/Konstantstromquelle betreiben.
- WS2812: 100 Ω in der Datenleitung; bei langer Leitung zusätzlich Pegelwandler
  und Stütz-Elko erwägen (3,3-V-Datensignal an 5-V-Pixel).
- GPIO20/21 sind beim C3 die UART0-Pins; da `Serial` über USB-CDC läuft, sind
  sie hier als GPIO frei (mainLaser/subLaser). Beim Boardwechsel prüfen.
- API/Steuerung: siehe `../API_LaserMarker6_3.md`.
