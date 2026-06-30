# GesamtKonzept CatFinder

CatFinder ist ein System aus mehreren einzelnen Geräten. Der primäre Zweck besteht darin, eine Katze auf dem Rasen zu detektieren und zu lokalisieren. Um zu verhindern, dass sie auf den Rasen kotet, kann ein Aktor einen Wasserstrahl in ihre Richtung senden.

Das System besteht aus mehreren Komponenten und wird stetig erweitert. Bisher handelt es sich dabei um Funktionseinheiten, die eine oder mehrere Funktionen übernehmen und lokal von einem ESP32 gesteuert werden. Die ESP32 werden per OTA mit Arduino programmiert.

## Arduino-Programmarchitektur

### Global

Globale Variablen, Typdefinitionen sowie Prozeduren, die von allen Programmen verwendet werden, befinden sich in `xComDef.h` und `xComProc.h`. Diese Dateien sind Teil des Controller/Managers und werden in allen Programmen eingebunden. Sie sind per Symlink in der Arduino-Library-Sektion verfügbar. So können sie im Manager bearbeitet werden und stehen gleichzeitig allen Programmen zur Verfügung.

### Support

Alle Programme definieren die Hardware und Konstanten in `hwDef.h`. Initialisierungsprozeduren, Hardwareprozeduren und Support-Prozeduren befinden sich in `hwProc.ino`. Spezialprozeduren, zum Beispiel für Erkennung oder Mapping, werden ebenfalls als separate `.ino`-Dateien eingebunden.

### Betriebsprogramme

Die Haupt- bzw. Betriebsprogramme können dadurch möglichst schlank gehalten werden. Dort werden im Wesentlichen Aktionen ausgelöst und es wird auf Ereignisse oder empfangene Nachrichten reagiert.

### Kommunikation

Alle Programme sind OTA-fähig und binden `Credentials.h` für die WiFi-Informationen ein. Die Kommunikation erfolgt über UDP, hauptsächlich per Broadcast oder Unicast sowie per Text-Multicast für Debug-Ausgaben.

In `xComDef`, in `stationDefinitions device`, sind alle Geräte definiert, die im System vorkommen können. Die IP-Adresse bzw. deren letztes Byte wird dort entweder vordefiniert oder vom Gerät eingetragen, sobald es das erste Mal sendet. Wie gesendet wird und welche Bedeutung die gesendeten Inhalte haben, wird ebenfalls in `xComDef` festgelegt. Das gilt auch für die Variablen, die alle Geräte verwenden.

## Gerätearten

Bisher gibt es folgende Gerätearten:

### Aktoren

Aktoren können einen Wasserstrahl ausrichten sowie ein- und ausschalten. Häufig verfügen sie über LED-Pixel als Indikatoren, Knöpfe usw. An ihrem Drehturm können meist Sensoren, zum Beispiel Lidar, US-Meter oder IR-Detektoren, sowie Ziellaser oder andere Indikatoren befestigt werden. Damit können Aktoren auch selbst als Sensoren arbeiten oder die Zielinformation überprüfen.

### Sensoren

Die meisten Sensoren sind Distanz- und Bewegungssensoren, zum Beispiel Lidar oder Radar. Sie können etwa eine Katze detektieren oder etwas messen und bei einer Detektion eine Broadcast-Meldung senden. Weitere Sensoren, zum Beispiel für Regen, sind ebenfalls denkbar.

### Displays

Weiter gibt es Displays, teilweise mit Bedienelementen oder On/Off-Schaltern, um zum Beispiel das System scharf zu schalten.

### Spezialgeräte

Es gibt auch einfache Geräte, zum Beispiel einen Laser-Marker oder einen Simulator. Eine spezielle Rolle nimmt der Controller/Manager ein. Eigentlich ist er nicht zwingend notwendig, da das ganze Gerätenetzwerk ohne zentrale Steuerung auskommt. Der Manager hilft bei der Programmentwicklung, insbesondere bei globalen Elementen, und könnte in Zukunft als Informationsressourcenspeicher genutzt werden. Im Moment läuft der normale Betrieb ohne ihn.

## Ausrichtung und Orientierung

Gegenwärtig werden im System kartesische und Polarkoordinaten gleichzeitig und redundant verwendet. Der Kreis im Polarsystem hat 4096 Ticks, weil auch die seriellen Servos mit 4096 arbeiten. Diese Koordinatensysteme sind immer auf die jeweilige Quelle bezogen. Damit der Aktor mit diesen Koordinaten arbeiten kann, muss der Sensor fix mit ihm verbunden sein; die Tower-Achse wird damit quasi als Ursprung verwendet.

