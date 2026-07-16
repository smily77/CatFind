# Karten-Konzept: Aufräumen + NoShot-Editor

Stand: 2026-07-16 — Entwurf zur Diskussion

## Leitplanken

1. **Der Karten-Upload MUSS ohne VPS funktionieren.** Der Manager ist die Wahrheit
   vor Ort; der VPS ist nur Spiegel (Anzeige) und Komfort-Weg (Editor). Fällt der
   VPS aus, bleibt das System inkl. Kartenpflege voll bedienbar.
2. **Karten gehören nicht in den Firmware-Quellcode.** `NOSHOT_DEFAULT` und
   `RASEN_DEFAULT` in `Manager6_3_0.ino` fliegen raus — eine Kartenänderung darf
   nie wieder einen Flash bedeuten.
3. **Eine Karte, ein Format, eine Einheit.** Alles Betriebliche in Welt-mm mit dem
   bestehenden Header (`# <Typ> v<N> crc=… units=mm frame=world`, Ringe durch
   Leerzeilen). Der Meter/mm-Mix bei der RasenKarte verschwindet.
4. **Version/CRC zählt nie mehr ein Mensch hoch.** Der Manager vergibt beide beim
   Annehmen eines Uploads selbst.

## Ist-Zustand (was heute wo liegt)

| Karte | Heute | Nutzer |
|---|---|---|
| **NoShot** (erlaubte Schusszone) | Raw-String im `Manager6_3_0.ino` → Seed ins LittleFS; Kopien `noshot.csv` + `data/noshot.csv` im Repo; VPS parst den .ino per Regex von GitHub (nur Anzeige) | Lidar (Feuerfreigabe), VPS-Analyse |
| **Rasen** (Relevanzfilter) | `Map/RasenKarte.csv` in **Metern** (VPS-Weltkarte) + `RASEN_DEFAULT` in **mm** im Manager-Code (Handkonvertierung) | HLK-Radare (Meldefilter), VPS-Weltkarte |
| **Coverage** (gelernte Erfassung) | VPS-SQLite, `/coverage_export.csv` | CatIdentifier, VPS-Modell |
| `Map/karte_*.csv`, `Landmark.csv` | Repo | Vermessungs-Rohdaten |

Schmerzpunkte: Karten im Firmware-Code (Änderung = Flash), VPS liest Firmware-
Quelltext per Regex von GitHub, Versionszählen von Hand, Einheiten-Mix, laufende
Geräte laden Karten nur beim Boot.

Was schon gut ist und **bleibt**: das UDP-Chunk-Protokoll Manager→Sensoren
(`mapRequest → mapInfo → mapChunk`) mit Version/CRC-Abgleich in `acquireMap()` —
nur die Quelle am Anfang der Kette ist falsch verankert.

## Zielbild

```
                 ┌────────────────────────────────────────────┐
                 │  Manager (LittleFS = MASTER)               │
   Upload-Wege   │  /noshot.csv  /rasen.csv                   │   Verteilung
                 │  Version/CRC vergibt der Manager selbst    │
  ──────────────►│                                            │──────────────►
  A) lokal, ohne │  bei Annahme:                              │  bestehendes
     VPS: HTTP-  │   • Version++, CRC neu, Header schreiben   │  Chunk-Protokoll
     POST direkt │   • mapInfo-Announce (Multicast)           │  an Lidar/Radar
     an den      │   • Kartenstand an VPS melden (Spiegel)    │  (acquireMap
     Manager     │                                            │   unverändert)
  B) komfortabel └────────────────────────────────────────────┘
     via VPS-Editor: /commands-Poll → Manager HOLT die Karte per HTTP GET
```

Der Manager ist **einziger Master**. Es gibt zwei Upload-Wege zu ihm — beide
enden im selben Annahme-Code (Validierung, Version++, Announce):

### Weg A — lokal, ohne VPS (Pflicht-Weg)

Der Manager bekommt einen minimalen HTTP-Endpunkt im lokalen Netz
(er hat bisher keinen WebServer — nur OTA/UDP/HTTPClient; ein schlanker
`WebServer` auf Port 80 mit zwei Routen genügt):

```
GET  http://<manager>/map?type=noshot     → aktuelle CSV (Backup/Kontrolle)
POST http://<manager>/map?type=noshot     → neue CSV (Body), Antwort: v/crc
```

