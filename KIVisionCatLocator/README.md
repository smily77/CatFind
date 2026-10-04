# KIVisionCatLocator — Kamera-Webserver (Phase 2) und Busgerät (Phase 3)

Weboberfläche auf dem Pi `kivision` (192.168.0.186), mit der der Montageort der
Kamera gesucht wird: Livebild im Browser, alle wichtigen Kamera-Parameter
verstellbar, Schnappschüsse, und ein zuschaltbarer Coral-Test, der direkt zeigt,
ob die KI von diesem Standort aus überhaupt etwas erkennt.

**Aufruf: <http://192.168.0.186:8080/>** (auch vom Handy im Heim-WLAN).

Seit Phase 3 ist derselbe Prozess zugleich **Gerät #20 `KIVision`** auf dem
CatFinder-Bus (siehe unten „Busgerät"). Homographie/Pose folgt in Phase 4.

## Dateien

| Datei | Zweck |
|-------|-------|
| `kivision_web.py` | Flask-Server: MJPEG-Stream, Kamera-Controls, Schnappschüsse, Coral-Test |
| `templates/index.html` | Weboberfläche (eine Seite, kein Build, handytauglich) |
| `kivision-web.service` | systemd-Dienst (Autostart) |
| `xcom.py` | xCom-6.3-Protokoll in Python, **zur Laufzeit aus `xComDef6_3.h` geparst** |
| `kivision_bus.py` | das Busgerät: HB, settingsReport, poseReport, Kommandos |
| `kivision_motion.py` | Bewegungserkennung (MOG2 + Spurverfolger) auf dem lores-Graubild |
| `xComDef6_3.h` | wird beim Ausrollen aus `Controller/Manager6_3_0/` daneben kopiert |

## Busgerät (Phase 3)

- **ID 20, Name `KIVision`, Typ `VisionLocator`**, IP .186 (Eintrag in `device[]` der
  `xComDef6_3.h`, `deviceCount` 21). Geräte mit alter Firmware (`deviceCount` 20)
  verwerfen Pakete von #20 — wer #20 kennen muss (Manager, CatIdent,
  Bedienungs-Display), braucht einen Neubau.
- **Protokoll ohne zweite Quelle:** `xcom.py` liest beim Start `#define`s,
  `constexpr`s und alle `__attribute__((packed))`-Structs aus `xComDef6_3.h`
  (auch eingebettete wie `hbPayload hb;`). Protokolländerung = Header neu kopieren.
- **Sendet:** HB alle 10 s (`hbPayload`), `settingsReport` (Start, auf Anfrage, nach
  jeder Änderung — auch wenn die Erkennung hier im Browser umgeschaltet wird),
  `poseReport` (bis Phase 4 `validWorldPose=0`), Debug-Text → VPS-Debugfenster.
- **Settings:** `stgCamAi` = Coral-Erkennung (dieselbe wie der Knopf „Erkennung";
  persistiert in `web_config.json`), `stgActive` = Ruhemodus (Erkennung und
  Trefferbilder aus, HB läuft; nicht persistiert — nach Neustart wieder aktiv).
- **Kommandos:** `cmdSetSetting`, `cmdReboot` (beendet den Prozess, systemd startet
  ihn in ~10 s neu — Kamera/Coral werden sauber freigegeben, der Pi bootet nicht).
- Kopfzeile der Weboberfläche: **Bus** `#20 ok` / `still` (seit 30 s nichts
  empfangen) / `Ruhemodus`; Tooltip mit Zählern und letztem Kommando.

Auf dem Pi liegt alles unter `~/kivision/web/`, Schnappschüsse unter
`~/kivision/snapshots/` (max. 200, danach werden die ältesten gelöscht),
Treffer-Bilder unter `~/kivision/hits/` (max. 60), Radar-Bilder unter
`~/kivision/radar/` (max. 150), Bewegungs-Bilder unter `~/kivision/motion/`
(max. 150), Radar↔Kamera-Paare in `~/kivision/pairs.jsonl`, gespeicherte Einstellungen
in `~/kivision/web_config.json`.

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

**Treffer-Bilder** — beim Justieren läuft niemand mit einer Katze durchs Bild,
also hält die Kamera selbst fest, was sie gesehen hat: bei jeder Erkennung
legt sie ein Bild in `~/kivision/hits/` ab, mit **eingezeichnetem Rahmen**,
Klasse, Sicherheit und einer Fußzeile aus Uhrzeit, Lux, Belichtungszeit und
Gain — daran sieht man hinterher, ob der Treffer aus der Tag- oder der
Infrarot-Lage stammt. Zwei Bremsen gegen das Überlaufen:

- **Abstand der Bilder** (10–300 s, voreingestellt 60 s). Er zählt ab der
  letzten *Aufnahme*, nicht ab dem letzten Treffer: eine Katze, die zehn
  Minuten im Bild sitzt, liefert zehn Bilder, nicht tausend. Die Oberfläche
  schreibt mit, wieviel Sperrzeit noch läuft.
- **max. 60 Bilder** auf der Karte, danach fällt das älteste heraus — bei einer
  Minute Abstand also mindestens eine Stunde Rückschau. Einzeln löschbar,
  dazu ein Knopf für „alle löschen".

**Treffer festhalten** hat einen *eigenen* Filter (`aus | nur Katzen | Tiere |
alles Erkannte`), unabhängig von „nur Katzen zeigen" im Livebild. Das ist nicht
Kosmetik: nachts meldet das COCO-Modell auf dem kontrastarmen Infrarotbild
dauerhaft ein bildfüllendes `horse` mit ~56 % (nachgemessen 2026-09-28). Wer
sich so etwas im Livebild ansehen will, soll sich davon nicht jede Minute die
Galerie zumüllen lassen.

**Katzenerkennung nachgeschärft (2026-09-30).** Bis dahin wurde keine einzige
echte Katze erkannt, dafür gab es zwei Fehlalarme mit *bildfüllendem* „cat“
(44/50 %, Dämmerung, IR). Ursache: Mit Ausschnitt 1 wird das 1640×1232-Bild
auf 300×300 geschrumpft, eine Katze auf dem Rasen hat dann nur noch 10–15 Pixel.
Das ist weit unter dem, was SSD-MobileNet findet. Seither gilt:
- **Ausschnitte 4×3** (Standard): Die Katze hat dann ~40 Pixel. Den Mäher findet das
  Modell damit sofort als `car`, vorher sah es ihn gar nicht. Kosten: ~0,4 s je
  Runde (Coral an USB 3, die Zeit geht in die Vorverarbeitung).
- **`max_area` 0,4**: Rahmen, die mehr als 40 % ihres Ausschnitts bedecken,
  werden verworfen. Das trifft genau die bildfüllenden Fehlalarme.
- **Treffer-Filter „Tiere“** (Standard): Er hält auch `dog/bear/sheep/horse/cow/
  teddy bear` fest, weil das Modell auf dem Graubild Katzen leicht verwechselt.
  So sammeln sich echte Katzenbilder, an denen man weiter abstimmen kann.

**Radar-Bilder** (seit 2026-10-02) — die Treffer-Bilder sagen nur, was das
Modell *gefunden* hat, nie, was es *verpasst* hat. Am 2026-10-02 lief die
Kamera, und die Radar-Tracks 353280 (11:14) und 353283 (15:16, von Hand als
Katze bewertet) ergaben kein einziges Bild, weil das Modell keine `cat` über
der Schwelle sah — ob die Katze überhaupt im Bild war, blieb offen. Deshalb
fotografiert der Pi jetzt zusätzlich *auf Zuruf vom Bus*: jede `catObserved`
(Radare, Lidar, CatCam) startet eine Episode mit bis zu 4 Bildern im Abstand
von 2 s (nur solange weiter gemeldet wird; 8 s Funkstille = neue Episode, eine
Dauermeldung wie der Mäher zählt nach 120 s als neue Episode). `catDetected`
vom CatIdent löst sofort ein Bild aus. Eingezeichnet wird alles, was das Modell
im selben Durchlauf ab 20 % sah — Katze grün, alles andere orange; die Zahl im
Dateinamen ist die beste *Katzen*-Sicherheit (0 = keine). Die Fußzeile nennt
Sender und Welt-Position. Ist die KI aus, wird das nackte Bild abgelegt; im
Ruhemodus gar nichts. Bilder in der eigenen Galerie „Radar-Bilder“.

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

### Schwache Clients (Handy) — Sendepuffer und Nagle

Auf dem Handy blieb mehr Verzögerung übrig als am PC. Zwei Stellen im Server
wirken genau dort, wo die Strecke schmal ist:

- **Sendepuffer auf ein knappes Bild verkleinert** (`SO_SNDBUF` 96 → 24 kB
  Wunsch, der Kernel verdoppelt intern auf 48 kB). Was im Puffer liegt, ist
  beim Ankommen schon alt; der Puffer muss nur den *Weg* füllen
  (Bandbreite × Laufzeit ≈ 15 kB bei 30 Mbit/s und 4 ms), nicht ganze Bilder
  stapeln. Bleibt er unter einer Bildgröße, blockiert das Schreiben früh, der
  Stream überspringt Bilder — und der Client bekommt das *neueste* statt eines
  alten aus der Schlange.