## Aufgabe: Globales Koordinatensystem ermöglichen

Das System soll die Möglichkeit erhalten, mit einem globalen Koordinatensystem zu arbeiten und den Einsatz mehrerer Aktoren zu erlauben. Wenn Aktoren nicht mit einem globalen Koordinatensystem arbeiten, verwenden sie ein relatives lokales Koordinatensystem. Das heisst, die Koordinaten stimmen für Sensoren, die normalerweise fest mit dem jeweiligen Aktor verbunden sind und damit eine eigene Koordinatensystemgruppe bilden.

Die Skizze des Gesamtzusammenhangs sieht wie folgt aus: Sensoren und Aktoren verfügen über Prozeduren, die es ihnen erlauben, ihre Position bezüglich eines globalen Koordinatensystems festzustellen. Das ist Gegenstand einer späteren Aufgabe und wird in dieser Aufgabe nicht realisiert.

Der normale Startablauf für Aktoren und Sensoren ist dann folgender:

1. Globale Positionskoordinaten und Ausrichtung aus dem nichtflüchtigen Speicher auslesen.
2. Quickcheck, ob die Pose stimmt. Wenn nicht, wird die Pose neu bestimmt.

Für Sensoren und Aktoren gilt dann: Bei Detektionsereignissen und Messungen werden die relativen Koordinaten, polar und kartesisch, übermittelt. Falls eine gültige Positionsbestimmung vorliegt, werden zusätzlich die globalen Koordinaten übermittelt. Globale Koordinaten sind immer kartesisch.

Wenn ein Aktor ein Detektionsereignis empfängt, prüft er, ob globale Koordinaten übermittelt wurden und ob er selbst eine gültige Lokalisationsbestimmung hat. Wenn beides erfüllt ist, verwendet er die globalen Koordinaten und prozessiert sie entsprechend. Falls nicht, prüft er, ob die relativen Koordinaten aus derselben relativen Koordinatengruppe stammen, zu der auch er gehört. Wenn dies der Fall ist, verwendet er diese Koordinaten und prozessiert sie. Falls weder das eine noch das andere zutrifft, ignoriert er die Meldung.

Ein Spezialfall sind Displays und Anzeigen. Diese sind entweder auf eine relative Koordinatengruppe eingestellt und prozessieren diese, oder sie sind auf globale Koordinaten eingestellt. Wenn sie in diesem Fall eine Meldung erhalten, die nur relative Koordinaten enthält, und sie darauf reagieren müssten, prüfen sie, ob ein Mitglied der relativen Koordinatengruppe eine valide Positionsbestimmung hat. Falls ja, fragen sie diese an und rechnen die Koordinaten um.

Das ist die grobe Skizze des Gesamtzusammenhangs. In dieser Aufgabe ist es nicht Gegenstand, die spezifischen funktionalen Prozeduren zu erzeugen; das erfolgt später in eigenen Aufgaben. In dieser Aufgabe sollen die Datenstruktur und die Common-Prozeduren des Systems in Version 6_3 erweitert werden, um dies zu ermöglichen.

### Konkret

1. Jedes Gerät des Systems soll über Variablen für die globalen Koordinaten und seine Ausrichtung sowie über ein Flag `validWorldPose` verfügen. Nach dem Booten ist `validWorldPose = false`. Auch wenn vermutlich nur Sensoren und Aktoren diese Information verwenden, sollen alle Geräte sie haben, damit mit globalen Prozeduren gearbeitet werden kann.

2. Die Datenstruktur `stationDefinitions device` in `xComDef`, die im Wesentlichen das Geräteverzeichnis darstellt, soll um einen Eintrag für die relative Koordinatengruppe erweitert werden. Zurzeit gibt es eine Gruppe für den Aktor PA2, zu dem der Aktor, der CompactDome und LD06 gehören, und eine Gruppe für `Actor1_1` mit dem MiniDome. Alle übrigen Geräte gehören zurzeit keiner relativen Koordinatengruppe an.

3. Die Struktur bzw. Payload soll so erweitert werden, dass neben den relativen Koordinaten auch die World-Koordinaten übermittelt werden. Wenn der Sensor keine valide Pose hat, werden sie als 0,0 übermittelt.

4. Die Kommunikationsstruktur soll zur Abfrage erweitert werden, ob `validWorldPose` vorliegt, sowie zur Abfrage der lokalen Koordinaten, falls `validWorldPose` vorliegt.

5. Die globalen Prozeduren in `xComProc` sollen um die Umrechnung von lokalen Koordinaten in Weltkoordinaten und umgekehrt erweitert werden.

