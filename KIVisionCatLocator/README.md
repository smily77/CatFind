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

**Stream** — Auflösung (640×480 bis 2592×1944), Bildrate, JPEG-Qualität.
Bei schwachem WLAN im Garten: 640×480 und Qualität 50–60, sonst ruckelt der
Stream (1280×960/85 sind rund 14 Mbit/s).

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
