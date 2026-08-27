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


## Aufgabe: Einstellung und Steuerung der CatFinder Elemente

### Grobkonzept
Alle Sensoren, Aktoren und zum Teil weitere Elemente führen aut. Aufgaben aus, senden einen HB oder führen Funktionen aus. Viele davon haben LED Pixel oder Anzeigen um ein visuelles Feedback zu geben. Diese Anzeigen sollen eigestellt werden können, so wie ebenso gewisse automatische Aufgaben (z.B. automatische Kalibration eines Radarsensors) und Funktion sollen ausgelöst werden können wie z.B. die Kalibration eines Radarsensors. Die Einstellungen sollen entweder im nicht flüchtigen Speicher (NVS) abgelegt werden oder einen Default-Wert für den start haben. Die Variablen für die Einstellungen werden in der entsprechenden hwDef.h des Elements (Sensor, Aktor,...) festgelegt. 
Die Bedienung der Einstellungen und Auslösung von Aktionen soll über den Touchscreen eines Displays oder den Webserver des VPS erfolgen. 

### Details - Geräteanzeige

#### Standardanzeige
Im Standardfall haben Elemente 4 Anzeige Ereignisse, die sie mit den LED-Pixel anzeigen (kann variieren je nach Elementtyp):
Anzeige-Ereignisse (Tabelle bezieht sich nur auf die Anzeige)
1. Initialisierung, stand des Boot und Initialisierung Vorganges: Immer eingeschalten
2. HB: Ein/-Ausschaltbar - Zustand im NVS gespeichert
3. CatObserved Ereignis: Ein/-Ausschaltbar - Zustand im NVS gespeichert  
4. Kalibrierung, OTA, usw. :  Immer eingeschalten

Spezialfall Manger:
1. HB Empfangen: Ein/-Ausschaltbar - Zustand im NVS gespeichert
2. CatObserved Empfangen: Ein/-Ausschaltbar - Zustand im NVS gespeichert

#### automatische Aufgaben
1. Automatische Übernahme/Kopieren der WorldPose, wenn ein anders Gerät im selben relativen Koordinatensystem eine gültige WorldPose hat:  Ein/-Ausschaltbar - Zustand im NVS gespeichert

Spezialfall Radar:
1. Automatische Kalibrierung: Ein/-Ausschaltbar - Zustand im NVS gespeichert

SpezialFall Lidar
1. Motor des Lidars ein und ausschalten: ein/ausschaltbar - default (also immer beim Booten) eingeschaltet

#### Auslösbare Funktionalitäten
1. WorldPose von einem anderen Gerät mit gültiger WorldPose im selben relativen Koordinatensystem kopieren

Spezialfall Radar:
1. Kalibrierung durchführen: Kann ausgelöst werden

### Details Steuerung und Anzeige

#### Displays 
Die Funktionen sollen über den Touchscreen der Displays bedient werden können. Auf einer Seite soll das Gerät (Aktive geräte) ausgewählt werden können und dann sollen die anwählbare Dinge dargestellt und bedient werden können. Das ganze muss autoskaliert werden, da nicht alle Displays die selbe Auflösung haben. Kopiere den Display Prototypen vollständig in ein Verzeichnis Bedienung_Einstellungen in dieser Kopie kannst du die nicht benötigten Funktionen des Hauptprogrammes löschen und durch die hierfür notwendigen ersetzen. Wichtig basierend auf dem vollständigen Prototyp - keine abgespeckte version und dass man wie gewohnt vor dem kompilieren das Display über #define auswählen kann

#### VPS - Webpage 
Der Webserver des VPS soll eine zusätzliche Seite (auswählbar wie die unterschiedlichen Karten) erhalten, auf der man alle einstellbaren Funktionen und Aktionen der aktiven Geräte einstellen resp. auslösen kann

### Umsetzung, konkretisiert — Stand 6_3

Umgesetzt in den gemeinsamen Dateien (`xComDef6_3.h`/`xComProc6_3.h`), in den Geräten
Radar/Lidar/Manager, im neuen Touch-Bediendisplay `Displays/Bedienung_Einstellungen/`
und im VPS-Dashboard (`VPS/dashboard/`).

- **Generisches Einstellungs-/Aktions-Modell (entschieden):** Jedes Gerät führt eine
  Bitmaske `mySettings` (`deviceSettings` in `xComDef`): `supported` (welche Settings es
  hat), `values` (aktueller on/off-Zustand) und `actions` (auslösbare Aktionen). WELCHE Bits
  ein Gerät führt, legt seine `hwDef.h` über die Masken `STG_SUPPORTED`/`STG_DEFAULT`/
  `STG_PERSIST`/`STG_ACTIONS` fest — so bleiben die Variablen der Einstellungen wie gefordert
  in der `hwDef.h` des jeweiligen Elements. Setting-Indizes: `stgHbLed`, `stgCatLed`,
  `stgAutoCopyPose`, `stgAutoCalib`, `stgLidarMotor`; Aktionen: `actCopyPose`, `actCalibrate`, `actClearPose`.
- **Persistenz vs. Default:** `initSettings()` (in `xComProc`) lädt die persistierten Bits
  aus dem NVS-Namespace `"devcfg"` und überlagert damit `STG_DEFAULT`; nicht persistierte
  Bits (Lidar-Motor) starten immer auf ihrem Default (an). `saveSettings()` schreibt nur die
  `STG_PERSIST`-Bits.
