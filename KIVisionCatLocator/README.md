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
zwei Voreinstellungen: **Sparmodus** (640×480/50/8 fps) und **Detail**
(1280×960/55/3 fps). In der Kopfzeile steht laufend, wieviel der Stream gerade
braucht (**Mbit/s**, gelb ab 3,5, rot ab 5). Siehe „Verzögerung" unten.

**Kamera** — Belichtungsautomatik an/aus, feste Belichtungszeit und Gain
(für den Nachttest), EV-Korrektur, Weißabgleich, Helligkeit, Kontrast,
Sättigung, Schärfe, Rauschunterdrückung. Regler, die gerade wirkungslos sind
(z.B. Belichtungszeit bei aktiver Automatik), werden ausgegraut.
Kopfzeile zeigt laufend Belichtungszeit, Gain, **Lux**, **Schärfe (FocusFoM)**
und CPU-Temperatur — FocusFoM hilft beim Scharfstellen des Objektivs: je höher,
desto schärfer.

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
Rückstand** — also echtes Livebild. Das ist die Einstellung fürs Herumlaufen
mit dem Handy; für Detailfragen lieber einen Schnappschuss machen, als den
Stream hochzudrehen.

Zur Kopfzeile: **Rückstand messbar** über den Header `X-Seq` je Bild im
Vergleich zu `frames` aus `/api/state` — das braucht keine synchronen Uhren.

Falls es am endgültigen Montageort weiter klemmt: die SSID gibt es auch auf
**5 GHz** (am Schreibtisch nur −78 dBm, am Montageort evtl. besser), sonst
Repeater oder LAN-Kabel.

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

- Das Modul zeigt einen deutlichen **Magenta-Stich** bei Kunstlicht — typisch,
  wenn kein IR-Sperrfilter im Strahlengang steht (IR-CUT in Nachtstellung bzw.
  NoIR-Variante). Für den Tagbetrieb muss der IR-CUT-Filter einschwenken, sonst
  stimmen die Farben nicht und COCO erkennt schlechter.
- Starke **Tonnenverzeichnung** (Weitwinkel) — wie im Konzept erwartet. Vor der
  Homographie in Phase 4 muss die Verzeichnung korrigiert werden.