- **Nagle abgeschaltet** (`disable_nagle_algorithm`, setzt `TCP_NODELAY`).
  Sonst hält der Kernel das letzte Teilstück jedes Bildes zurück, bis die
  vorige Sendung bestätigt ist; zusammen mit der verzögerten Bestätigung des
  Empfängers kostet das bis zu 40 ms je Bild. Bei einem Strom aus fertigen
  Bildern bringt das Bündeln ohnehin nichts.

Gemessen mit einem Client, dessen **Empfangsfenster** klein ist (`SO_RCVBUF`
8 kB) — der bremst den Server per TCP-Flusskontrolle aus, so wie es eine
schwache Funkstrecke tut. Bloß langsam *lesen* taugt als Test nicht, das füllt
nur den eigenen Empfangspuffer. Szene: 1280×960/65 bei 12 fps.

| | vorher (192 kB, Nagle an) | nachher (48 kB, Nagle aus) |
|---|---|---|
| Rückstand, Median | 4 Bilder | **1 Bild** |
| Rückstand, schlimmster Fall | 20 Bilder (~1,7 s) | **4 Bilder (~0,3 s)** |
| angekommene Bilder | 6,7 /s | **9,1 /s** |

Der schnelle Client verliert dabei nichts: unverändert 11,8 von 12 fps bei
6,8 Mbit/s und 1–2 Bildern Rückstand. Der kleine Puffer kostet erst dann
Durchsatz, wenn er unter Bandbreite × Laufzeit fällt — davon ist er hier weit
entfernt.

**Was auf dem Handy am meisten bringt, ist trotzdem die Bildgröße:** im
Detailmodus ist ein Bild rund 70 kB, im Flüssig-Modus rund 25 kB. Auf einer
schmalen Strecke ist die reine Übertragungszeit eines Bildes Verzögerung —
bei 4 Mbit/s sind das 140 ms gegenüber 50 ms. Fürs Herumlaufen mit dem Handy
also **Flüssig**; der Detailmodus ist zum Beurteilen der Schärfe am großen
Schirm da.

Dazu kommt ein Effekt, der sich von hier aus nicht messen lässt (die Messungen
oben liefen über **Ethernet**, `192.168.0.60`): der Pi funkt selbst, und das
Handy funkt auf demselben Kanal noch einmal. Die Strecke Pi → Router → Handy
belegt die Luft also zweimal und teilt sich dieselbe Sendezeit, während der
Weg zum verkabelten PC nur eine Funkstrecke hat. Das Handy sieht daher
grundsätzlich weniger Bandbreite als die 29 Mbit/s der Messung — plausibel,
aber nicht nachgemessen. Passend dazu schwankt die Strecke: bei −54 dBm waren
29 Mbit/s drin, bei −60 dBm nur noch rund 15.

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

**Probemontage (2026-09-28):** das montierte Modul hat einen *schaltenden*
IR-Sperrfilter, der sich nach Helligkeit selbst ein- und aushängt. Damit hat
die Kamera zwei Farblagen statt einer, und ein Tuning kann nur für eine davon
stimmen: am Tag (Filter drin) passt `normal`, nachts (Filter draußen, Bild
ohnehin nahezu grau) passt `noir`. Weil die Nachtlage die ist, in der es auf
Erkennung ankommt, und ein grauer Kanal keinen Weißabgleich braucht, bleibt
`noir` stehen; stört der Tagesstich beim Justieren, kurz auf `normal` stellen.
Die Fußzeile der Treffer-Bilder (Lux, Belichtungszeit, Gain) sagt im Nachhinein,
in welcher Lage das Bild entstanden ist — nachts gemessen: Lux 13, 67 ms, Gain 8.

## Bewegungserkennung (2026-10-04)

**Warum:** Die Katze kommt fast immer hinten rechts an der Hecke herein und ist
dort im 1640er-Bild nur ~25×45 Pixel gross. Mit den Radar-Bildern vom
2026-10-04 offline nachgemessen (Ausschnitte 1,5×–6× um die Katze, plus
Gegenproben Mäher/Rasen/Hecke/Blumentopf/Schatten):