6. Die globalen Prozeduren in `xComProc` sollen um Prozeduren zum Speichern und Lesen der Ist-Pose im nichtflüchtigen Speicher erweitert werden.

### Umsetzung, konkretisiert — Stand 6_3

Diese Aufgabe wurde in den gemeinsamen Dateien `xComDef6_3.h` und `xComProc6_3.h` umgesetzt. Die folgenden Festlegungen gelten als Teil der Aufgabe bzw. Dokumentation:

- **Einheiten:** Weltkoordinaten sind immer kartesisch in **mm** (`int32`, konsistent mit `posPayload.x/y`). Die Ausrichtung (`heading`) wird in **PA-Einheiten 0..4096** angegeben, konsistent mit `angle`. `0` bedeutet: Die lokalen Achsen sind welt-ausgerichtet.
- **Zu 1) Pose-Variablen pro Gerät:** `struct worldPose { int32 worldX; int32 worldY; float heading; bool validWorldPose; }` mit globaler Instanz `worldPose myPose` in `xComDef6_3.h`. Damit besitzt jedes Programm die Variablen. `validWorldPose` ist nach dem Booten immer `false`.
- **Zu 2) Relative Koordinatengruppe:** `stationDefinitions` erhält das Feld `byte group`. Defines: `groupNone(0)`, `groupPA2(1)` = {PA2i, CompactDome, LD06}, `groupPA1_1(2)` = {PA1_1, MiniDome}. Alle übrigen Geräte = `groupNone`.
- **Zu 3) Payload-Erweiterung:** `posPayload` erhält `int32 worldX`, `int32 worldY` und ein explizites Flag `uint8 worldValid` (1 = Weltkoordinaten gültig; 0,0 wäre sonst nicht vom Ursprung unterscheidbar). Die Grösse steigt von 25 auf 34 Bytes. Dadurch müssen alle 6_3-Geräte gemeinsam neu geflasht werden.
- **Zu 4) Kommunikationsstruktur:** neue msgCodes `poseRequest(6)` (ohne Payload — „melde deine Welt-Pose“) und `poseReport(7)` mit `worldPosePayload { uint8 validWorldPose; int32 worldX; int32 worldY; float heading; }`. Damit lässt sich abfragen, ob ein Gerät eine `validWorldPose` hat. Der Anfrager kann mit `worldToLocal()` selbst in lokale Koordinaten umrechnen.
- **Zu 5) Transformation:** `localToWorld()` / `worldToLocal()` in `xComProc6_3.h` (2D-Translation + Rotation um `heading`).
- **Zu 6) Persistenz:** `savePose()` / `loadPose()` über NVS (Preferences-Namespace `"pose"`). `loadPose()` lädt nur Koordinaten und Ausrichtung; `validWorldPose` bleibt nach dem Booten `false` (Quickcheck bestätigt später).

Bewusst **nicht** Teil dieser Aufgabe, sondern eigene Folgeaufgaben: tatsächliche Positionsbestimmung bzw. Quickcheck, das Füllen von `worldX/worldY` durch weltfähige Sensoren, die Empfangs-/Feuerlogik der Aktoren und die Display-Sonderfall-Logik.

## Aufgabe: No-Shot-Karte über den Manager verteilen

Das System braucht eine **Schusszonen-Karte**, im Sprachgebrauch weiter **No-Shot-Karte** genannt. Die Karte beschreibt als geschlossene Weltfläche den **erlaubten Bereich**, also den Rasen. Dieser ist bewusst etwas **enger** gefasst als der reale Rasen, weil teilweise Bewuchs über den Rand ragt. Es gilt: **Innerhalb** der Karte ist Feuern erlaubt, **ausserhalb** ist No-Shot.

Die Karte wird zentral auf dem **Controller/Manager** gehalten und von dort an die Sensoren bzw. weltfähigen Geräte verteilt, damit diese die In/Out-Entscheidung autonom und feuerschnell treffen können. Bereits getroffene Architekturentscheidungen:

- **Format:** Polygon als **CSV in Welt-mm** (ganzzahlig, konsistent mit `worldX/worldY`), Kommentar-Header mit Versions-/CRC-Kennung; mehrere Teilflächen oder Löcher werden durch Leerzeilen getrennt. Begründung sowie Laufzeit-/Point-in-Polygon-Überlegungen siehe Projektnotiz `no-shot-map-format`.
- **Ablage:** auf Manager und Sensor jeweils im **LittleFS** (nicht SPIFFS). Auf den Manager kommt die Karte per OTA-Filesystem-Upload; der Sensor lädt sie vom Manager.
- **Abgrenzung:** Die VPS-Lokalisierungskarte (`Map/*.csv`) ist eine andere Karte und kein Teil dieser Aufgabe. Der VPS zieht sie direkt aus dem Repo.