- **Protokoll:** neue msgCodes `settingsRequest(11)` (ohne Payload — „melde deine
  Einstellungen") und `settingsReport(12)` mit `settingsPayload{supported,values,actions}`.
  Neue cmd-Codes `cmdSetSetting(20)` (`info=(idx<<1)|value`) und `cmdCopyPose(21)`. Die
  Kalibrier-Auslösung nutzt das vorhandene `cmdCalibrate(18)`. Gemeinsame Prozeduren in
  `xComProc`: `handleCommonMsg()` (beantwortet `settingsRequest`/`poseRequest`, führt
  `cmdSetSetting` aus + sendet `settingsReport`), `sendSettingsReport()`, `sendPoseReport()`,
  `copyPoseFromGroup()`.
- **Anzeige-Settings betreffen nur die LED/Anzeige, nicht die Funktion:** HB-/catObserved-
  Anzeige werden per `settingOn(...)` vor dem Setzen der Pixel geprüft (Radar/Lidar/Manager);
  Broadcasts/Detektion laufen unverändert weiter. Beim Manager sind es die EMPFANGS-Anzeigen.
- **Automatik/Aktionen:** Radar prüft `stgAutoCalib` vor dem Auto-Trigger und `stgAutoCopyPose`
  für periodische Gruppen-Pose-Übernahme; Aktion „Kalibrieren" = `cmdCalibrate`, „Pose
  kopieren" = `cmdCopyPose` → `copyPoseFromGroup()`. Lidar: `stgLidarMotor` schaltet den Scan
  (`lidar.stop()`/`startScan()`). „Pose kopieren" (`actCopyPose`) nutzt, dass Geräte derselben
  relativen Koordinatengruppe dieselbe Welt-Pose teilen (poseRequest/poseReport).
- **Display (Touch):** `Displays/Bedienung_Einstellungen/` ist eine **vollständige Kopie des
  Display-Prototyps** (`Udisp6_3_0`) — gleiche Display-Auswahl per `#define`
  (CYD28/CYD35/Sunton7/Sunton5/M5Core2/M5Tab5/Wave7), gleiche LovyanGFX-Profile
  (`dispDevLoGFX.h`) und Bildschirm-Definitionen (`dispDef.h`). Das Karten-/Menü-Betriebs-
  programm wurde entfernt und durch eine **autoskalierte Touch-Bedienung** ersetzt: Seite 1
  Geräteauswahl (Geräte, die `settingsReport` gemeldet haben), Seite 2 die Einstellungen als
  AN/AUS-Schalter und die Aktionen als Buttons. Bedienung per `gfx.getTouch()`, Ziel-IPs aus
  den HBs gelernt. Default-Build **CYD35** (per USB), OTA aktiv.
- **VPS-Webinterface + Rückkanal (entschieden):** Das Dashboard erhält einen Tab
  **„Steuerung"** (umschaltbar wie die Karten), der je aktivem Gerät die Settings-Schalter und
  Aktions-Buttons zeigt. Da der VPS die 192.168.0.x-Geräte **nicht direkt** erreicht, wirkt der
  **Manager als Gateway in beide Richtungen**: er reicht die `settingsReport` per `/ingest`
  an den VPS weiter und **pollt** `GET /commands` (CSV „target,cmd,info"), um vom Webinterface
  angeforderte Kommandos auf den lokalen Bus zu geben (`POST /command`). `target 255` =
  Broadcast `settingsRequest` (alle Geräte melden sich).

## Aufgabe: Katze oder Störung — Ereignis-Validierung

### Grobkonzept
Radar und Lidar-C1 laufen und liefern `catObserved` — aber auch Störungen:
einzelne Zufallsereignisse, Sonneneinstrahlung ins Lidar (haufenweise
Fehlmeldungen auf einmal), Insekten nahe am Sensor, Vegetation im Wind. Bevor
die Feuerlogik (PA) auf Beobachtungen schiesst, braucht es eine Validierung:
**Ist das eine Katze oder eine Störung?**

Randbedingungen:
- Lange warten und viele Treffer zählen geht nicht — eine Katze kann schnell
  sein und ist sonst über den Rasen, bevor das System reagiert.
- Beide Sensoren zwingend verlangen (UND) geht nicht — das Lidar sieht die
  Katze je nach Fellfarbe/Reflektivität nicht.
- Ground Truth ist unsicher — die Wildkamera zeichnet nicht jede Katze auf.

### Prinzip: Spuren bewerten statt Treffer zählen
Eine echte Katze liefert bei ~10 Hz Sensorrate schon in 0,3–0,5 s mehrere
Beobachtungen, die **physikalisch zusammenhängen**: plausible Geschwindigkeit,
plausible Beschleunigung, Netto-Verschiebung über den Rasen. Störungen können
einzelne Ereignisse vortäuschen, aber fast nie eine kohärente Bahn:

| Störung | Signatur | scheitert an |
|---|---|---|
| Einzel-Zufallsereignis | 1 Beobachtung, dann nichts | keine Fortsetzung im Gate |
| Sonne ins Lidar | Burst: viele Bins gleichzeitig, Positionen springen | keine kohärente Bahn |
| Insekten | nur nah am Sensor, 1–2 Bins, erratisch | Nahbereichs-Gate, Winkelausdehnung |
| Vegetation im Wind (Radar) | oszilliert am festen Ort | Netto-Verschiebung ≈ 0 |
| Regen | beide Sensoren rauschen überall gleichzeitig | Sturm-Modus |

### Drei Ebenen

**Ebene 1 — Selbstdiagnose im Sensor („Sturm-Modus"):** Der Sensor erkennt
selbst, wenn er gerade unglaubwürdig ist. Lidar: zu viele markierte Punkte pro
Umdrehung (Gegenstück zu `MIN_DETECT_POINTS`) oder zu weite Winkelstreuung →
ganze Umdrehung verwerfen; mehrere Sturm-Umdrehungen in Folge → Sensor meldet
sich ab (sendet nichts mehr, bis es einige Sekunden ruhig war; optional
Text-Multicast „Lidar degraded"). Radar analog über die Ereignisrate
(Dauerfeuer am selben Fleck = Clutter). Die Fehlmeldungs-**Haufen** der Sonne
sind so gerade das Erkennungsmerkmal.

**Ebene 2 — Track-Bestätigung beim Konsumenten (Feuerlogik im PA):**
Beobachtungen in Welt-Koordinaten werden per Nearest-Neighbor mit Gate
(max. Katzengeschwindigkeit × Δt) zu Tracks assoziiert. Ein Track ist
bestätigt, wenn: ≥ 4 Beobachtungen innerhalb ≤ 1 s, Netto-Verschiebung
≥ 0,4 m, Geschwindigkeit durchgehend 0,1–4 m/s, liefernder Sensor nicht im
Sturm-Modus. Bestätigung nach ~0,5 s; der PA verfolgt danach die aktuelle
Track-Position (nicht die erste Beobachtung) — 0,75 m Katzenweg bei 1,5 m/s
sind verkraftbar.

**Ebene 3 — Fusion als Beschleuniger, nicht als Pflicht:** Kein UND, sondern
gestuft: (a) beide Sensoren sehen dasselbe Objekt (< 0,5 m, < 0,5 s) →
**sofort bestätigt** (~0,2 s); (b) ein Sensor allein → Track-Bestätigung nach
Ebene 2 (~0,5 s); (c) beide Sensoren rauschen gleichzeitig überall → Umwelt
(Regen/Sturm) → systemweiter Feuer-Stopp.

### Ground Truth ohne verlässliche Wildkamera
1. **Langzeitaufnahmen mit dem Simulator** (2. Ausbau, Kap. 5.7 der Doku):
   Nächte ohne Katze = reine Störungs-Bibliothek, Sonnenstunden = Sonnen-
   Signatur. Damit die Schwellwerte (Gate, Burst-Limit, Verschiebung) offline
   tunen und per Replay gegen die Feuerlogik testen.
2. **Kontrollierte Positiv-Daten:** die Kalibrier-Läufe (Person läuft kurvige
   Bahn) sind aufgezeichnete echte Tier-Bahnen; Testläufe mit bekannter
   Katze/Hund liefern weitere.
3. Optional später: **ESP32-CAM als Eigen-Trigger** — schiesst bei bestätigtem
   Track ein Foto der Track-Position; das System verifiziert seine eigenen
   Entscheidungen, statt auf den Zufalls-Trigger der Wildkamera zu hoffen.

### Festlegungen / Reihenfolge der Umsetzung
1. **Sturm-Modus im Lidar** (kleinster Eingriff, grösster Effekt gegen die
   Sonne — Erweiterung der Umdrehungs-Auswertung im C1-Sketch).
2. **Langzeit-Datensammlung** mit dem Simulator (Szenen 1–9, läuft nachts mit).
3. **Track-Bestätigung** als generischer Baustein in `xComProc` (nutzen beide
   PAs); Schwellwerte aus den gesammelten Daten.
4. Optional: Fusion-Schnellpfad (Ebene 3a) + ESP32-CAM.

(Stand 2026-07-02: Strategie festgelegt. Die Umsetzung beginnt — anders als
oben zunächst geplant — mit der **VPS-Modellierung der Katzenerkennung**
(nächste Aufgabe unten): erst das Erkennungsmodell auf dem VPS an echten
Langzeitdaten entwickeln, dann die abgeleiteten Regeln lokal umsetzen.
Sturm-Modus im C1 und Track-Bestätigung in `xComProc` bleiben gültige
Bausteine und folgen danach.)

## Aufgabe: VPS-Modellierung der Katzenerkennung (CatDetected)

### Grobkonzept

Ziel ist die **qualifizierte Aussage `CatDetected`**: das System hat aus den
rohen `catObserved`-Beobachtungen geschlossen, dass es sich tatsächlich um eine
Katze handelt. Die Aktoren sollen künftig auf `CatDetected` reagieren statt auf
rohe `catObserved` (sie prüfen weiterhin selbst No-Shot, Limits, Sicherheit).
Die Live-Entscheidung muss am Ende **lokal im CatFind-Netz** laufen — geplant
auf einem **spezialisierten ESP32** (ein Raspberry Pi nur, wenn es wirklich,
wirklich nicht anders geht — also fast sicher nicht). Der VPS ist **nicht** Teil
der Live-Kette; er ist das **Modellierungs- und Analysewerkzeug**, mit dem das
Erkennungsmodell an echten Daten entwickelt wird, bevor es auf den ESP32 kommt.
(Das Dokument `CatDetectionModelingStrategy.md` war hierfür die Inspiration;
verbindlich sind die Festlegungen in dieser Aufgabe.)

**Erkenntnisse aus den Outdoor-Tests (2026-07, Kalibrierung erfolgreich):**

- Das **Radar** ist der **primäre Katzendetektor**: eine Katze wird mit recht
  hoher Wahrscheinlichkeit erkannt; Störungen kommen vor, sind aber beherrschbar.
- Der **LidarC1** produziert je nach Wetter/Tageszeit extrem viele
  Fehlereignisse und sieht die Katze meist gar nicht — **super zum
  Lokalisieren, wenig nützlich zum Detektieren**. Er darf die Erkennung
  unterstützen, seine Bestätigung darf aber nie Pflicht sein.
- Die **Detektionsgrenzen** helfen bei der Wahrscheinlichkeits-Bewertung:
  beim Radar ist der **Öffnungswinkel eher hart** (Achtung: Bewuchs kann Teile
  verdecken), die **Maximalreichweite dagegen weich** (objektgrössenabhängig).

**Arbeitsweise:** Der VPS zeichnet die `catObserved` über sehr lange Zeit
persistent auf. Auf einer neuen, zoombaren Analyse-Karte geht man in der Zeit
vorwärts/rückwärts, streckt und staucht das Zeitfenster, blendet einzelne
Sensoren ein/aus, beurteilt die Signale und markiert (labelt) sie. Der VPS
markiert die `CatDetected`-Ereignisse nach seinem Modell; wo das Modell
danebenliegt, werden Parameter angepasst oder das Modell ergänzt — so lange,
bis die Trefferwahrscheinlichkeit im Gesamten gut ist. Erst dann wird das
Modell auf dem ESP32 implementiert.

### Konkret / Festlegungen

1. **Erweiterung, kein Umbau:** Alles, was auf dem VPS läuft (Live-Dashboard,
   Karten, Steuerung, Localizer), läuft unverändert weiter. Die Analyse kommt
   als **zusätzlicher Tab** ins bestehende Dashboard (`VPS/dashboard/`).
2. **Persistenz:** Alle eingehenden `catObserved` werden — bei eingeschalteter
   Aufnahme — in eine **SQLite-Datenbank in einem Docker-Volume**
   (`/opt/catfinder/data/`) geschrieben. Die Daten überleben damit
   Container-Neubauten (Modelländerungen!) und liegen **nicht** in GitHub.
   Die Aufnahme hat **Pause/Append**: ein REC-Schalter im UI pausiert die
   Aufzeichnung bzw. setzt sie fort (an dieselbe Datenbank anhängend); der
   Zustand ist persistent. Labels und Modell-Parameter liegen im selben Volume.
3. **Millisekunden-Zeitstempel (Manager-Erweiterung):** Der Manager bündelt
   Events ~1,5 s — ohne Gegenmassnahme bekämen alle Events eines Pushes
   dieselbe Zeit, und Geschwindigkeits-/Bahn-Features wären unmöglich. Der
   Manager schickt deshalb pro Event zusätzlich seine **Empfangszeit
   (`millis()`)**, den **targetSpeed** des Radars sowie pro Push `now_ms` und
   einen **Drop-Zähler** (wie viele Events der Burst-Schutz verworfen hat —
   bei Lidar-Sonnen-Bursts ist gerade die Menge das Erkennungsmerkmal). Der
   VPS rechnet daraus ms-genaue Serverzeiten. Abwärtskompatibel: Events ohne
   `ms` bekommen die Push-Zeit.
4. **Analyse-Tab:** Die Zeitleiste zeigt **genau das gewählte Zeitfenster**
   (Ereignisdichte je Sender, Zeit-Gitter, Aufnahme-Band, Labels, rote
   CatDetected-Punkte): Ziehen = verschieben, Mausrad = strecken/stauchen,
   „Alles" passt die ganze Aufnahme ein. (Der frühere Übersichts-Balken über
   die gesamte Aufnahme mit blauem Fenster-Rahmen wurde nach mehreren Tagen
   Aufnahme unübersichtlich und ist ersetzt.) Darunter eine **Welt-Karte mit
   Pan/Zoom** (RasenKarte als Hintergrund), auf der die Events des Fensters
   erscheinen (Farbe je Sensor/Ziel). Einzelne Sensoren sind
   ein-/ausblendbar; die Erfassungssektoren sind einblendbar („Erfassung");
   „Geräte ⟳" liest die xComDef sofort neu und fragt frische Posen an. **Labeln:** ein
   Zeitbereich (optional auf einen Sensor beschränkt) bekommt ein Label
   (Katze, Einzelereignis, Insekt, Vegetation, Sonne/Lidar, Regen/Sturm,
   Vogel, unbekannt); Labels sind persistent und in der Zeitleiste sichtbar.
5. **Modell auf dem VPS (v2 — Score statt starrer Regeln):** bildet aus den
   Events **Tracks** (Nearest-Neighbor mit Geschwindigkeits-Gate, in
   Welt-Koordinaten; **Track-Stitching** überbrückt kurze Aussetzer, z.B.
   wenn eine sitzende Katze aus dem Radar fällt) und bewertet jeden Track
   mit einem **Score 0–100** („Katzen-Wahrscheinlichkeit", mit
   Aufschlüsselung im UI):
   - **Pflicht** ist eine **kohärente Bewegungsphase** irgendwo im Track
     (sonst wäre oszillierende Vegetation eine „sitzende Katze"); dabei
     zählen **Sensor-Gewichte je Gerätetyp** — die Typen (HLK, Lidar, …)
     liest der VPS **dynamisch aus `xComDef6_3.h`** (GitHub raw, wie die
     RasenKarte): neue Sensoren brauchen keinen VPS-Eingriff, und die
     Gerätedatenbank bleibt die Quelle der Wahrheit. HLK-Radar wiegt hoch,
     Lidar niedrig — Lidar allein bestätigt nie.
   - **Erfassungsgrenzen aus der Gerätedatenbank (geometrisch):** jedes
     Sensor-Gerät trägt in `xComDef6_3.h` seinen **nominellen
     Erfassungsbereich** (`covLeftDeg`/`covRightDeg`/`covRangeMm`; HLK-Radar
     −60°…+60°, 7 m; 360°-Lidar −180°…+180°, 12 m). Der VPS legt diese
     Sektoren über die **Welt-Posen der Sensoren** (die Geräte melden
     `poseReport`, der Manager reicht sie per Gateway-Push an den VPS
     weiter) in die Karte — nach dem **Versetzen eines Sensors stimmt der
     Bereich sofort wieder**, nichts muss empirisch neu „eingelaufen"
     werden. Der Bereich ist bewusst nominell (Winkel eher hart, Reichweite
     weich/objektgrössenabhängig) und pro Gerät in der xComDef anpassbar,
     z.B. wenn ein Radarsektor über die Rasenkarte hinausragt. Eine Katze
     läuft in den Erfassungsbereich **hinein und hinaus**: Track-Geburt/-Tod
     nahe dem Rand der **Gesamt-Abdeckung** (Vereinigung aller Sektoren —
     Übergaben zwischen überlappenden Sensoren zählen so nicht als
     Austritt) gibt **Bonus**; Auftauchen/Verschwinden mitten im Feld nur
     einen **weichen Malus** („kann sein, muss nicht"). Das trennt
     insbesondere **Vogel** (erscheint/verschwindet mitten im Feld) von
     **Katze**. Fallback, solange keine Posen bekannt sind: empirische
     Abdeckung aus den Langzeitdaten (Belegungsraster).
   - **Eine Katze darf stehenbleiben (koten!):** „sitzt am Ende stabil"
     gibt Bonus + Flag **STATIONAER**; ein am Fensterende noch offener
     Track bekommt keinen Austritts-Malus (Flag OFFEN).
   - **Sturm-Erkennung** je Sensor über Rate **und räumliche Streuung**
     (hohe Rate allein ist auch eine normal getrackte Katze; ein Burst ist
     gleichzeitig überall). **Fusion** (mehrere Sender sehen dasselbe)
     gibt Bonus; weitere Boni/Maluse: Feld-Durchquerung, langer Track,
     sehr kurzer Track, unphysikalische Sprünge.
   Tracks ab `confirm_score` (mit Bewegungsphase) werden auf Karte und
   Zeitleiste als **`CatDetected`** markiert, mit Score und Begründung.
   Alle Schwellwerte stehen in einer **Parameter-JSON im Volume** und sind
   ohne Container-Neubau im UI änderbar; Modell-**Code**-Änderungen sind ein
   Container-Rebuild — die Daten bleiben davon unberührt.
6. **Mehr-Sensor-fähig:** alles arbeitet je `(sender, sensor)` — es kommen
   sicher 1–2 weitere Radarsensoren dazu, evtl. mehr. Das Modell trackt in
   Welt-Koordinaten (`worldValid=1`); Events ohne Welt-Koordinaten werden auf
   der Analyse-Karte nicht dargestellt (die Sensoren sind inzwischen
   welt-posiert).
7. **Manuelle Track-Bewertung + Übereinstimmung:** Im Tracks-Panel lässt sich
   jeder Track von Hand kategorisieren: **„Katze", „Person", „Vogel",
   „Mäher"** oder **„Störung"** (persistent, stabiler Track-Schlüssel =
   Geburtszeit+Geburtsort; nochmal klicken = entfernen). Die Modellbewertung
   bleibt unberührt; **keine Markierung = einverstanden**; für die
   **Übereinstimmung Modell↔Mensch** oben zählt alles außer „Katze" als
   „keine Katze" (x/y, „Modell-Katze abgelehnt" bzw. „Katze übersehen") —
   die Messlatte fürs Iterieren; die Kategorien sind zugleich gelabelte
   Ground-Truth. Die Modell-Flags (sitzt/offen/Eintritt/Austritt/2+
   Sensoren) stehen als **Chips direkt in jeder Track-Zeile**.
   **RoboMäher:** Zeitbereiche mit dem Label **„Mäher"** werden von der
   Analyse ausgeschlossen (Events bleiben aufgezeichnet und sichtbar — die
   Mäher-Läufe leuchten die Erfassungsbereiche schön aus, sollen aber keine
   CatDetected erzeugen). Zusätzlich verwirft das Modell Tracks mit
   **Weglänge > `max_path_mm`** (Default 40 m) automatisch als
   „Mäher/Person?" — ein Mäher/eine Person läuft in einem Track hunderte
   Meter zusammenhängend, eine Katze nicht; damit sind auch unmarkierte
   Mäher-Läufe entschärft.
8. **CatDetected-Auslösezeitpunkt:** Vom Modell bestätigte Tracks tragen auf
   Karte und Zeitleiste einen **roten Punkt** an dem Ort/Zeitpunkt, an dem
   das (künftig auf dem ESP32 laufende) Modell `catDetected` auslösen würde —
   also **bevor** die Katze den Erfassungsbereich verlässt. Die
   Wire-Datenstruktur ist in `xComDef6_3.h` definiert (`catDetected` = msgCode
   13, `catDetectedPayload`: Welt-Position, Score, Flags, Track-Dauer,
   Netto-Verschiebung).
9. **Inzwischen umgesetzt (2026-07-04):** am Radar das **RasenKarten-Gating**
   (neuer Kartentyp `mapRasen` = Rasen-Umriss in Welt-mm aus `Map/RasenKarte.csv`;
   der Manager verteilt ihn wie die No-Shot-Karte gechunkt per UDP, generisches
   `acquireMap` in xComProc; das Radar meldet mit gültiger Welt-Pose nur noch
   Ziele innerhalb des Rasens, fail-open ohne Karte/Pose — die No-Shot-Karte
   lädt es bewusst NICHT, es schießt nicht) und die **Kopplung des
   Pose-Drift-Health-Checks an das Auto-Kalibrierungs-Setting** (Pose wird nur
   noch automatisch verworfen, wenn `stgAutoCalib` an ist und das Radar sich
   selbst neu kalibrieren kann); der **radarCalibrationButton** hat eine
   **Zielauswahl** über alle HLK-Radare der Gerätetabelle.
10. **Nicht Teil dieser Aufgabe** (Folgeaufgaben, notiert): der
   ESP32-DetectionActor, der `catDetected` tatsächlich sendet (erst wenn das
   Modell steht); Sensor-/Aktorprofile in `xComDef6_3.h` (2. Priorität —
   Aktor-Reichweiten); am Radar: Mute je Sensor (VPS + Display, wegen
   mehrerer Radars).

## Aufgabe: KI-Vision-Katzenlokalisierung auf Raspberry Pi (KIVisionCatLocator)

### Grobkonzept
Ein kamerabasiertes Gerät auf einem **Raspberry Pi 4** überwacht den Rasen
dauerhaft, erkennt eindringende Katzen per KI (**Coral Edge TPU**, lokal, ohne
Cloud), bestimmt ihre **Position im Rasen-Weltkoordinatensystem** und meldet sie
dem bestehenden CatFinder-ESP32-Netzwerk. Das Gerät (`KIVisionCatLocator`) ist
**vollwertiges Mitglied des Busses** und verhält sich wie die anderen Sensoren,
obwohl es auf einem Pi statt einem ESP32 läuft. Es wird vollständig von Claude
Code per SSH programmiert und ist wie die übrigen Geräte im Heim-WLAN
(192.168.0.x) angebunden.

Motivation: Eine gute KI-Kamera ist voraussichtlich der **stärkste
Katzendetektor** im System (Erfahrung bisher: Radar gut, Lidar schwach) und
liefert zusätzlich welt-lokalisierte Treffer als Ground-Truth, mit denen sich
das kausale CatIdent-Modell verbessern lässt.

### Hardware
- **Raspberry Pi 4, 1 GB** — Kamera, Bildverarbeitung, KI-Ansteuerung, Webserver, Bus-Kommunikation.
- **OV5647-Kamera (5 MP)** am CSI-Anschluss, einstellbarer Fokus/Objektiv.
- **Schaltbarer IR-CUT-Filter** — Farbbild am Tag, IR-empfindlich in der Nacht.
- **850-nm-IR-Scheinwerfer** — nächtliche Ausleuchtung für kurze Belichtung/scharfe Bilder.
- **Coral USB Accelerator** — Edge-TPU-Inferenz über USB 3.0.
- **WLAN** — Heimnetz, Weboberfläche, Bus-Teilnahme.

### Prinzip / Bild-Pipeline
Ein einziges Programm öffnet die Kamera und stellt jedes Bild gleichzeitig der
KI, dem Webserver und der Ereignisspeicherung zur Verfügung.
- Kamera läuft dauerhaft (z.B. 1280×960, 10–15 fps).
- Der relevante Rasenbereich wird in **mehrere überlappende Ausschnitte** geteilt;
  jeder wird auf die Modell-Eingabegröße (z.B. 320×320) skaliert und vom Coral
  geprüft. Treffer-Koordinaten werden auf das volle Kamerabild zurückgerechnet.
  Grund: eine entfernte Katze bleibt im Ausschnitt größer/erkennbarer, als wenn
  das ganze breite Bild auf 320×320 verkleinert wird. (Optional später:
  Bewegungs-Gating — Inferenz nur auf Ausschnitten mit Bewegung, spart CPU/Wärme.)
- **Treffer-Bestätigung** (gegen Fehlalarme durch Schatten/Pflanzen/Insekten/
  Rauschen): eine Katze gilt z.B. erst als bestätigt, wenn ≥3 passende
  Erkennungen innerhalb ~1 s auftreten, die Sicherheit über einem Mindestwert
  liegt und die Treffer räumlich zusammenpassen.

### Weltlokalisierung (Boden-Homographie)
Eine einzelne Kamera misst nur eine **Richtung**, keine Distanz. Die Weltposition
entsteht über eine **planare Boden-Homographie** `H` (3×3), die Bildpixel auf
Rasen-Weltkoordinaten (mm) abbildet:
- Für eine erkannte Katze wird der **Fußpunkt** der Bounding-Box (Kontakt
  Katze↔Boden, i.d.R. Unterkante-Mitte) über `H` in `worldX/worldY` gerechnet.
- Voraussetzungen: Rasen ~eben (planar), Kamera fix montiert, **Linsen­verzeichnung
  vorher korrigiert** (Weitwinkel-OV5647 verzeichnet spürbar), Füße sichtbar.
- **Grenze:** Genauigkeit nah gut, fern schlecht (Perspektive staucht Distanz).
  Bei langem, längs betrachtetem Rasen hat das ferne Ende wenig Auflösung →
  Kamera möglichst hoch und steil nach unten montieren. Fernbereich-Lokalisierung
  ggf. bewusst grob.
- Die Homographie **ist** die „Pose" der Kamera (entspricht `poseReport`/Welt-Pose
  der anderen Geräte). Kalibrierung **assistiert** über die Weboberfläche: der
  Nutzer klickt bekannte Rasenpunkte (z.B. die Ecken der `RasenKarte`) im
  Kamerabild an, daraus wird `H` berechnet. Das nutzt vorhandene RasenKarten-Daten
  wieder; eine vollautomatische Ableitung ist nicht Teil dieser Aufgabe.

### Integration ins ESP32-Netz
- **Vollwertiges Busgerät:** Python-Portierung des xCom-Protokolls (`struct`-
  gepackte Payloads, little-endian, Header-Version `0x63`, Multicast
  239.0.0.57:8266, Unicast :23456, Text-Multicast :8300). Der Pi sendet **HB**,
  **`poseReport`**, **`settingsReport`** und antwortet auf `settingsRequest`,
  `cmdSetSetting`, `cmdReboot`; er erscheint damit automatisch im VPS-Steuertab
  (inkl. Aktiv/Ruhemodus `stgActive`, Reset).
- **Neues Gerät:** ID **20**, neuer Typ (z.B. `VisionLocator`), feste IP (z.B.
  `.186`), Eintrag in `device[]`, `deviceCount` 20 → **21**. Die Gerätetabelle
  parst der VPS bereits aus `xComDef6_3.h` (`parse_xcomdef`).
- **Festlegung (Variante A): der Pi sendet `catObserved` mit `worldValid=1`,
  NICHT `catDetected`.** CatIdent bleibt der einzige Bestätiger (`catDetected`),
  damit nicht mehrere Quellen Aktoren auslösen; der Pi ist ein hochwertiger,
  welt-lokalisierender Sensor, dessen Beobachtungen das kausale Modell fusioniert.
- **Bilder** werden per HTTP an den VPS geladen (wie die CatCam) und erscheinen im
  Bilder-Tab. **Rollen-Abgrenzung zur CatCam:** CatCam = Nahaufnahme/Identifikation,
  Pi = Weitwinkel-Erkennung + Lokalisierung.

### Software-Stack (Festlegungen)
- **OS: Raspberry Pi OS Bookworm 64-bit Lite (Debian 12, Python 3.11), ohne Desktop**
  (umgesetzt am 2026-08-18; ersetzt die frühere Bullseye-Festlegung). Grund der
  Änderung: seit der Trixie-Umstellung bietet der Raspberry Pi Imager Bullseye
  nicht mehr an („Legacy" = Bookworm), und die befürchtete Coral-Falle lässt sich
  auf Bookworm sauber umgehen — siehe Inferenz-Punkt. Bullseye bliebe nur über ein
  Archiv-Image ohne regulären Support erreichbar.
- **Kamera:** `picamera2`/libcamera (OV5647).
- **Inferenz:** `libedgetpu` + `tflite-runtime` (+ optional `pycoral`), **alle drei
  gegen dieselbe TF-Version gebaut** — verifizierte Kombination auf Python 3.11:
  `libedgetpu1-std 16.0tf2.17.1` + `tflite_runtime 2.17.1` + `pycoral 2.0.3`
  (Community-Builds von `github.com/feranick`, weil Googles Wheels bei Python 3.9
  enden) und **`numpy<2`** im venv (tflite_runtime ist gegen numpy 1.x gebaut).
  Googles eigenes `libedgetpu1-std 16.0` (gegen TF 2.5) **segfaultet** mit
  `tflite-runtime 2.14` — das ist die eigentliche Falle, nicht die Python-Version.
  Gemessen auf dem Pi 4: 13,5 ms/Inferenz (SSD MobileNet v2 COCO, 300x300).
  Startmodell
  **COCO-vortrainiert (Klasse „cat"), Edge-TPU-kompiliert (int8)**. Nachtbetrieb
  (IR-Graubild) erkennt COCO anfangs schlecht → **IR-Nachttraining später**
  (anspruchsvoll, iterativ; siehe Phase 7).
- **Web:** kleiner Flask/FastAPI-Server + MJPEG-Stream, Kontrollstream nur aktiv,
  solange die Seite offen ist (spart CPU/RAM/WLAN im Normalbetrieb).
- **Autostart:** `systemd`-Dienst.
- **Protokoll-Sync:** die Python-xCom-Structs werden aus `xComDef6_3.h`
  abgeleitet/generiert, damit C-Header und Python nicht auseinanderdriften
  (zwei Quellen der Wahrheit vermeiden).

### Tag- und Nachtbetrieb
| Modus | IR-Filter | IR-Scheinwerfer |
|-------|-----------|-----------------|
| Tag   | ein       | aus             |
| Nacht | aus       | ein             |

Umschaltung über Sonnenauf-/-untergang oder gemessene Bildhelligkeit; für gute
Nachtbilder ist gleichmäßige IR-Ausleuchtung wichtiger als maximale Mittenhelligkeit.

### Grenzen / Risiken (bewusst benannt)
- **Coral/Python-Version** — mit Bullseye entschärft.
- **Nacht-IR-Erkennung** — COCO auf Tag-RGB trainiert; Nachtraining nötig.
- **Monokulare Fernbereich-Genauigkeit** — Boden-Ebenen-Annahme; kauernde/erhöhte
  Katze bricht die Fußpunkt-Annahme.
- **Outdoor (Hardware, macht der Nutzer):** stabile 5V/3A+-Versorgung, Kühlung
  (Dauer-Inferenz erwärmt den Pi), wetterfestes Gehäuse, WLAN-Reichweite am
  Montageort. IR-CUT/IR-Scheinwerfer-Verkabelung (GPIO; der IR-CUT-Filter braucht
  meist einen kurzen Umschalt-Impuls, kein Dauersignal) klärt der Nutzer im Detail.

### Phasen (Reihenfolge der Umsetzung)
1. **Pi-Setup (assistiert).** Der Nutzer beschreibt eine leere SD-Karte mit dem
   Raspberry Pi Imager (Windows); Claude gibt die exakten „Advanced options"
   (Hostname, WLAN, SSH-Key, User). Danach übernimmt Claude per SSH: OS, Kamera
   und Coral einrichten, Grundcheck der Steuerbarkeit. (SD-Beschreiben und
   Kamera-/IR-Verkabelung sind die manuellen Teile.)
   **ERLEDIGT 2026-08-18:** Pi 4 (2 GB) mit Bookworm 64-bit Lite, Hostname
   `kivision`, User `pi` (SSH-Key, sudo ohne Passwort), feste IP
   **192.168.0.186/24** (NetworkManager-Profil `preconfigured`, WLAN-Powersave
   aus), Zeitzone Europe/Zurich. Kamera OV5647 erkannt (bis 2592x1944),
   `python3-picamera2` aus apt. Python-Umgebung: `~/kivision/venv`
   (`--system-site-packages`, damit picamera2 sichtbar ist), Coral-Stack wie oben,
   Testskripte `~/kivision/test/{coral_test.py,cam_test.py}`, Testmodell in
   `~/kivision/models/`. Verifiziert: Katze auf Coral-Testbild mit 0.96 erkannt,
   13,5 ms/Inferenz; Kamera liefert 1280x960-Bilder.
2. **Kamera-Test + Webserver.** Livebild + Kamerasteuerung im Browser. Der Nutzer
   sucht den optimalen Montageort (ganzer Rasen im Blick — vermutlich Kamera 90°
   gedreht, da der Rasen länger als breit ist) und testet Objektive sowie
   Tag-/Nachtfähigkeit.
   **ERLEDIGT 2026-08-27:** Weboberfläche unter **http://192.168.0.186:8080/**
   (Quellcode `KIVisionCatLocator/`, auf dem Pi `~/kivision/web/`, systemd-Dienst
   `kivision-web` mit Autostart). MJPEG-Livebild, Anzeige-Drehung 0/90/180/270°,
   Spiegeln, Raster, Auflösung 640×480…2592×1944, Bildrate, JPEG-Qualität,
   Belichtung/Gain/Weißabgleich/Bildregler, Schnappschuss (Stream oder volle
   5 MP) mit Galerie, und ein **zuschaltbarer Coral-Test** mit den Kachel-
   Ausschnitten des Konzepts (1…3×2, 15 % Überlappung) — damit lässt sich am
   Montageort direkt prüfen, ob die KI von dort erkennt. Kopfzeile zeigt Lux,
   Belichtungszeit, Gain, **FocusFoM** (Scharfstellhilfe) und CPU-Temperatur.
   Gemessen: 10 fps bei 1280×960, Erkennung 110 ms (ganzes Bild) bzw. 143 ms
   (2 Kacheln), CPU 40 °C ohne Throttling. Erste Beobachtungen: deutlicher
   **Magenta-Stich** (IR-Sperrfilter steht nicht im Strahlengang) und starke
   **Tonnenverzeichnung** — beides vor Phase 4 zu klären.
3. **Netz-Integration.** Claude macht den Pi zum Busgerät `KIVisionCatLocator`
   (Python-xCom, HB, `settingsReport`, Steuerbarkeit im VPS).
4. **Pose / Homographie (assistiert).** Die Kamera lernt Standort und Ausrichtung
   als Boden-Homographie über RasenKarten-Referenzpunkte (Web-UI-Klicks; VPS falls
   nötig).
5. **Katzenerkennung + Tag/Nacht-Automatik.** Permanente Erkennung; IR-Filter/
   -Scheinwerfer automatisch tag/nacht; bei bestätigter Katze `catObserved` senden
   und ein Bild auf dem VPS ablegen.
6. **Lokalisierung.** Erkannte Katze → Rasen-Weltkoordinaten (Homographie aus
   Phase 4) → in `catObserved` (`worldX/worldY`, `worldValid=1`) mitübertragen.
7. **CatIdent-Modelloptimierung.** Die welt-lokalisierten Kamera-Treffer dienen
   zusammen mit den Daten der anderen Sensoren als Labels/Ground-Truth, um das
   kausale CatIdent-Modell zu tunen — und als Grundlage fürs IR-Nachttraining.

### Nicht Teil dieser Aufgabe / Abgrenzung
- Wetterfestes Gehäuse, Strom, Montage (Nutzer).
- IR-CUT-/IR-Scheinwerfer-Verkabelung im Detail (Nutzer studiert Modul/GPIO noch).
- Vollautomatische Pose-Ableitung ohne Nutzerklicks; IR-Nachttraining (eigene
  spätere Iteration); Nutzung als eigenständiger `catDetected`-Bestätiger
  (bleibt bei CatIdent).


