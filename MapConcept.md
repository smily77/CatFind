# Karten-Konzept: Aufräumen + NoShot-Editor

Stand: 2026-07-16 — Entwurf zur Diskussion

## Leitplanken

1. **Der VPS-Editor ist der reguläre Weg für Kartenänderungen.** Der Manager
   bleibt die Wahrheit vor Ort (LittleFS = Master im Betrieb). Fällt der VPS
   aus, läuft die Erfassung mit der zuletzt geladenen Karte unverändert
   weiter — nur *Ändern* der Karte ist dann nicht mehr komfortabel möglich
   (Notweg siehe unten, kein eigener Netzwerk-Upload-Endpunkt am Manager).
2. **Karten gehören nicht dauerhaft in den Firmware-Quellcode.**
   `NOSHOT_DEFAULT` und `RASEN_DEFAULT` in `Manager6_3_0.ino` fliegen raus.
   Reguläre Kartenänderungen (über den VPS) bedeuten nie einen Flash. Der
   Notweg ohne VPS (Repo-CSV von Hand anpassen, Firmware neu bauen und
   flashen) ist bewusst die akzeptierte Ausnahme, kein Dauerzustand.
3. **Eine Karte, ein Format, eine Einheit.** Alles Betriebliche in Welt-mm mit dem
   bestehenden Header (`# <Typ> v<N> crc=… units=mm frame=world`, Ringe durch
   Leerzeilen). Der Meter/mm-Mix bei der RasenKarte verschwindet.
4. **Version/CRC zählt nie mehr ein Mensch hoch.** Der Manager vergibt beide beim
   Annehmen einer Kartenänderung selbst.
5. **Das Repo ist immer Abbild des aktiven Kartenstands.** Jede über den VPS
   angenommene Kartenänderung wird automatisch nach `Map/noshot.csv` bzw.
   `Map/rasen.csv` zurückcommittet — das Repo ist nicht mehr nur Backup/
   Erstbefüllung, sondern laufender Spiegel dessen, was gerade auf dem
   Manager aktiv ist. Kein manueller Schritt nötig.

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
                 │  Manager (LittleFS = MASTER im Betrieb)     │
                 │  /noshot.csv  /rasen.csv                    │   Verteilung
                 │  Version/CRC vergibt der Manager selbst     │
                 │                                              │──────────────►
  regulärer Weg: │  bei Annahme:                                │  bestehendes
  VPS-Editor →   │   • Version++, CRC neu, Header schreiben     │  Chunk-Protokoll
  /commands-Poll │   • mapInfo-Announce (Multicast)             │  an Lidar/Radar
  → Manager HOLT │   • Kartenstand an VPS melden                │  (acquireMap
  per HTTP GET  ─┼──────────────────────────────────────────────┘   unverändert)
                 └────────────────────────────────────────────┘
                                     │
                                     │ VPS committet angenommene Karte
                                     ▼
                          Git-Repo (Map/noshot.csv, Map/rasen.csv)
                          — immer aktuelles Abbild, kein manueller Schritt

  Notweg ohne VPS: Repo-CSV von Hand ändern → Firmware/LittleFS neu bauen
  → Manager flashen (kein Editor-Komfort, aber funktioniert autark)