### Konkret

1. **Manager:** No-Shot-CSV im LittleFS ablegen und pflegen (Upload per OTA-Filesystem-Image). Versions-/CRC-Kennung aus dem Header bereitstellen.

2. **xComDef erweitern:** neue msgCodes und Payload-Structs für die Kartenabfrage und -übertragung, zum Beispiel `mapRequest` mit Kartentyp, `mapInfo` mit Typ/Version/CRC/Gesamtlänge/Chunk-Anzahl sowie `mapChunk` mit Chunk-Index und Daten. Ein Kartentyp-Enum ist vorzusehen, zunächst `mapNoShot`, damit später weitere Kartentypen möglich sind.

3. **xComProc erweitern:** gemeinsame Prozeduren für die Senderseite, also Karte aus LittleFS lesen und gechunkt versenden, sowie für die Empfängerseite, also Karte anfordern, Chunks sammeln, CRC/Version prüfen und in das eigene LittleFS schreiben. So kann jedes weltfähige Gerät die Karte generisch vom Manager beziehen.

4. **Sensor-Seite (Abfrage):** Beim Booten oder bei abweichender Version fordert der Sensor die No-Shot-Karte vom Manager an und cached sie lokal im LittleFS. Die Version wird per Quickcheck gegen den Manager geprüft.

5. **Transport (entschieden):** über das bestehende **UDP-xCom-Protokoll**, gechunkt und stilkonsistent über die neuen msgCodes in Punkt 2/3. Es wird kein separater HTTP-Server verwendet.

6. **Nicht Teil dieser Aufgabe** (Folgeaufgaben): der eigentliche Point-in-Polygon-Test (In/Out) und die daraus folgende Feuer-/Melde-Konsequenz im Sensor/Aktor (siehe Projektnotiz `no-shot-map-format`).

## Aufgabe: Sensor mit absoluten Koordinaten hinzufügen

Ein Lidar-Sensor soll als neuer Sensor mit Weltkoordinaten zum Projekt CatFinder hinzugefügt werden. Sensor und Teile des Konzepts sind aus `C:\Users\stefan\Documents\Arduino\Lidar_C1_Prog` und `C:\Users\stefan\Documents\Arduino\Lidar_C1_Prog\Position_estimate` bekannt. Der Ablauf, also wer den Prozess startet und wo er läuft, ist jedoch unterschiedlich.

### Grobkonzept

Der Sensor soll seine eigene Position mithilfe geeigneter Programme ermitteln, die in Python auf einem VPS-Server laufen. Das Verfahren entspricht demjenigen bei `PositionEstimate`. Die IP des VPS wird durch Einbindung von `Credentials.h` in der Form `IPAddress ipVPS(xxx.xxx.xxx.xxx);` bereitgestellt.

Alles, was auf dem VPS-Server läuft, soll in Docker laufen, damit der Container im Repository gespeichert und auf andere Maschinen portiert werden kann. Die Datei `RasenKarte.csv` im Verzeichnis `Map` muss bzw. kann vom VPS aus dem CatFinder-Repository (`https://github.com/smily77/CatFind`) geladen werden. Das soll sicherstellen, dass bei Änderungen nichts am VPS gemacht werden muss.

Der Ablauf ist folgender:

1. Beim Booten des Sensors Positions- und Ausrichtungsdaten aus dem NVS lesen.
2. Mit dem VPS prüfen, ob die Daten plausibel sind.
3. Wenn nicht, mit dem VPS die wahrscheinlichste Position und Ausrichtung auf dem Rasen bestimmen.
4. `validWorldPose` setzen.
5. Wenn `validWorldPose` gesetzt ist, die No-Shot-Karte aus dem Speicher lesen und validieren.
6. Wenn keine aktuelle No-Shot-Karte vorhanden ist, eine neue Karte vom Manager lesen.
7. Wenn der Manager nicht verfügbar ist, die alte No-Shot-Karte verwenden.
8. Wenn die Position klar ist, mit der Überwachung beginnen.
9. Bei jedem Treffer mit der No-Shot-Karte prüfen, ob der Treffer im schiessbaren Bereich liegt. Wenn ja, `catObserved` broadcasten.

Weiteres:

- Die Status-Pixel, 2 Stück bei `Lidar_C1`, sollen Auskunft über den Initialisierungsstatus geben und leuchten, wenn eine Detektion im schiessbaren Bereich erfolgt ist, ähnlich wie bisher.
- OTA und alle anderen CatFinder-Konzepte gelten natürlich ebenfalls.
- Nach Abschluss der Initialisierung wird ein sehr knapper Text-Multicast über den Status gesendet.

### Festlegungen, Umsetzung

- **Gerät:** neuer Sketch `C1Lidar6_3_0` (Ordner `CF_LidarC1/`), RPLidar C1 (Serial1, 460800), 2× WS2812. Device-DB-Eintrag `LidarC1` (ID 17), Typ `Lidar`, **DHCP** (keine feste IP nötig; IP wird per HB gelernt), `group = groupNone` (weltfähig, keine relative Gruppe). Flash per **OTA**.
- **Sensor↔VPS:** **HTTP-POST** vom Sensor an den VPS (`ipVPS` aus `Credentials.h` als `IPAddress ipVPS(46,225,81,240);`), Body = 360-Bin-Scan (mm), Antwort = Pose-JSON (`x_mm,y_mm,heading_deg,mirror,confidence,inlier_ratio`). Ausgehend → kein NAT-Port. **Offener Endpoint, kein Token.**
- **VPS:** Docker-Container `VPS/localizer/` im Repo (HTTP-Dienst, portiert aus `lidar_localize.py`), lädt `Map/RasenKarte.csv` aus dem GitHub-Repo (raw) beim Start bzw. periodisch. Kartenänderungen erfordern keinen VPS-Eingriff.
- **Scan-Quelle für die Lokalisierung:** der nach 20 s gelernte 360-Bin-Hintergrund (ortsunabhängig; **keine Wand/Nische mehr nötig**). Wand- und Landmark-Modell der alten Firmware entfallen; der Perimeter-/Hintergrund-Detektor bleibt und arbeitet rundum.
- **Plausibilitätsprüfung:** Die NVS-Pose wird gegen die globale VPS-Pose verglichen. Wenn sie übereinstimmen (Toleranz) und die Konfidenz hoch ist, wird `validWorldPose=true` gesetzt und die NVS-Pose behalten. Andernfalls wird die VPS-Pose übernommen und im NVS gespeichert.
- **No-Shot/Point-in-Polygon:** generischer Loader + In/Out-Test (innerhalb = schiessbar) als gemeinsame Prozedur in `xComProc6_3.h`; Karte aus LittleFS, sonst per `requestMap` vom Manager (Bausteine bereits vorhanden), sonst alte Karte.
- **catObserved:** bei Treffer im schiessbaren Bereich Broadcast mit relativen Koordinaten (x/y, radius/angle) **und** Weltkoordinaten (`worldX/worldY`, `worldValid=1`).
- **Status:** Pixel zeigen Init-Phasen (WiFi/Kalibrierung/Lokalisierung/Karte) und leuchten bei Detektion im schiessbaren Bereich; nach Init ein knapper Text-Multicast (`sendUdpTextln`, Port 8300).

## Aufgabe: Treffervisualisierung

### Grobkonzept

Der VPS stellt einen Webserver bereit, auf dem die `catObserved`-Events angezeigt werden können. Die Informationen dafür sendet der Master an den VPS. Zu beachten ist: Lokal läuft das CatFinder-Netzwerk auch dann weiter, wenn der Master nicht zur Verfügung steht. Ausnahme ist das Laden der No-Shot-Karte.

Anzeige des VPS-Webservers:

1. Ein Fenster zeigt die letzten Systemereignisse scrollend an. Es geht dabei nicht um Funktionen wie `catObserved`, sondern um Statusinformationen, zum Beispiel aus der Initialisierung, die als Debug-Text gebroadcastet werden. Dieses kleine Fenster ist immer auf dem Bildschirm sichtbar.
2. Eine kleine Anzeige zeigt, welche Geräte in den letzten 1–3 Minuten einen HB gesendet haben. Diese Anzeige bleibt ebenfalls immer auf dem Bildschirm sichtbar.
3. Die folgenden Anzeigen können umgeschaltet werden:
   - **3a. Ereignisliste:** Eine scrollbare Liste mit allen `catObserved`-Events, zusammengefasst auf maximal einen Eintrag pro Minute. Der Eintrag enthält die Zeit und die Sensoren, die das Ereignis gemeldet haben. Die ID-Nummer des Sensors genügt.
   - **3b. Weltkarte:** Karte mit World-Koordinaten (`RasenKarte`), auf der alle `catObserved` eingetragen werden. Jeder Sensor erhält eine andere Farbe. Radarsensoren können bis zu 3 Ziele verfolgen; diese sollten ebenfalls eigene Farben erhalten. Die Karte hat einen Reset-Button. Alle `catObserved` werden kumuliert, bis der Reset-Button gedrückt wird.
   - **3c. Relative Karte Gruppe 1:** Karte wie 3b, aber in relativen Koordinaten der Koordinatengruppe 1.
   - **3d. Relative Karte Gruppe 2:** Karte wie 3b, aber in relativen Koordinaten der Koordinatengruppe 2.
   - **3e. Relative Karte Gruppe 3:** Karte wie 3b, aber in relativen Koordinaten der Koordinatengruppe 3. Diese Gruppe gibt es noch nicht, wird es aber noch geben.

