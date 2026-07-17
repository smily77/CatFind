# Karten-Upload zum Manager — Schritt-für-Schritt-Anleitung

Ergänzt `MapConcept.md` (Repo-Root) um die konkrete Bedienungsanleitung für
den **Notweg** (Kartenänderung ohne/am VPS vorbei). Für den Normalbetrieb
gilt weiterhin: Analyse-Tab-Editor auf dem VPS benutzen — dafür ist diese
Anleitung **nicht** nötig.

## Single Source of Truth — wo liegt was

| Ort | Rolle |
|---|---|
| **LittleFS des Managers** (`/noshot.csv`, `/rasen.csv`, auf dem Gerät) | Die tatsächlich **aktive** Karte im Betrieb. Alles andere ist nur ein Weg dorthin. |
| **`Controller/Manager6_3_0/data/noshot.csv` und `.../data/rasen.csv`** (dieses Repo-Verzeichnis) | **Source of Truth im Repo.** Genau der Ordner, aus dem das Arduino/ESP32-Werkzeug das LittleFS-Image baut (USB *und* OTA, siehe unten). Der VPS committet den vom Manager bestätigten Kartenstand automatisch hierhin zurück (`/mapsync` in `dashboard.py`). Für den Notweg wird **ausschließlich diese Datei** editiert. |
| `Map/backup/noshot.csv`, `Map/backup/rasen.csv` | Reine Sicherheitskopie (wird vom VPS zusätzlich committet). Éditieren hat **keine** Wirkung auf den Manager. |
| `Map/RasenKarte.csv`, `Map/karte_*.csv`, `Map/Landmark.csv` | Vermessungs-Rohdaten (Meter), Ursprungsquelle für die erste `rasen.csv`-Version bzw. Eingabe für den VPS-Localizer — nicht Teil des laufenden Karten-Sync.

Merksatz: **„Ändern will ich → `Controller/Manager6_3_0/data/`. Alles andere ist Anzeige oder Backup.“**

## Wann brauche ich das hier überhaupt?

- Der VPS ist nicht erreichbar (Ausfall, noch nicht deployt) und die Karte
  muss trotzdem geändert werden.
- Erstinbetriebnahme eines frisch geflashten Managers (LittleFS ist leer,
  bevor der VPS-Editor je benutzt wurde).
- Wiederherstellung nach einem Defekt/Tausch des Managers.

Für alles andere: Analyse-Tab-Editor auf dem VPS benutzen (`/`, Tab
„Analyse“, Knopf „Karte bearbeiten“) — der läuft komplett ohne diese
Anleitung.

## Voraussetzungen

- Arduino IDE mit installiertem ESP32-Boardpaket (`esp32` by Espressif
  Systems), so wie es zum Kompilieren von `Manager6_3_0.ino` sowieso
  gebraucht wird.
- Für den **USB-Weg**: ein Datenkabel zum Manager-Board und ein LittleFS-
  Upload-Werkzeug für die Arduino-IDE-Version, die installiert ist:
  - Arduino IDE **1.8.x**: Plugin `arduino-esp32fs-plugin`
    (`ESP32 Sketch Data Upload` im Tools-Menü, nach Installation des Plugins
    im Sketchbook-`tools/`-Ordner).
  - Arduino IDE **2.x**: das alte Plugin funktioniert dort nicht mehr; hier
    entweder das VS-Code-Pendant/aktuelle Community-Alternative für IDE 2.x
    verwenden, oder direkt den **OTA-Weg** unten (der ist IDE-Versions-
    unabhängig, da er über die mitgelieferten Kommandozeilen-Tools läuft).
- Für den **OTA-Weg**: Rechner im selben lokalen Netz (192.168.0.x) wie der
  Manager, Python 3 (für `espota.py`).
- Die zwei Werkzeuge, die der ESP32-Boardpaket-Installation beiliegen
  (Pfade variieren je nach Betriebssystem/Paketversion, Suche z. B. mit
  `find ~ -iname mklittlefs* 2>/dev/null` bzw. `find ~ -iname espota.py 2>/dev/null`):
  - `mklittlefs` — baut aus einem Ordner ein LittleFS-Binary-Image.
  - `espota.py` — lädt ein Binary-Image per WLAN auf ein ArduinoOTA-Gerät hoch.
  - Typische Fundorte:
    - Linux: `~/.arduino15/packages/esp32/tools/mklittlefs/<version>/mklittlefs`
      und `~/.arduino15/packages/esp32/hardware/esp32/<version>/tools/espota.py`
    - macOS: `~/Library/Arduino15/packages/esp32/...` (sonst analog)
    - Windows: `%LOCALAPPDATA%\Arduino15\packages\esp32\...`

## Manager-Netzwerkdaten (aus `xComDef6_3.h`, für die Befehle unten)

- **IP:** `192.168.0.180` (festes letztes Oktett `180`, siehe `device[Manager]`)
- **OTA-Hostname:** `Manager_Dev` (aus `device[Manager].Name`, per mDNS
  ggf. auch als `Manager_Dev.local` erreichbar)
