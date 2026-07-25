# CatFinder VPS — Dashboard (Treffervisualisierung)

Web-Dashboard, das die System-Ereignisse des CatFinder-Netzes zeigt. Die Daten
liefert der **Manager als Gateway** per HTTP-POST (`/ingest`); der lokale
Betrieb läuft unabhängig vom VPS weiter.

Erreichbar unter **`http://<VPS-IP>/`** (Port 80).

## Anzeige

- **Immer sichtbar:** scrollendes System-Debug-Fenster (die per Text-Multicast
  gebroadcasteten Statusmeldungen) + Liste der Geräte, die in den letzten 3 min
  einen HB gesendet haben.
- **Umschaltbar:**
  - **Liste** — alle `catObserved`, pro Minute zu einem Eintrag zusammengefasst
    (Zeit + meldende Sensor-IDs).
  - **Welt-Karte** — `catObserved` in Welt-Koordinaten über der `RasenKarte`.
  - **Manuelle Pose** — Weltkarte mit Referenz-`catObserved` gültig posierter Sensoren und beweglicher/drehbarer Trefferwolke eines zu kalibrierenden Radarsensors; Bestätigung sendet die Pose an den Sensor und löscht/ersetzt die alte Pose.
  - **Gelernte Erfassungs-Polygone** — aus einer manuell gewählten Mäherperiode erzeugt der VPS pro Sensor ein posegebundenes Weltpolygon (6–10 Punkte). Wenn kein passendes Polygon vorhanden ist, wird der Default-Bereich aus xComDef als Polygon verwendet.
  - **Gruppe 1/2/3** — `catObserved` in relativen Koordinaten der jeweiligen
    Koordinatengruppe.
  - Farbe je `(Sensor, Ziel)`-Kombination (Radar bis 3 Ziele).
  - **Reset** löscht die akkumulierten `catObserved` (alle Karten).

## Endpunkte

| Methode | Pfad | Zweck |
|---|---|---|
| `GET`  | `/` | Web-UI |
| `POST` | `/ingest` | Manager-Push `{events,debug,hb,settings,poses,maps}` |
| `GET`  | `/state` | Debug + aktive Geräte + Minuten-Zusammenfassung |
| `GET`  | `/events?since=N` | neue catObserved ab Index N (für die Karten) |
| `POST` | `/reset` | akkumulierte Ereignisse löschen |
| `GET`  | `/map` | Rasen-Punkte (Manager-Spiegel, Meter) für die Welt-Karte |
| `GET`  | `/noshot` `/rasenmap` | No-Shot-/RasenKarte (Manager-Spiegel) als Ringe in Welt-mm |
| `GET`  | `/maps/<typ>` | Manager holt eine pending Karte ab (204 = nichts pending) |
| `POST` | `/maps/<typ>` | Editor speichert eine Kartenänderung als pending |
| `POST` | `/mapsync` | Manager meldet die aktive Karte (Version/CRC + CSV) - siehe `MapConcept.md` |
| `GET`  | `/manual_data` | Snapshot für die manuelle Pose-Seite (Geräte, gültige Posen, letzte Treffer) |
| `POST` | `/manual_pose` | Manuell bestimmte Weltpose als Kommando-Sequenz an Sensor senden |
| `GET`  | `/coverage_profiles` `/coverage_export` | gelernte Erfassungs-Polygone verwalten / kompakt für ESP32 exportieren |
| `POST` | `/coverage_learn` `/coverage_delete` | Polygon aus Mäherperiode lernen / gelerntes Polygon deaktivieren |
| `GET/POST` | `/rec` | Aufnahme-Status / Pause-Append |
| `GET`  | `/density /adata /amodel /alabels /acoverage` | Analyse-Tab (Fenster-Daten, Tracks aus der Analysierer-DB, Abdeckungs-Sektoren) |
| `GET`  | `/hbstats` | Sensor-Spuren der Zeitleiste: HB-Empfangsquote 0..1 je Bin+Sensor (aus `hb_minute`, null = keine Daten) |
| `POST` | `/alabel /alabel_del /amark` | Labels + manuelle Track-Bewertung (Katze/Person/Vogel/Mäher/Insekt/Sturm/Störung/sicher keine Katze) |
| `POST` | `/amerge /aunmerge` | Tracks zusammenkleben (gehören zum selben Tier) / Klebung lösen |
| `GET`  | `/atracks` `/atracks.csv` | komplette Trackliste inkl. Bewertungen+Klebungen (JSON/CSV, separat verwendbar) |
| `GET`  | `/avalidate` | Modell gegen alle von Hand bewerteten Tracks prüfen (Übereinstimmung, Abweichungsliste) |
| `POST` | `/devreload` | xComDef (Typen+Erfassungsbereiche) sofort neu lesen + poseRequest |