```

Der Manager ist **einziger Master im Betrieb**. Es gibt genau einen
produktiven Weg, ihm eine neue Karte zu geben — den VPS-Editor — plus einen
bewusst schwerfälligen Notweg ohne Netzwerk-Endpunkt.

### Kartenänderung über den VPS-Editor (regulärer Weg)

Der VPS kann den Manager nicht direkt erreichen (Manager pollt den VPS, nicht
umgekehrt) — deshalb Pull statt Push, über den bestehenden Kanal:

1. Editor im Analyse-Tab speichert → VPS legt die Karte als **pending** ab
2. Die nächste `/commands`-Antwort (Poll läuft ohnehin alle paar Sekunden)
   enthält ein Kommando „Karte <typ> abholen"
3. Manager holt die CSV per `HTTP GET /maps/<typ>?pending=1` vom VPS
   (nutzt den bereits vorhandenen `HTTPClient`, kein neuer Endpunkt am
   Manager nötig)
4. Annahme-Code im Manager (siehe unten): validieren, Version++, CRC,
   LittleFS schreiben, Announce, Kartenstand zurückmelden
5. VPS markiert pending als übernommen, committet die angenommene CSV nach
   `Map/<typ>.csv` im Git-Repo (siehe „VPS: vom Regex-Parser zum Spiegel“)
   und der Editor zeigt „übernommen als v13“

### Notweg ohne VPS (Ausnahme, kein Netzwerk-Endpunkt am Manager)

Fällt der VPS aus oder soll eine Karte ganz ohne VPS geändert werden, gibt es
bewusst **keinen** eigenen Upload-Endpunkt am Manager (kein WebServer nötig):

1. `Map/<typ>.csv` im Repo von Hand anpassen
2. Firmware (inkl. LittleFS-Image) neu bauen und auf den Manager flashen
3. Der laufende Betrieb (Sensoren mit der zuletzt geladenen Karte) läuft bis
   dahin unterbrechungsfrei weiter — es ist kein Ausfall im Sinne von
   „System steht“, sondern nur „Karte kann gerade nicht komfortabel
   geändert werden“

Der Aufwand (Compile + Flash) ist bewusst der Preis dafür, dass der Manager
keinen zusätzlichen Netzwerk-Endpunkt braucht und keine Angriffsfläche für
die Schuss-Freigabezone im lokalen Netz entsteht.

### Annahme-Code im Manager

- **Validierung**: parsebar, jeder Ring ≥ 3 Punkte, Koordinaten plausibel
  (Welt-mm-Range), Gesamtgröße ≤ Chunk-Protokoll-/LittleFS-Limit
  (`mapChunkBytes × chunkCount`, `mapRxBuf` der Empfänger). Bei Fehler:
  ablehnen, alte Karte bleibt aktiv — es gibt keinen Zustand „kaputte Karte“.
- **Versionierung**: Version = alte + 1, CRC über den Inhalt, Header schreibt
  der Manager.
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

## VPS: vom Regex-Parser zum Spiegel (und Repo-Schreiber)

- Der GitHub-Regex-Fetch (`NOSHOT_URL` → `Manager6_3_0.ino` parsen) **fliegt
  raus**. Ebenso `MAP_URL` auf die Meter-RasenKarte.
- Der Manager **meldet seinen Kartenstand an den VPS**: beim Boot und nach
  jeder Kartenänderung schickt er Version/CRC (im `/ingest`-Body als neues
  Feld) und bei Abweichung die CSV (POST an einen neuen `/mapsync`-Endpunkt).
  Der VPS zeigt damit immer das, was **wirklich aktiv** ist — nicht das, was
  auf GitHub gepusht wurde.
- Nimmt der VPS über `/mapsync` eine vom Manager bestätigte neue Kartenversion
  entgegen, committet er sie automatisch nach `Map/<typ>.csv` im Git-Repo
  (z. B. per GitHub-API mit einem eng gescopten Deploy-Token). Damit ist das
  Repo immer aktuell, ohne dass jemand manuell CSVs hochladen oder committen
  muss.
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
- **Speichern** → pending auf dem VPS → regulärer Weg läuft automatisch; der
  Editor zeigt den Zustand: „wartet auf Manager …“ → „übernommen als v13“ →
  (aus den Debug-Meldungen) „Lidar lädt v13“
- **Verwerfen** stellt den Spiegelstand wieder her
- Statuszeile „NoShot v13 — Lidar: v13 ✓“; sauber wird das als Ausbaustufe,
  wenn die Sensoren ihre Kartenversion im HB mitschicken

Ohne VPS ist Kartenpflege nur über den Notweg möglich (Repo-CSV anpassen +
Firmware neu flashen, siehe oben) — bewusst kein zweiter grafischer Editor,
der Notweg soll simpel bleiben.

## Repo/Code aufräumen

- `NOSHOT_DEFAULT` + `RASEN_DEFAULT` aus `Manager6_3_0.ino` **entfernen**
  (~100 Zeilen). Erstinbetriebnahme: die Startkarte kommt entweder gleich im
  LittleFS-Image mit (Erstflash, Notweg) oder folgt beim ersten
  Editor-Speichern über den VPS. Bis dahin antwortet der Manager auf
  `mapRequest` schlicht nicht, Sensoren melden „Karte nicht verfügbar“ ins
  Debug-Fenster (Meldung existiert). Kein Seed-Sonderfall mehr im Code.
- **Referenzkopien** der Betriebskarten an EINEM Ort im Repo:
  `Map/noshot.csv` und `Map/rasen.csv` (Welt-mm, Betriebsformat) — vom VPS
  automatisch aktuell gehalten, kein manueller Upload-/Commit-Schritt mehr.
- `Controller/Manager6_3_0/noshot.csv` und `data/noshot.csv` **löschen**
  (der FS-Upload-Weg entfällt).
- `Map/RasenKarte.csv` (Meter) bleibt als Vermessungs-Original liegen;
  `Map/` per README-Zeile als „Rohdaten + laufend aktuelle Betriebskarten“
  deklarieren.
- Doku (`Dokumentation_6_3.md`): Kapitel „Karten“ mit der Tabelle
  Karte → Master → Konsument → Verteilweg.
- **Coverage bleibt unverändert** — dort ist der VPS als Master richtig
  (die Polygone entstehen aus VPS-Daten und der Hauptkonsument CatIdentifier
  holt sie schon per HTTP vom VPS). Sie wird nur in der Doku mit einsortiert.

## Sicherheit / offene Punkte

- Kein eigener Upload-Endpunkt am Manager mehr nötig (kein WebServer) — der
  Kartenaustausch läuft komplett über die VPS-Endpunkte (`/mapsync`,
  `/maps/<typ>`, `/commands`). Das reduziert die Angriffsfläche am Manager
  im lokalen Netz, verlagert das Auth-Thema aber vollständig auf die
  VPS-Schreibendpunkte (passt zum offenen Punkt „Review-Punkt 10“, der für
  die VPS-Schreibendpunkte ohnehin ansteht).
- Der VPS braucht Schreibrechte auf das Git-Repo (Deploy-Token/GitHub-App),
  um `Map/<typ>.csv` automatisch zu committen — Token so eng wie möglich
  scopen (nur Schreibrecht auf `Map/`, kein Force-Push), damit ein
  kompromittierter VPS nicht das ganze Repo gefährdet.

## Umsetzung in Stufen (jede einzeln deploybar)

| Stufe | Inhalt | Deploy | Status |
|---|---|---|---|
| 1 | Manager: Karten aus dem Code raus; Annahme-Code (Validierung, Auto-Version, Announce) für die per VPS abgeholte Karte; Kartenstand-Push an VPS. Kein WebServer nötig — nur `/commands`-Poll (existiert) + `HTTP GET` vom VPS (`HTTPClient` existiert schon) | Manager-OTA | ✅ umgesetzt |
| 2 | VPS: Spiegel statt GitHub-Regex, `/mapsync`, `/maps/<typ>`, pending-Mechanik im `/commands`-Kanal, automatischer Git-Commit der angenommenen Karte nach `Map/<typ>.csv` | VPS | ✅ umgesetzt |
| 3 | Analyse-Tab: Polygon-Editor mit Status-Rückmeldung | VPS | ✅ umgesetzt |
| 4 | Lidar/Radar: `mapInfo`-Announce-Handler + stündlicher Re-Check | Sensor-OTA | ✅ umgesetzt |
| 5 | Repo/Doku aufräumen (alte CSVs, README, Doku-Kapitel) | — | ✅ umgesetzt |

Stufe 1 allein entfernt bereits die Karten aus dem Code, liefert aber noch
keinen Weg für eine neue Karte (kein Pending-Mechanismus ohne Stufe 2) — für
den ersten produktiven Nutzen gehören Stufe 1+2 zusammen. Der Notweg
(Repo-CSV + Flash) funktioniert schon ab Stufe 1 unabhängig davon. Die
Stufen 3–4 sind Komfort (Editor, Live-Reload).

## Umsetzungsstatus und Warnungen (nach Implementierung)

Alle fünf Stufen sind im Code umgesetzt (Manager-Firmware, VPS-Backend,
Analyse-Tab-Editor, Sensor-Firmware, Repo-Aufräumung). Geprüft wurde:

- **VPS-Backend**: end-to-end per Flask-Testclient UND per echtem Browser
  (Playwright) durchgespielt — Editor speichert → `/maps/<typ>` pending →
  simulierter Manager holt ab (`GET /maps/<typ>?pending=1`) → bestätigt
  (`POST /mapsync`) → Spiegel aktualisiert, `Map/<typ>.csv`-Commit ausgelöst
  (ohne Token no-op, siehe unten) → Editor-Statuszeile pollt und zeigt
  „übernommen als vN“. Alle Schritte funktionieren wie im Konzept beschrieben.
- **Analyse-Tab-Editor**: Punkt ziehen, Klick-auf-Kante-fügt-Punkt-ein,
  Punkt löschen (Taste/Button), Ring hinzufügen/löschen im echten Browser
  geprüft (Playwright, Koordinaten aus dem Seiten-Kontext berechnet) —
  funktioniert korrekt inkl. Welt-mm-Umrechnung.
- **Manager-Firmware**: **konnte nicht kompiliert werden** — diese Umgebung
  hat keine Arduino-Toolchain/Board-Definitionen. Geprüft wurden stattdessen
  Klammern-/Klammer-Balance, Funktions-/Variablen-Sichtbarkeit über die
  `.ino`-Tab-Reihenfolge (Arduino generiert nur für Funktionen automatische
  Prototypen, nicht für Variablen — dabei wurde ein echter Fehler gefunden
  und behoben: `ensureMaps()` griff direkt auf `gwMapVer`/`gwMapCrc` aus
  `gatewayProc.ino` zu, das textuell NACH `Manager6_3_0.ino` einfliesst;
  jetzt nur noch ein Funktionsaufruf) sowie die Handhabung von
  `noShotLoaded`/`insideNoShot` (Annahme-Validierung mutiert dieselbe
  globale Polygon-Struktur wie die Fire-Gating-Logik — auf dem Manager
  unkritisch, da er `insideNoShot` selbst nie aufruft; auf den Sensoren
  wurde bewusst NUR die eigene Status-Variable `noShotOK`/`rasenLoaded`
  zurückgesetzt, nie `noShotLoaded` direkt, damit ein Announce/Re-Check nie
  ein Fail-Open-Fenster öffnet). **Vor dem Flashen auf echte Hardware:
  Kompilieren und auf dem Simulator/einem Testgerät verifizieren.**
- **Sensoren (Radar/LidarC1)**: dieselbe Einschränkung — kein Compiler
  verfügbar, nur Code-Review + Cross-Tab-Sichtbarkeitsprüfung.

Offene Punkte / bewusste Vereinfachungen:

- **Keine Authentifizierung** auf den neuen VPS-Endpunkten (`/maps/<typ>`,
  `/mapsync`) — passt zum bereits bekannten offenen Punkt „Review-Punkt 10“
  (gilt für alle VPS-Schreibendpunkte, nicht neu durch dieses Konzept).
- **Git-Commit ist best-effort**: ohne `GITHUB_MAP_TOKEN` (Env-Var) wird er
  übersprungen (kein Fehler, keine Blockade der Kartenverteilung selbst).
  Token muss vor dem produktiven Einsatz von Stufe 2 gesetzt werden, sonst
  bleibt Leitplanke 5 („Repo ist immer Abbild“) unerfüllt.
- **Header-`crc=`-Feld ist rein informativ**: es deckt nur die Ring-Daten
  ohne Kopfzeile ab (ein selbstreferenzieller CRC über die ganze Datei
  inkl. der eigenen `crc=`-Zeichenkette ist unmöglich). Der tatsächlich
  fürs Versions-/Integritätsprotokoll verwendete `fileCrc`
  (`mapInfoPayload`) wird weiterhin korrekt über die komplette Datei
  berechnet (`mapFileInfo`/`crc32Bytes`) — nur die Textzahl im Kommentar
  ist eine Annäherung fürs menschliche Auge.
- **Ring-Validierung ist etwas großzügiger als „jeder Ring ≥ 3 Punkte“**:
  `gwAcceptMap` nutzt den vorhandenen `loadNoShot`-Parser, der nur prüft
  „mindestens 1 Ring UND insgesamt ≥ 3 Punkte“ (nicht: JEDER Ring einzeln
  ≥ 3). Über den regulären Weg (Editor) kann das nicht passieren — sowohl
  das Frontend als auch `/maps/<typ>` (POST) im VPS lehnen Ringe mit < 3
  Punkten schon vorher ab. Nur beim Notweg (Repo-CSV von Hand editieren,
  am Editor vorbei) könnte ein zu kurzer Ring unbemerkt durchrutschen; er
  würde von `insideNoShot` dann still ignoriert (Ring mit < 3 Punkten
  trägt nicht zum Point-in-Polygon-Test bei), nicht hart abgelehnt.
- **Editor-„Ring +“** legt ein kleines Dreieck in der Kartenmitte an, das
  man danach an Ort und Stelle ziehen muss — kein Klick-zum-Zeichnen-Modus
  (bewusst einfach gehalten, siehe Aufwand/Nutzen).
- **Statuszeile „Lidar: v13 ✓“** (Sensor-Kartenversion im HB) ist wie im
  Entwurf vermerkt NICHT umgesetzt — bräuchte eine Protokolländerung
  (`hbPayload`/Sensor-Firmware), als eigene Ausbaustufe offen.