Bedienung z. B. per `curl` oder einem kleinen Upload-Skript im Repo
(`Map/upload_map.py` o. ä.: nimmt CSV, postet an den Manager, zeigt die
Antwort). Damit ist die Kartenpflege komplett VPS-unabhängig: CSV im Repo
editieren → Skript → fertig. Optional als Ausbaustufe: eine simple statische
Upload-/Download-Seite direkt vom Manager serviert.

### Weg B — komfortabel, via VPS-Editor

Der VPS kann den Manager nicht direkt erreichen (Manager pollt den VPS, nicht
umgekehrt) — deshalb Pull statt Push, über den bestehenden Kanal:

1. Editor im Analyse-Tab speichert → VPS legt die Karte als **pending** ab
2. Die nächste `/commands`-Antwort (Poll läuft ohnehin alle paar Sekunden)
   enthält ein Kommando „Karte <typ> abholen"
3. Manager holt die CSV per `HTTP GET /maps/<typ>?pending=1` vom VPS
4. Derselbe Annahme-Code wie bei Weg A: validieren, Version++, CRC,
   LittleFS schreiben, Announce, Kartenstand zurückmelden
5. VPS markiert pending als übernommen; der Editor zeigt „übernommen als v13“

Fällt der VPS aus, fehlt nur der Komfort-Editor — Weg A bleibt.

### Annahme-Code im Manager (gemeinsam für A und B)

- **Validierung**: parsebar, jeder Ring ≥ 3 Punkte, Koordinaten plausibel
  (Welt-mm-Range), Gesamtgröße ≤ Chunk-Protokoll-/LittleFS-Limit
  (`mapChunkBytes × chunkCount`, `mapRxBuf` der Empfänger). Bei Fehler:
  ablehnen, alte Karte bleibt aktiv — es gibt keinen Zustand „kaputte Karte“.
- **Versionierung**: Version = alte + 1, CRC über den Inhalt, Header schreibt
  der Manager. Egal über welchen Weg die Karte kam.
- **Atomar**: erst nach vollständigem, validiertem Empfang in eine Temp-Datei →
  Rename auf `/noshot.csv` (das Muster aus `mapBeginRx` existiert schon).

## Verteilung an die Sensoren (Update ohne Reboot)

Bestehendes Chunk-Protokoll bleibt unverändert. Neu:

1. **Announce**: nach jeder angenommenen Karte broadcastet der Manager ein
   unaufgefordertes `mapInfo` (Typ, Version, CRC) per Multicast. Lidar/Radar
   vergleichen mit ihrer lokalen Version und rufen bei Abweichung ihr
   vorhandenes `acquireMap()` erneut auf — der Handler ist klein, die ganze
   Download-Logik existiert.
2. **Fangnetz**: periodischer Re-Check (z. B. stündlich) in Lidar/Radar, weil
   der UDP-Announce verloren gehen kann (bekanntes Thema beim Kartentransfer;
   es gibt kein Resend).
3. Fortschritt bleibt sichtbar: die vorhandenen `sendUdpTextln`-Meldungen
   („Karte …: lade vom Manager (v…)“) landen weiterhin im VPS-Debug-Fenster.

## VPS: vom Regex-Parser zum Spiegel

- Der GitHub-Regex-Fetch (`NOSHOT_URL` → `Manager6_3_0.ino` parsen) **fliegt
  raus**. Ebenso `MAP_URL` auf die Meter-RasenKarte.
- Der Manager **meldet seinen Kartenstand an den VPS**: beim Boot und nach
  jeder Kartenänderung schickt er Version/CRC (im `/ingest`-Body als neues
  Feld) und bei Abweichung die CSV (POST an einen neuen `/mapsync`-Endpunkt).
  Der VPS zeigt damit immer das, was **wirklich aktiv** ist — nicht das, was
  auf GitHub gepusht wurde.
- `/noshot` und `/map` (Weltkarten-Hintergrund) lesen aus dem Spiegel.
  Ist der Spiegel leer (frischer VPS, Manager noch nie verbunden), zeigt die
  Karte schlicht nichts an — kein GitHub-Fallback mehr.

## NoShot-Editor im Analyse-Tab

Direkt auf der bestehenden Analyse-Karte — Events, Coverage und Totzonen liegen
als Referenz darunter, genau das, was man beim Ziehen der Schusszone sehen will:

- **„Karte bearbeiten“-Knopf** schaltet den Edit-Modus ein: Ringe werden
  Polygone mit greifbaren Eckpunkten
- Punkt ziehen · Klick auf eine Kante fügt einen Punkt ein · Punkt markieren +
  Entf löscht ihn · Ring hinzufügen/löschen (Löcher = No-Shot-Inseln, das
  Format kann das schon)
