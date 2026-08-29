# KIVisionCatLocator — Phase 2: Kamera-Test und Webserver

Weboberfläche auf dem Pi `kivision` (192.168.0.186), mit der der Montageort der
Kamera gesucht wird: Livebild im Browser, alle wichtigen Kamera-Parameter
verstellbar, Schnappschüsse, und ein zuschaltbarer Coral-Test, der direkt zeigt,
ob die KI von diesem Standort aus überhaupt etwas erkennt.

**Aufruf: <http://192.168.0.186:8080/>** (auch vom Handy im Heim-WLAN).

Phase 2 ist bewusst eigenständig: keine Bus-Anbindung, kein VPS, keine
Homographie — das kommt in Phase 3/4.

## Dateien

| Datei | Zweck |
|-------|-------|
| `kivision_web.py` | Flask-Server: MJPEG-Stream, Kamera-Controls, Schnappschüsse, Coral-Test |
| `templates/index.html` | Weboberfläche (eine Seite, kein Build, handytauglich) |
| `kivision-web.service` | systemd-Dienst (Autostart) |

Auf dem Pi liegt alles unter `~/kivision/web/`, Schnappschüsse unter
`~/kivision/snapshots/` (max. 200, danach werden die ältesten gelöscht),
gespeicherte Einstellungen in `~/kivision/web_config.json`.

## Bedienung

**Anzeige**
- **Drehen 0/90/180/270°** — nur die Browser-Anzeige. Der ISP der Pi-Kamera kann
  kein 90°-Drehen; fürs Suchen des Montageorts (Kamera hochkant, weil der Rasen
  länger als breit ist) reicht das. Ab Phase 5 wird im Bild selbst gerechnet,
  nicht gedreht.
- **Spiegeln ⇄ / ⇅** — echte Kamera-Transformation (Stream startet kurz neu).
- **Raster** — Drittel-Linien und Mittelkreuz zum Ausrichten.
- **Vollbild** — Stage im Vollbild, praktisch auf dem Handy.

**Stream** — Auflösung (640×480 bis 2592×1944), Bildrate, JPEG-Qualität, dazu
zwei Voreinstellungen: **Flüssig** (640×480/60/25 fps) und **Detail**
(1280×960/65/12 fps). In der Kopfzeile steht laufend, wieviel der Stream gerade
braucht (**Mbit/s**, gelb ab 12, rot ab 20) und die gemessene **Verzögerung**
vom Sensor bis zur Anwendung. Siehe „Verzögerung" unten.