- **OTA-Port:** `3232` (ArduinoOTA-Standardport, nicht überschrieben)
- **OTA-Passwort:** **keins gesetzt** (`setUpOTA()` ruft kein
  `setPassword()`/`setPasswordHash()` auf) — jeder im lokalen Netz kann
  OTA-Updates an den Manager schicken. Das ist ein bekannter offener Punkt
  (siehe `MapConcept.md`, „Review-Punkt 10“), nicht neu durch dieses
  Konzept. Bis das nachgerüstet ist: lokales Netz entsprechend absichern.

## Schritt 1 — CSV bearbeiten

`Controller/Manager6_3_0/data/noshot.csv` bzw. `.../data/rasen.csv` öffnen:

```
# NoShotZone v3  crc=0x1234ABCD  units=mm  frame=world
# Polygon(e) der ERLAUBTEN Schusszone: innerhalb = Feuern erlaubt, ausserhalb = No-Shot.
# Mehrere Ringe durch Leerzeile getrennt (Loecher = No-Shot-Inseln).
# x,y  (Welt, Millimeter, ganzzahlig)
115,2
132,1301
...
```

- Punkte ändern/hinzufügen/löschen: eine Zeile `x,y` pro Punkt, Welt-mm,
  ganzzahlig.
- Mehrere Ringe (Löcher = No-Shot-Inseln): durch eine **Leerzeile** trennen.
- **Version im Header von Hand hochzählen** (`v3` → `v4` o. ä.) — anders als
  beim VPS-Weg gibt es hier keinen Annahme-Code, der das automatisch
  erledigt. Wird die Version vergessen, erkennen die Sensoren die Änderung
  trotzdem (sie vergleichen zusätzlich den CRC — siehe unten), aber für die
  Nachvollziehbarkeit lieber hochzählen.
- Das `crc=`-Feld im Header ist **nur informativ** (siehe Kommentar in der
  Datei) — es muss nicht von Hand neu berechnet werden, es wird von
  niemandem ausgewertet. Der wirklich fürs Protokoll verwendete CRC
  (`fileCrc`) wird vom Manager beim Booten aus der kompletten Datei
  berechnet (`mapFileInfo`/`crc32Bytes`).
- Jeder Ring braucht **mindestens 3 Punkte**, sonst wird er beim Laden auf
  den Sensoren ignoriert (`loadNoShot`).
- **`Map/backup/<typ>.csv` NICHT editieren** — wirkungslos, wird beim
  nächsten VPS-Sync ohnehin überschrieben.

## Schritt 2A — USB-Weg (seriell, Kabel)

1. Manager-Board per USB anschließen, Sketch-Ordner `Manager6_3_0` in der
   Arduino IDE öffnen (damit Board/Port bereits eingestellt sind).
2. Seriellen Monitor **schließen**, falls offen (blockiert sonst den Port).
3. Tools-Menü → **„ESP32 Sketch Data Upload“** (IDE 1.8.x + Plugin, s. o.).
   Das Tool baut automatisch aus `Controller/Manager6_3_0/data/` ein neues
   LittleFS-Image und schreibt es auf die Datenpartition — **der Sketch-
   Code selbst wird dabei nicht angefasst**, ein Neukompilieren/Hochladen
   des Programms ist für eine reine Kartenänderung nicht nötig.
4. Nach Abschluss startet der Manager neu. Seriellen Monitor öffnen
   (115200 Baud) und die Boot-Zeilen prüfen:
   ```
   noshot map: v4 crc=0x...
   rasen map: v2 crc=0x...
   ```
   Fehlt eine Zeile bzw. steht „keine Karte im LittleFS“ da, ist beim
   Image-Bau/Upload etwas schiefgelaufen — Schritt 3 wiederholen.

## Schritt 2B — OTA-Weg (WLAN, kein Kabel)

Nur nötig/sinnvoll, wenn der Manager physisch schwer erreichbar ist oder
kein USB-Kabel griffbereit ist. Beispielbefehle (Pfade zu `mklittlefs`
und `espota.py` je nach gefundenem Pfad anpassen, siehe „Voraussetzungen“):

1. LittleFS-Image aus dem `data/`-Ordner bauen. Die Partitionsgröße muss
   zur im Board-Menü gewählten „Partition Scheme“ passen — steht in der
   `.csv`-Partitionstabelle des gewählten Schemas (Arduino IDE: Tools →
   Partition Scheme zeigt den Namen, die Größe findet man in der
   zugehörigen Partitionstabellen-Datei des Boardpakets, z. B.
   `default.csv`/`min_spiffs.csv` — Spalte `size` der `spiffs`-Zeile, in
   Hex, z. B. `0x160000`):
   ```bash
   mklittlefs -c Controller/Manager6_3_0/data -b 4096 -p 256 -s 0x160000 littlefs.bin
   ```