## Gelernte Erfassungs-Polygone

Für Radar-Sensoren kann der reale, auf den Rasen begrenzte Erfassungsbereich aus
einer kontrollierten Robomäher-Periode gelernt werden. Vorgehen:

1. Sensor muss eine gültige Weltpose haben; der Mäherzeitraum wird im Analyse-Tab
   ausgewählt (Fenster oder Shift-Selektion).
2. **Coverage lernen** erzeugt per polarer Hüllkurve um den Sensorstandort ein
   einfaches Weltpolygon (typisch 6–10 Punkte) aus den `catObserved` des Sensors.
3. Das Polygon wird zusammen mit der Pose gespeichert. Ändert oder verliert der
   Sensor seine Pose, wird das Polygon deaktiviert und bis zum Neulernen der
   Default-Bereich aus xComDef verwendet.
4. Für VPS-Modell und ESP32/CatIdentifier gilt dasselbe Prinzip: Coverage ist
   eine Liste aktiver Polygone; der ESP32 nutzt `inside-any-polygon` statt eine
   komplizierte Polygon-Union berechnen zu müssen. Der kompakte Export liegt als
   `/coverage_export.csv` vor und wird vom CatIdentifier gecacht.

Ausführliche Beschreibung: siehe `AbbildungErfassungsbereich.md`.

## Kontinuierliche Trackerkennung

Die Trackerkennung läuft **unabhängig von der Betrachtung** in einem
Hintergrund-Thread: er arbeitet die Aufnahme fortlaufend in Häppchen ab
(`catmodel.analyze_stream` — dasselbe Modell, das später auf dem ESP32 laufen
soll), vergibt **fortlaufende Track-Nummern** und persistiert fertige Tracks in
SQLite. `/amodel` liest nur noch — Zoom/Fenstergrösse haben keinerlei Einfluss
mehr auf das Erkennungsergebnis. Parameter- oder „Mäher"-Label-Änderungen lösen
automatisch einen kompletten Neuaufbau aus (Nummern werden neu vergeben;
manuelle Bewertungen und Klebungen überleben über den stabilen Track-key).

State liegt im RAM (Ereignisse kumulieren bis Reset; gehen bei Container-Neustart
verloren). Der Kartenspiegel (No-Shot/Rasen) kommt ausschließlich vom Manager
(`/mapsync`, siehe `MapConcept.md`) und geht bei Container-Neustart ebenfalls
verloren, bis der Manager sich wieder meldet (Boot-Heartbeat via `/ingest`) —
kein GitHub-Fetch/Fallback-CSV mehr im Image.

## Deployment

### Dashboard manuell auf dem VPS aktualisieren

```bash
# auf dem VPS
cd /opt/catfinder/dashboard
git pull --ff-only
docker compose up -d --build
docker compose ps
curl -s localhost/state | head
```

### Localizer-Container kontrolliert neu bauen/starten

Der automatische Localizer bleibt unverändert im eigenen Container. Wenn du ihn
manuell prüfen oder neu deployen willst:

```bash
# auf dem VPS
cd /opt/catfinder/localizer
git pull --ff-only
docker compose up -d --build
docker compose ps
curl -s localhost:8080/health
```

Logs und Neustart bei Bedarf:

```bash
docker compose logs -f --tail=100
docker compose restart
```