**Kamera** — **Farbprofil** (siehe „Farbstich" unten), Belichtungsautomatik
an/aus, feste Belichtungszeit und Gain (für den Nachttest), EV-Korrektur,
Weißabgleich — automatisch oder von Hand über **Rot-/Blau-Verstärkung**
(`ColourGains`, nur bei abgeschalteter Automatik) —, Helligkeit, Kontrast,
Sättigung, Schärfe, Rauschunterdrückung. Regler, die gerade wirkungslos sind
(z.B. Belichtungszeit bei aktiver Automatik), werden ausgegraut.
Kopfzeile zeigt laufend Verzögerung, Belichtungszeit, Gain, **WB** (die gerade
wirksamen Rot-/Blau-Gains), **Lux**, **Schärfe (FocusFoM)** und CPU-Temperatur
— FocusFoM hilft beim Scharfstellen des Objektivs: je höher, desto schärfer.

**Schnappschuss** — in Stream-Auflösung (sofort) oder **Voll 5 MP**
(2592×1944; der Stream pausiert dafür rund 2 s). Galerie unten, Bilder einzeln
löschbar.

**KI-Test (Coral)** — Erkennung an/aus, Modellauswahl aus `~/kivision/models/`,
Mindest-Sicherheit, Takt, und **Ausschnitte** (1, 2, 3, 2×2, 3×2 mit 15 %
Überlappung) — genau das Kachelverfahren aus dem Konzept: eine entfernte Katze
bleibt in einer Kachel größer als im ganzen, auf 300×300 geschrumpften Bild.
„nur Katzen zeigen" ausschalten, dann werden alle COCO-Klassen angezeigt — so
kann man sich zum Testen selbst ins Bild stellen (`person`).

Gemessen am Schreibtisch: 110 ms je Durchlauf über das ganze Bild, 143 ms mit
2 Kacheln (inkl. Bildabholung und Skalierung), CPU 40 °C, kein Throttling.

Der grüne Rahmen sitzt seit 2026-08-29 auf dem erkannten Objekt: `get_objects`
rechnet intern `Eingangsbreite / image_scale_x`, der übergebene Maßstab war
also gerade verkehrt herum und quetschte jeden Rahmen in eine Bildecke. Die
Beschriftung sitzt jetzt als Schild am Rahmen und klappt am Bildrand nach
innen, statt außerhalb des sichtbaren Bereichs zu landen.

## Verzögerung im Livebild (gemessen 2026-08-27)

Das WLAN des Pi ist der Engpass, nicht der Pi. Gemessen am Schreibtisch:
2,4 GHz (Kanal 6), −60 dBm, ausgehandelte 19,5 Mbit/s, **real nutzbar rund
2–3 Mbit/s**. Der Stream mit 1280×960/85/10 fps erzeugt **14,9 Mbit/s** — das
Fünffache. Die Bilder stauten sich im TCP-Sendepuffer (Linux puffert per
Autotuning bis ~2,5 MB, das sind über ein Dutzend Bilder), das Livebild lief
rund **2 s** nach und wurde nach jedem Neustart nur kurz besser.

Drei Gegenmaßnahmen im Server:
1. **Sendepuffer auf 96 kB begrenzt** (`SO_SNDBUF` auf dem Listener, wird
   vererbt). Der Schreibvorgang blockiert dadurch früh, der Stream überspringt
   Bilder statt sie zu stapeln — der Rückstand ist gedeckelt.
2. **Bildnummern statt Warten auf das nächste Bild** (`StreamingOutput.wait`
   bekommt die zuletzt gesendete Nummer): liegt schon ein neueres Bild bereit,
   geht es sofort raus, statt bis zum nächsten Encoder-Bild zu warten.
3. **Encoder läuft nur bei Zuschauern** (5 s Nachlauf) — vorher lief er auch
   ohne offene Seite und kostete dauerhaft 23 % CPU.
   Dazu `buffer_count` 4 → 3.

Ergebnis über WLAN, unverändert 1280×960/85:

| | vorher | nachher |
|---|---|---|
| angekommene Bilder | 0,8 /s | 4,4 /s |
| Rückstand | ~2 s, wachsend | 0,5 s, stabil |

Im **Sparmodus** (640×480/50) sind es 2,1 Mbit/s, 8,5 Bilder/s und **0,00 s
Rückstand** — also echtes Livebild.

Zur Kopfzeile: **Rückstand messbar** über den Header `X-Seq` je Bild im
Vergleich zu `frames` aus `/api/state` — das braucht keine synchronen Uhren.

### Nachgemessen (2026-08-29): es liegt *nicht* am WLAN

Die Restverzögerung ließ sich auf drei Stellen aufteilen und einzeln messen —
das Ergebnis widerlegt die WLAN-These von oben:

| Was | Wie gemessen | Ergebnis |
|---|---|---|
| WLAN-Kapazität | 20-MB-Datei vom Pi geladen | **29 Mbit/s** (3,6 MB/s), nicht 2–3 |
| Ping zum Pi | 20 Pakete | 4 ms im Mittel, 0 % Verlust |
| Sensor → Anwendung | `SensorTimestamp` gegen `monotonic` | 64 ms bei 8 fps, **27 ms bei 25 fps** |
| Encoder → Client | `X-Seq` gegen `frames` | konstant **1 Bild** — über WLAN *und* über localhost identisch |

Weil localhost und WLAN dieselben Werte liefern, kostet das Netz praktisch
nichts. Die verbleibende Verzögerung ist eine **Kette aus ganzen Bildern**:
Sensor, Encoder-Rückstand und der Browser puffern je rund ein Bild. Bei 8 fps
ist ein Bild 125 ms — die Bildrate war also selbst die Hauptursache.

Deshalb ist **die Bildrate hochzudrehen das wirksamste Mittel**, nicht sie zu
senken: 640×480/60 bei 25 fps braucht 5,5 Mbit/s (19 % der Leitung), die
Sensor-Verzögerung fällt von 64 auf 27 ms und der Rückstand von 125 auf 40 ms.
Zur Kontrolle mit der früher als unmöglich notierten Einstellung geprüft:
1280×960/85/10 fps läuft heute mit vollen 9,9 fps, 10,1 Mbit/s und stabil
einem Bild Rückstand.

Die Maßnahmen 1.–3. von oben bleiben trotzdem richtig: sie sorgen dafür, dass
bei knapper Leitung *Bilder übersprungen* statt gestapelt werden. Sie sind das
Sicherheitsnetz, nicht die Bremse. Was der Browser selbst puffert (`<img>` mit
MJPEG, rund ein Bild), lässt sich nur mit einem anderen Transportweg
(WebSocket + Canvas oder WebRTC) beseitigen — lohnt sich hier nicht.

Falls es am endgültigen Montageort doch klemmt: die SSID gibt es auch auf
**5 GHz** (am Schreibtisch nur −78 dBm, am Montageort evtl. besser), sonst
Repeater oder LAN-Kabel.

## Farbstich: das Modul ist eine NoIR-Kamera (gemessen 2026-08-29)

Der Magenta-Stich bei Tageslicht ist kein Weißabgleich-Fehler, sondern ein
fehlender IR-Sperrfilter. Nachweis über die Kanalmittelwerte eines
Schnappschusses derselben Szene:

| Farbprofil | R/G | B/G | Gains, die der Weißabgleich setzt |
|---|---|---|---|
| `ov5647.json` (Standard) | **1,94** | 1,13 | R 1,68 / B 1,18, CT 6492 K |
| `ov5647_noir.json` | **1,01** | 0,99 | R 0,89 / B 1,27, CT 4500 K |

Entscheidend ist die **rote Verstärkung unter 1,0**: das Rot kommt schon zu
stark aus dem Sensor, weil Infrarot vor allem auf die roten Pixel fällt. Die
Standard-Tuning-Datei hält den Weißabgleich auf der Farbtemperatur-Kurve und
*darf* diese Korrektur gar nicht fahren; die NoIR-Datei lässt ihm die Freiheit
— und das Bild ist damit neutral.

Umschaltbar in der Oberfläche unter **Kamera → Farbprofil** (die Kamera wird
dafür neu geöffnet, Tuning wird nur beim Öffnen gelesen). Steht jetzt auf
`noir`. Für die endgültige Kamera gilt: hat sie einen IR-Sperrfilter, gehört
das Profil auf `normal`/`auto`. Ohne Filter ist der Stich der Preis für
Nachtsicht — dann bleibt `noir` richtig, und für den Nachtbetrieb mit
IR-Strahler ist das ohnehin die gewünschte Bauform.

## Dienst

```bash
sudo systemctl status kivision-web      # Zustand
sudo systemctl restart kivision-web     # nach Code-Änderung
journalctl -u kivision-web -f           # Log
```

Der Dienst ist `enabled`, startet also nach jedem Stromausfall von selbst.

## Neu ausrollen

```bash
scp kivision_web.py pi@192.168.0.186:~/kivision/web/
scp templates/index.html pi@192.168.0.186:~/kivision/web/templates/
ssh pi@192.168.0.186 sudo systemctl restart kivision-web
```

## Beobachtungen vom ersten Test (2026-08-27)

- Das Modul zeigt einen deutlichen **Magenta-Stich**, auch bei Tageslicht.
  Am 2026-08-29 als fehlender IR-Sperrfilter nachgewiesen und mit der
  NoIR-Tuning-Datei behoben — siehe „Farbstich" oben.
- Starke **Tonnenverzeichnung** (Weitwinkel) — wie im Konzept erwartet. Vor der
  Homographie in Phase 4 muss die Verzeichnung korrigiert werden.