2. Image per OTA auf den Manager schreiben (Ziel: Filesystem-Partition,
   nicht Firmware — deshalb `--spiffs`):
   ```bash
   python3 espota.py -i 192.168.0.180 -p 3232 -f littlefs.bin --spiffs
   ```
   (`-i` = Manager-IP; `-r` statt `-i ...` würde den mDNS-Hostnamen
   `Manager_Dev` auflösen, falls mDNS im Netz funktioniert — je nach
   `espota.py`-Version heißt die Option leicht anders, `--help` prüfen.)
3. Der Manager startet nach erfolgreichem OTA-Update automatisch neu
   (Standardverhalten von `ArduinoOTA`, gilt für Firmware- **und**
   Filesystem-Updates).
4. Verifizieren: entweder per USB-Serial-Monitor (Boot-Zeilen wie oben)
   **oder** im VPS-Debug-Fenster beobachten — der Manager loggt seinen
   Kartenstand beim Boot zusätzlich als `mapInfo`-Announce, die
   angeschlossenen Sensoren sollten innerhalb weniger Sekunden
   reagieren (`sendUdpTextln`-Meldungen „Karte …: lade vom Manager …“ im
   VPS-Debug-Fenster).

## Danach: verteilt sich das automatisch an die Sensoren?

Ja. Seit dem Boot-Announce (`gwAnnounceMaps()`, in `ensureMaps()`)
broadcastet der Manager nach jedem Neustart seinen aktuellen Kartenstand
per Multicast — Lidar/Radar vergleichen das mit ihrer lokalen Version und
laden bei Abweichung automatisch neu (kein manueller Schritt an den
Sensoren nötig). Sollte der Announce verlorengehen (UDP, kein Resend):
spätestens nach `MAP_RECHECK_MS` (stündliches Fangnetz, siehe
`Radar6_3_0.ino`/`CF_LidarC1/C1Lidar6_3_0/hwProc.ino`) wird trotzdem
nachgeladen.

## Danach: meldet sich das an den VPS zurück?

Ja, automatisch — der Manager schickt seinen Kartenstand (Version/CRC)
leichtgewichtig mit jedem `/ingest`-Push mit. Weicht das vom VPS-Spiegel
ab (z. B. weil die Änderung per Notweg und nicht über den Editor kam),
fordert der VPS über die `/commands`-Queue einen Re-Sync an
(`cmdMapPush`); der Manager postet daraufhin die volle CSV an
`/mapsync`, der VPS aktualisiert seinen Spiegel und committet sie (bei
gesetztem `GITHUB_MAP_TOKEN`) automatisch zurück nach
`Controller/Manager6_3_0/data/<typ>.csv` **und** `Map/backup/<typ>.csv` —
der Notweg schließt sich also von selbst wieder mit dem Repo kurz.

## Troubleshooting

| Symptom | Wahrscheinliche Ursache |
|---|---|
| `LittleFS mount FAILED` im Boot-Log | Image-Upload fehlgeschlagen/abgebrochen — Schritt 2A/2B wiederholen. |
| „keine Karte im LittleFS“ trotz Upload | Datei falsch benannt/falscher Ordner beim Image-Bau (`data/noshot.csv` bzw. `data/rasen.csv`, exakter Dateiname!). |
| Sensor meldet weiter alte Version | Bis zu `MAP_RECHECK_MS` (1 h) abwarten oder Sensor neu starten (lädt beim eigenen Boot sofort neu). |
| OTA bricht mit `Auth Failed` ab | Unwahrscheinlich (kein Passwort gesetzt) — eher Netzwerk-/Portproblem (3232 blockiert?) prüfen. |
| `espota.py` findet den Host nicht | IP statt Hostname probieren (`-i 192.168.0.180`), WLAN-Erreichbarkeit prüfen (`ping 192.168.0.180`). |

## Welches Programm nutzt welche Karte?

| Programm | Karte | Wofür | Pfad auf dem Gerät |
|---|---|---|---|
| `Controller/Manager6_3_0` (Manager) | **beide** (Master) | hält beide Karten im LittleFS, verteilt sie per `mapRequest`/`mapInfo`/`mapChunk` | `/noshot.csv`, `/rasen.csv` |
| `CF_LidarC1/C1Lidar6_3_0` (LidarC1) | **NoShot** | Fire-Gating: ein Ziel wird nur als `catObserved` gemeldet, wenn `insideNoShot()` true liefert (`hwProc.ino`: `serviceNoShotMap`/`finishInit`) | `/noshot.csv` |
| `Radar_HKL/Radar6_3_0` (HLK-Radar) | **Rasen** | Relevanzfilter: Ziele außerhalb des Rasens (Nachbargrundstück/Straße) werden nicht gemeldet (`serviceRasenMap`) | `/rasen.csv` |
| `CF3_LD06_Lidar/LD06_6_3_0` | keine | reiner 360°-Bewegungssensor ohne Welt-Pose-Filterung | — |
| alle anderen (`PowerActor*`, `Displays/*`, `LaserMarker`, `CatIdentifier`, `CatCam`, `Button`, `Simulator`) | keine | konsumieren keine Polygon-Karte direkt | — |

Quelle/Details: `Dokumentation_6_3.md`, Kapitel 4.2 „Karten-Verteilung“.