Der Bildschirm zeigt die Debug-Meldungen und die HB-Liste immer an. Die Ereignisliste bzw. die Karten aus 3a–3e können durchgeschaltet werden.

### Festlegungen, Umsetzung

- **Master als Gateway:** Der Manager lauscht ohnehin auf Multicast (catObserved, HB) und Text-Multicast (Debug, Port 8300). Er puffert diese Daten und schickt sie **gebündelt per HTTP-POST** an den VPS (`ipVPS:80/ingest`, ca. alle 1,5 s; Burst-Schutz mit Begrenzung pro Push). Fällt der Master aus, läuft das lokale Netz weiter; nur die Visualisierung pausiert. Pro Event überträgt der Master `sender, sensor, worldX/Y/valid, x/y` **und** `group` (= `device[sender].group`). Damit kann der VPS Weltkarten (3b) und relative Karten (3c–3e) zeichnen. Den Zeitstempel setzt der VPS beim Empfang; der Master braucht keine NTP-Zeit.
- **VPS-Webserver:** zweiter Docker-Container `VPS/dashboard/` auf **Port 80** (extern erreichbar als `http://<VPS-IP>/`). Single-Page-UI mit Debug-Fenster und HB-Liste immer sichtbar; umschaltbar zwischen Liste 3a, Weltkarte 3b und Gruppenkarten 3c–3e; Reset-Button. Der State liegt im RAM. Events kumulieren bis zum Reset und gehen bei Container-Neustart verloren. Die Weltkarte nutzt `Map/RasenKarte.csv` aus dem GitHub-Repo.
- **Farben:** je `(sender, sensor)`-Kombination eine eigene Farbe. Radar bis zu 3 Ziele → 3 Farben.
- **Liste 3a:** `catObserved` werden pro Minute zu einem Eintrag zusammengefasst (Zeit + beteiligte Sensor-IDs).
- **Master-Flash:** über COM6 (USB).

## Aufgabe: Welt-Pose eines Sensors per Co-Observation kalibrieren (Radar)

### Grobkonzept

Sensoren mit 360°- oder beweglichem Lidar können ihre Welt-Pose selbst bestimmen (VPS-Lokalisierung), Radar-Sensoren nicht. Ein Radar oder allgemein ein nicht selbst lokalisierender Sensor soll seine Welt-Pose dadurch erhalten, dass er **gleichzeitig mit einem welt-posierten Lidar dieselbe laufende Person beobachtet**. Aus den beiden Beobachtungsbahnen wird die starre Transformation vom Radar-Frame in die Welt geschätzt: Translation + Heading + Drehsinn = eine `worldPose`. Das ist klassische Extrinsik-Kalibrierung über ein gemeinsames bewegtes Ziel (Trajektorien-Registrierung).

### Prinzip

Ein Lidar mit gültiger Welt-Pose liefert die Bahn in Weltkoordinaten `p_welt`, das Radar dieselbe Bahn im Eigenframe `p_radar`. Gesucht ist:

```text
p_welt = R(heading)·(mirror·p_radar) + (tx,ty)
```

Mit genügend korrespondierenden Punktepaaren ist dies per Least-Squares-Registrierung (Umeyama/Kabsch) + RANSAC lösbar.

### Ablauf, knopfgesteuert im Radar-Sketch

1. Voraussetzung: Ein Lidar mit gültiger Welt-Pose broadcastet `catObserved` mit `worldValid=1`; das Radar läuft.
2. Knopfdruck am Radar → Kalibriermodus für ca. 30–60 s; der Status-Pixel zeigt dies an.
3. Das Radar sammelt parallel:
   - eigene Detektionen (Relativ-`x/y`, in Reihenfolge),
   - die `catObserved` des Lidars vom Bus (Welt-`x/y`).