| Modell | ferne Katze | grosse Katze 13:35 | Blumentopf (Gegenprobe) |
|---|---|---|---|
| SSD-MobileNet-v2 COCO (bisher) | sheep 33 / person 16 | bear 96 | person 23 |
| SSDLite-MobileDet | teddy bear 48 / potted plant 46 | cow 58 | teddy bear 73 |
| EfficientDet-Lite2 (448) | dog 60 / person 52 | sheep 52 | person 52 |
| EfficientDet-Lite3 (512) | sheep 67 / person 41 | sheep 74 | person 69 |
| ImageNet-Klassifizierer (MobileNet, EfficientNet-M/L, Inception-v4) | Hunderassen, Dugong, Schnecke … | lynx 34 / tiger cat 21 | coral reef 81 |

Kein Modell nennt die ferne Katze „cat", und die Gegenprobe bekommt dieselben
Werte — **das Etikett trägt bei so wenigen Pixeln nicht**, auch ein grösseres
Modell ändert das nicht. Selbst die gut sichtbare Katze um 13:35 (~120×80 px)
hatte das bisherige 4×3-Kachelraster gar nicht gemeldet.

**Was stattdessen trägt: Bewegung.** `kivision_motion.py` zieht auf dem
lores-Zweitstrom (820×616, nur Y) einen MOG2-Hintergrund ab (~37 ms je Bild),
verbindet Flecken zu Spuren und nennt eine Spur erst „bewegt", wenn sie
mindestens 3 Bilder alt ist und sich um ≥1,2 % der Bildbreite bzw. 0,6× ihrer
eigenen Grösse verschoben hat. Ein stehender Mäher erzeugt so keine Spur, eine
wehende Hecke keine bewegte. Springt mehr als 20 % des Bildes gleichzeitig
(Sonne/Wolke), wird das Bild verworfen und schneller nachgelernt.

- **Voraussetzung:** `sudo apt install python3-opencv` (die venv sieht die
  System-Pakete; ohne OpenCV läuft alles wie vorher, nur ohne Bewegung).
- **Bildpumpe:** Ein Thread holt jedes Bild genau einmal (`capture_pair`: main
  RGB + lores aus *derselben* Aufnahme) und füttert Bewegung und Coral.
- **Gezielte KI:** Gibt es bewegte Spuren, schaut die Coral nur noch in
  quadratische Ausschnitte um sie (2,5× Spurgrösse, mind. 160 px, max. 3 Spuren,
  je ~25 ms) und das volle Kachelraster nur noch jede 4. Runde. Was sie dort
  sieht, wird der Spur zugeordnet (Gruppen katze/tier/person/fahrzeug).
- **Anzeige:** Spuren hellblau im Livebild und in den Radar-Bildern
  (`B<nr> <KI-Befund>` + zurückgelegter Weg), Kopfzeile der Erkennung zeigt
  Bewegungs-ms und bewegte/alle Spuren.
- **Bewegungs-Bilder** (eigene Galerie): ein Bild je Spur, die 1,5 s bewegt ist,
  höchstens alle 10 s. Journal: `[motion] Spur B… vorbei: Dauer, Weg, Grösse, Start/Ende, KI`.
- **`pairs.jsonl`:** bei jeder Radar-Meldung mit Weltposition, während die Kamera
  bewegte Spuren hat, eine Zeile `{t, sender, wx, wy, tracks:[{foot, w, h, …}]}`.
  Rohstoff für die Homographie (Phase 4) — automatisch kalibriert aus Paaren
  statt von Hand, danach Kamera-Spur ↔ Radar-Ziel räumlich zuordnen und die
  echte Grösse (Katze vs. Fuchs vs. Person) in Metern prüfen.

## Dienst

```bash
sudo systemctl status kivision-web      # Zustand
sudo systemctl restart kivision-web     # nach Code-Änderung
journalctl -u kivision-web -f           # Log
```

Der Dienst ist `enabled`, startet also nach jedem Stromausfall von selbst.

## Neu ausrollen

```bash
scp kivision_web.py kivision_motion.py xcom.py kivision_bus.py ../Controller/Manager6_3_0/xComDef6_3.h pi@192.168.0.186:~/kivision/web/
scp templates/index.html pi@192.168.0.186:~/kivision/web/templates/
ssh pi@192.168.0.186 sudo systemctl restart kivision-web
```

## Beobachtungen vom ersten Test (2026-08-27)

- Das Modul zeigt einen deutlichen **Magenta-Stich**, auch bei Tageslicht.
  Am 2026-08-29 als fehlender IR-Sperrfilter nachgewiesen und mit der
  NoIR-Tuning-Datei behoben — siehe „Farbstich" oben.
- Starke **Tonnenverzeichnung** (Weitwinkel) — wie im Konzept erwartet. Vor der
  Homographie in Phase 4 muss die Verzeichnung korrigiert werden.