- **Kartentyp-Dropdown**: derselbe Editor bearbeitet NoShot **und** Rasen
- **Speichern** → pending auf dem VPS → Weg B läuft automatisch; der Editor
  zeigt den Zustand: „wartet auf Manager …“ → „übernommen als v13“ →
  (aus den Debug-Meldungen) „Lidar lädt v13“
- **Verwerfen** stellt den Spiegelstand wieder her
- Statuszeile „NoShot v13 — Lidar: v13 ✓“; sauber wird das als Ausbaustufe,
  wenn die Sensoren ihre Kartenversion im HB mitschicken

Ohne VPS wird „editieren“ zu: CSV im Repo anpassen + Upload-Skript (Weg A).
Bewusst ohne zweiten grafischen Editor — der Notweg soll simpel und robust sein.

## Repo/Code aufräumen

- `NOSHOT_DEFAULT` + `RASEN_DEFAULT` aus `Manager6_3_0.ino` **entfernen**
  (~100 Zeilen). Erstinbetriebnahme ohne Karte: Manager antwortet auf
  `mapRequest` schlicht nicht, Sensoren melden „Karte nicht verfügbar“ ins
  Debug-Fenster (Meldung existiert), Karte kommt per Upload-Weg A. Kein
  Seed-Sonderfall mehr.
- **Referenzkopien** der Betriebskarten an EINEM Ort im Repo:
  `Map/noshot.csv` und `Map/rasen.csv` (Welt-mm, Betriebsformat) — das sind
  Backup + Erstbefüllung per Upload-Skript, nicht die laufende Wahrheit.
- `Controller/Manager6_3_0/noshot.csv` und `data/noshot.csv` **löschen**
  (der FS-Upload-Weg entfällt).
- `Map/RasenKarte.csv` (Meter) bleibt als Vermessungs-Original liegen;
  `Map/` per README-Zeile als „Rohdaten + Referenzkopien“ deklarieren.
- Doku (`Dokumentation_6_3.md`): Kapitel „Karten“ mit der Tabelle
  Karte → Master → Konsument → Verteilweg.
- **Coverage bleibt unverändert** — dort ist der VPS als Master richtig
  (die Polygone entstehen aus VPS-Daten und der Hauptkonsument CatIdentifier
  holt sie schon per HTTP vom VPS). Sie wird nur in der Doku mit einsortiert.

## Sicherheit / offene Punkte

- Der Manager-Upload-Endpunkt verändert eine **Schuss-Freigabezone**. Er ist
  nur im lokalen Netz erreichbar; trotzdem mindestens ein statisches Token
  (Header) vorsehen — passt zum offenen Auth-Thema (Review-Punkt 10), das für
  die VPS-Schreibendpunkte ohnehin ansteht.
- WebServer auf dem Manager kostet etwas RAM/Loop-Zeit; `server.handleClient()`
  ist nicht-blockierend und der Endpunkt wird selten benutzt — mit der neuen
  MC-Queue unkritisch, aber beim Test auf 10-Hz-Bursts achten.
- ~~Der `F_corr`-Branch ist noch nicht gemerged~~ — war veraltet: `F_corr`
  (PR #12) ist seit 2026-07-12 in `main` gemergt (Commit `de5f55b`), vor
  Firmware-Änderungen am Manager (Stufen 1/4) ist also nichts mehr zu klären.

## Umsetzung in Stufen (jede einzeln deploybar)

| Stufe | Inhalt | Deploy |
|---|---|---|
| 1 | Manager: Karten aus dem Code raus, HTTP-GET/POST-Endpunkt, Annahme-Code (Validierung, Auto-Version, Announce), Kartenstand-Push an VPS; Upload-Skript + Referenz-CSVs in `Map/` | Manager-OTA |
| 2 | VPS: Spiegel statt GitHub-Regex, `/mapsync`, `/maps/<typ>`, pending-Mechanik im `/commands`-Kanal | VPS |
| 3 | Analyse-Tab: Polygon-Editor mit Status-Rückmeldung | VPS |
| 4 | Lidar/Radar: `mapInfo`-Announce-Handler + stündlicher Re-Check | Sensor-OTA |
| 5 | Repo/Doku aufräumen (alte CSVs, README, Doku-Kapitel) | — |

Stufe 1 allein erfüllt bereits beide Kernforderungen: Upload ohne VPS und
keine Karten mehr im Code. Die Stufen 2–4 sind Komfort (Editor, Live-Reload).