4. Eine Person läuft eine **kurvige** Bahn im gemeinsamen Sichtfeld.
5. Das Radar sendet beide Punktzüge an den VPS (neuer Endpunkt, z.B. `/calibrate`).
6. Der VPS führt Trajektorien-Registrierung (ICP-artig) + RANSAC + Umeyama durch und liefert `tx, ty, heading, mirror, confidence`.
7. Das Radar übernimmt die Pose mit Quality-Gate, wie beim Lidar: `savePose`, `validWorldPose=true`, kurzer Status-Multicast. Danach füllt das Radar bei eigenen Detektionen auch `worldX/worldY` (`worldValid=1`).

### Festlegungen / wichtige Punkte

- **Korrespondenz über die Bahnform, nicht über die Zeit:** Der Header-`timeStamp` ist `time_t` in **Sekunden** und damit bei Gehtempo zu grob für eine zeitliche Zuordnung. Daher wird Trajektorien-Matching verwendet, also das Ausrichten geordneter Punktzüge als Kurven. Eine mögliche spätere Verbesserung wäre ein Millisekunden-Feld. Das ist vermeidbar, wenn Trajektorien-Matching genügt; es braucht dann keinen Eingriff ins Wire-Format.
- **Eindeutigkeit kommt vom Laufweg:** Eine gerade Linie ist mehrdeutig, insbesondere bezüglich Translation entlang der Linie. Eine 2-D-strukturierte Bahn, zum Beispiel Kurve/L/Acht, ist nötig. Beinbreite und Messrauschen helfen dabei **nicht**; sie mitteln sich nur weg.
- **Wiederverwendung:** `posPayload` trägt bereits beides: Radar = Relativ-`x/y`, Lidar = Welt-`x/y` mit `worldValid`. Es braucht keine neue Datenstruktur. `worldPose`/`savePose`/`loadPose` (mirror-fähig), Quality-Gate, VPS-Muster, OTA, Status-Text und Knopf/`commandMsg` sind vorhanden.
- **Voraussetzungen/Grenzen:** überlappendes Sichtfeld, **eine** Person, gute Lidar-Pose (Garbage-in → Garbage-out) und genügend Punkte. Der VPS liefert eine Konfidenz; das Gate verhindert eine schlechte Pose.
- **Generalisierung:** nicht radar-spezifisch. Jeder nicht selbst lokalisierende Sensor kann über jeden welt-posierten Sensor kalibriert werden. Die Sammel-/Sende-/Übernahme-Logik gehört daher als gemeinsame Prozedur in `xComProc`, zum Beispiel `coObserveCalibrate(...)`. Der VPS-Endpunkt wird einmal umgesetzt und kann von Radar sowie von künftigen Aktoren mit Turm-Sensor usw. genutzt werden.
- **Offene Detailfragen vor Umsetzung:** VPS-Endpunkt-/Datenformat; Mindest-Bahnlänge/Konfidenzschwelle; Ziel-Assoziation, wenn das Radar bis zu 3 Targets liefert (RANSAC wählt die zur Lidar-Bahn passende Spur); ob die Lösung rein 2-D bleibt (`mirror` als ±1 wie beim Lidar).

### Umsetzung, konkretisiert — Stand 6_3

Umgesetzt in den gemeinsamen Dateien (`xComDef6_3.h`/`xComProc6_3.h`), im VPS-Localizer
(`VPS/localizer/`) und im Radar-Sketch (`Radar_HKL/Radar6_3_0`). Die Sammel-/Sende-/
Übernahme-Logik ist **generisch** (nicht radar-spezifisch) und steht jedem nicht selbst-
lokalisierenden Sensor zur Verfügung.

- **Auslösung — Knopf UND Auto UND Health-Check (entschieden):**
  - *Knopf/`commandMsg`:* neuer `cmdCalibrate(18)` (`info` = Fensterdauer in ms, 0 = Default
    45 s). Damit kann ein Button-/Display-Gerät das Fenster starten.
  - *Auto:* Hat das Gerät **keine** valide Pose und liegt anhaltend (≥ `AUTO_ARM_MS`)
    Co-Observation an — eigenes Target **und** welt-valide `catObserved` gleichzeitig —,
    startet das Fenster selbsttätig. Das Quality-Gate verhindert eine schlechte Pose.
  - *Health-Check/Re-Validierung:* Bei vorhandener Pose wird jede co-beobachtete Lidar-
    Welt-Detektion gegen die eigene (per `localToWorld` transformierte) Detektion geprüft
    (`coObserveCheck`). Anhaltendes Welt-Residuum > Gate ⇒ `validWorldPose=false` (danach
    greift der Auto-Trigger erneut).
- **Gegen alle Welt-Quellen — je Sender getrennt (entschieden):** Das Radar taggt die
  gesammelten Welt-Punkte mit der **Sender-ID** und sammelt pro Quelle einen eigenen
  Punktzug (`coCalState`, bis zu `CO_MAX_WORLD_SRC` Quellen, je `CO_MAX_WORLD_PTS`). Der VPS
  probiert **jede (Eigen-Spur × Welt-Quelle)-Kombination** durch und liefert die beste; so
  wird „gegen alle welt-validen Sensoren" erfüllt, ohne die „eine kohärente Bahn"-Bedingung
  je Kandidat zu verletzen. Die gewinnende Quelle steht als `source` in der Antwort.
- **Kein neues Wire-Format:** `posPayload` trägt Relativ- **und** Welt-Koordinaten samt
  `worldValid` bereits; die Trajektorien gehen per HTTP an den VPS, nicht über xCom.
- **VPS-Endpunkt `/calibrate` (einmal, von allen nutzbar):** Body
  `{radar_tracks:[[[x,y]…]…], world_tracks:[{id,pts:[[x,y]…]}…]}` (Eigen mm relativ,
  Welt mm). Registrierung = **ICP mit reihenfolge-erhaltender DTW-Korrespondenz** +
  Umeyama/Kabsch über `mirror=±1`; Antwort `tx_mm,ty_mm,heading_deg,mirror,confidence,
  inlier_ratio,source`. Die Reihenfolge-Korrespondenz (statt Nearest-Neighbour) löst die
  Drehsinn-Mehrdeutigkeit; eine **gerade Linie** bleibt mehrdeutig und wird per
  Linearitäts-Check nie `HOCH`.
- **Übernahme mit Quality-Gate:** `coCalibFinish` übernimmt die Pose nur bei `confidence==HOCH`
  **und** `inlier_ratio ≥ CALIB_MIN_INLIER`, dann `savePose` + `validWorldPose=true` + kurzer
  Status-Multicast. Danach füllt das Gerät bei eigenen Detektionen `worldX/worldY`
  (`worldValid=1`) via `fillWorld`/`localToWorld`.
- **Boot vertraut der NVS-Pose (Radar-Ausnahme zur „false nach Boot"-Regel):** Da das Radar
  sich nicht selbst lokalisieren kann, aber im Normalfall nicht bewegt wird, setzt es beim Boot
  bei vorhandener gespeicherter Pose direkt `validWorldPose=true` (statt jedes Mal neu zu
  kalibrieren). Der Health-Check ist sein „Quickcheck": ein mitbeobachtender welt-posierter
  Sensor verwirft eine inzwischen falsche Pose; danach greift der Auto-Trigger. Erkannt wird
  Drift im Bereich zwischen Gate (~0,6 m) und Assoziationsgrenze (~1,5 m) — größere Abweichungen
  gelten als „anderes Ziel" und brauchen eine manuelle Neukalibrierung per Knopf.
- **Gemeinsame Prozeduren in `xComProc6_3.h`:** `coCalState`, `coCalibBegin/FeedLocal/
  FeedWorld/Elapsed/Finish`, `vpsCalibrate` (unter `USE_VPS_CALIBRATE`), `fillWorld`,
  `coHealth`/`coObserveCheck`.
- **Radar-Sketch:** verarbeitet jetzt empfangene Nachrichten (vorher nur Senden) — `catObserved`
  vom Bus (Sammeln/Health-Check) und `commandMsg`. Build/Flash: **`esp32:esp32:pico32`** (M5Stack
  Pico), per OTA.
- **Knopf als eigenes Gerät:** Da das Radar selbst keinen Knopf hat, übernimmt ein kleines
  Touch-Remote die Bedienung per `commandMsg` (Unicast): `Displays/radarCalibrationButton/` auf
  CYD35 (basiert auf `Udisp6_3_0`), mit **zwei Touch-Buttons** — „KALIBRIEREN" (`cmdCalibrate`)
  und „POSE LÖSCHEN" (`cmdClearPose`, verwirft die gespeicherte Pose → erzwingt Neukalibrierung,
  nötig wenn das Radar bewegt wurde). Es zeigt die Status-Multicasts des Radars an. Siehe
  Dokumentation_6_3.md Kap. 5.11.

Bewusst **nicht** umgesetzt / als Grenzen dokumentiert: Die DTW-Registrierung erzwingt Matching
der Bahn-Endpunkte — stark versetzte Start-/Endzeitpunkte der beiden Sensoren verschlechtern den
Fit an den Rändern; eine gemeinsam und vollständig beobachtete, kurvige Bahn bleibt nötig. Die
Health-Check-Assoziation ist best-effort (Distanz-Gate gegen „anderes Ziel").
