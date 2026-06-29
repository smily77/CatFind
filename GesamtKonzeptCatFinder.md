CatFinder ist ein System bestehend aus mehreren einzelnen Geräten. Der primäre Zweck ist es eine Katze auf dem Rasen zu detektieren und zu lokalisieren. Um zu verhindern, dass sie auf den Rasen kotet kann ein Actor einen Wasserstrahl in ihre Richtung senden. 

Das System besteht aus mehreren Komponenten und wund wird stetig um weitere erweitert. Bisher sind das alles Funktionseinheiten die eine oder mehrere Funktionen umfassen und lokal von einem ESP32 gesteuert werden, der via OTA mit Arduino programmiert worden sind. 



Die Arduino Programm Architektur:

Global

Globale Variablen und Typen Definitionen sowie Prozeduren, die von allen verwendet werden sind inxComDef.h und xComProc.h. Die sind Teil des Controller/Managers und werden in allen Programmen included. Sie Sind via symlink in der Arduino Libary Sektion. So können sie mit dem Manger bearbeitet werden und stehen allen zur Verfügung.

Support

Alle Programme definieren die HW und Konstanten in der hwDef.h init proceduren und HWprozeduren, support Prozeduren sind in der hwProc.ino. Spezialprozeduren (z.B. etwas erkennen, mapping usw.) werden auch als sepaparate .ino eingebunden

Betriebsprogramme

So können die Haupt/Betriebsprogramme möglichst schlank gehalten werden, hier werden eigentlich hauptsächlich lediglich Aktionen ausgelöst und auf Ereignisse oder empfangene Nachrichten reagiert. 



Kommunikation

Alle Programme sind OTA fähig und binden Credentials.h für die information zum WiFi ein. Die Kommunikation erfolgt über UDP, hauptsächlich Broadcast oder Unicast (und Text-Multicast für Debug). In xComDef in der stationDefinitions device sind alle Geräte die vorkommen können definiert. Die IP (letztes Byte) wird hier entweder vordefiniert oder vom Gerät eingetragen sobald es das erste Mal sendet. Wie und was gesendet wird, resp die Bedeutung wird auch in xComDef festgelegt (wie auch die Variabeln, die alle Geräte verwenden). 



Geräte bisher gibt es folgende Geräte-Arten:

Aktoren.

Die können einen Wasserstrahl ausrichten und ein ausschalten. Vielfach haben sie LED-Pixel als Indikator, Knöpfe usw. An ihrem Drehturm Kann können meist Sensoren (z.B. Lidar, US-Meter, IR-Detektor usw.) und Ziellaser oder andere Indikatoren befestigt werden. Damit können die Aktoren auch selbst als Sensoren arbeiten oder die Zielinformation überprüfen.

Sensoren

Die meisten Sensoren sind Distanz- und Bewegungssensoren (Lidar, Radar, usw.) Die z.B. eine Katze detektieren, oder etwas messen können und falls etwas detektiert Broadcasten. Es sind aber noch andere Sensoren, z.B. für Regen usw. denkbar  

Displays

Weiter gibt es Displays z.T. mit Bedienelementen oder on/off Schalter für z.B. das System auf "scharf zu schalten"

Spezial

Es gibt noch einfache geräte wie z.B. eine Laser Marker oder einen Simmulator. Eine Spezielle Rolle nimmt der Controller/Manager ein es braucht ihn eigentlich nicht, da das ganze Geräte Netzwerk ohne zentrale Steuerung auskommt. Der Manager hilft bei der Programm Entwicklung (globale Elemente) und könnte in der Zukunft als Informationsressourcenspeicher genutzt werden. Im Moment läuft der normale Betrieb ohne ihn.



Ausrichtung, Orientierung

Gegenwärtig werden im System Kartesische und Polarkoordinaten gleichzeitig und redundant verwendet. Der Kreis im Polarsystem hat 4096 "ticks" Wei die Seriellen Servo auch mit 4096 arbeiten. Diese Koordinatensysteme sind immer auf die Quelle bezogen. Damit der Actor also mit diesen Koordinaten arbeiten kann muss der Sensor fix mit ihm verbunden sein -> Tower-Achse wird quasi als Ursprung verwendet



**Aufgabe: Enable global Coordination system**

Das System soll zum einen die Möglichkeit erhalten mit einem globalen Koordinatensystem zu arbeiten und auch den Einsatz mehrere Aktoren erlauben. Wenn Aktoren nicht mit einem globalen Koordinaten System arbeiten, tun sie das mit einem relativen lokalen, das heisst die Koordinaten stimmen für Sensoren, die normalerweise fest mit dem jeweiligen Aktor verbunden sind und so eine eigene Koordinatensystem gruppe bilden.

Die Skizze des Gesamtzusammenhangs sieht wie folg aus: Sensoren und Aktoren verfügen über Proceduren, die ihnen erlauben ihre Position bezüglich eines globalen Koordinatensystemes fest zu stellen (Gegenstand einer späteren Aufgabe - Wird nicht in dieser Aufgabe realisiert). Der normale Start Ablauf (Für Aktoren und Sensoren) wird dann folgendermassen sein: 1. Globale Positions Koordinaten und Ausrichtung aus dem nichtflüchtigen Speicher auslesen. 2.) Quickcheck ob die Pose stimmt. Wenn nicht wird die Pose neu bestimmt. - Für Sensoren (und Aktoren) gilt dann, für Detektionsereignisse und Messungen werden die relativen Koordinaten (Polar und Kartesisch) übermittelt und falls eine gültige Positionsbestimmung vorliegt werden auch die globalen Koordinaten (nur Kartesisch, da globale Koordinaten immer nur kartesisch sind) übermittelt. Wenn ein Aktor ein Detektions-Ereignis empfängt prüft er, ob globale Koordinaten übermittelt wurden und er selbst eine gültige Lokalisations-Bestimmung hat. Falls das erfüllt ist verwendet er die globalen Koordinaten und prozessiert sie entsprechend, falls nicht prüft er, ob die relativen Koordinaten vom der selben relativen Koordinaten gruppe sind, zu der auch er gehört. Wenn dies der Fall ist verwendet er diese Koordinaten und prozessiert mit diesen. Falls weder das eine noch das andere zutrifft ignoriert er die Meldung. Ein Spezialfall sind Displays und Anzeigen. Diese Sind entweder auf eine relative Koordinatengruppe eingestellt und prozessieren diese oder wenn sie auf globale Koordinaten eingestellt sind und eine Meldung erhalten die nur relative Koordinaten enthält (und sie darauf reagieren müssten) prüfen sie, ob ein Mitglied der relativen Koordinatengruppe eine valide Positionsbestimmung hat und falls ja fragt er die an und rechnet dann die Koordinaten um. - Das ist die grobe Skizze des Gesamtzusammenhanges in dieser Aufgabe ist es nicht Gegenstand der Aufgabe diese spezifischen funktionalen Prozeduren zu erzeugen, das erfolgt später in spezifischen Aufgaben. In dieser Aufgabe soll die Datenstruktur und die common Prozeduren des Systems in der Version 6\_3 erweitert werden um das zu ermöglichen.

Konkret: 

1\. Jedes Gerät des Systems soll über die Variablen für die globalen Koordinaten und seine Ausrichtung sowie ein Flag validWorldPose (false nach booten) verfügen (Aus wenn nur Sensoren und Aktoren das vermutlich verwenden, sollen es alle haben, damit wir mit globalen Prozeduren arbeiteten können).

2\. Die Datenstruktur stationDefinitions device in xComDef - Das eigentlich das Geräte Verzeichnis darstellt, soll um einen Eintrag für die relative Koordinatengruppe erweitert werden. Zur Zeit haben wir eine für den Actor PA2, der der neben dem Actor, den CompactDome und LD06 dazugehört un eine für den Actor1\_1 mit dem MiniDome, alle überigen gehören zur Zeit keiner relativen Koordinatengruppe an.

3\. Erweitere Struktur/Payload, dass neben den relativen Koordinaten auch die world-Korrdinaten übermittelt werden (wir als 0,0 übermittelt wenn der Sensor keine valide Pose hat)

4\. Erweitere die Kommunikations-Struktur zur abfrage von ob validWorldPose vorliegt und zur Abfrage der lokalen Koordinaten falls validWordPose vorliegt

5\. Erweitere die globalen Proceduren in xComProc zur Umrechnung von lokalen Koordinaten in Welt Koordinaten und umgekehrt 

6\. Erweitere die globalen Proceduren in xComProc mit Proceduren zum speichern und lesen der ist-Pose in den nicht flüchtigen Speicher

**Umsetzung (konkretisiert) — Stand 6_3:**

Diese Aufgabe wurde in den gemeinsamen Dateien `xComDef6_3.h` / `xComProc6_3.h` umgesetzt. Getroffene Festlegungen (gelten als Teil der Aufgabe/Doku):

- **Einheiten:** Welt-Koordinaten immer kartesisch in **mm** (int32, konsistent mit `posPayload.x/y`). Ausrichtung (`heading`) in **PA-Einheiten 0..4096** (konsistent mit `angle`); 0 = lokale Achsen sind welt-ausgerichtet.
- **Zu 1) Pose-Variablen pro Gerät:** `struct worldPose { int32 worldX; int32 worldY; float heading; bool validWorldPose; }` mit globaler Instanz `worldPose myPose` in `xComDef6_3.h` — damit besitzt jedes Programm die Variablen. `validWorldPose` ist nach Boot immer `false`.
- **Zu 2) Relative Koordinatengruppe:** `stationDefinitions` erhält das Feld `byte group`. Defines `groupNone(0)`, `groupPA2(1)` = {PA2i, CompactDome, LD06}, `groupPA1_1(2)` = {PA1_1, MiniDome}. Alle übrigen Geräte = `groupNone`.
- **Zu 3) Payload-Erweiterung:** `posPayload` erhält `int32 worldX`, `int32 worldY` und ein explizites Flag `uint8 worldValid` (1 = Welt-Koordinaten gültig; 0,0 wäre sonst nicht von „Ursprung" unterscheidbar). Größe steigt von 25 → 34 Bytes ⇒ alle 6_3-Geräte müssen gemeinsam neu geflasht werden.
- **Zu 4) Kommunikations-Struktur:** neue msgCodes `poseRequest(6)` (ohne Payload — „melde deine Welt-Pose") und `poseReport(7)` mit `worldPosePayload { uint8 validWorldPose; int32 worldX; int32 worldY; float heading; }`. Damit lässt sich abfragen, ob ein Gerät eine `validWorldPose` hat, und der Anfrager kann mit `worldToLocal()` selbst in lokale Koordinaten umrechnen.
- **Zu 5) Transformation:** `localToWorld()` / `worldToLocal()` in `xComProc6_3.h` (2D-Translation + Rotation um `heading`).
- **Zu 6) Persistenz:** `savePose()` / `loadPose()` über NVS (Preferences-Namespace `"pose"`). `loadPose()` lädt nur Koordinaten/Ausrichtung; `validWorldPose` bleibt nach Boot `false` (Quickcheck bestätigt später).

Bewusst **nicht** Teil dieser Aufgabe (eigene Folge-Aufgaben): tatsächliche Positionsbestimmung/Quickcheck, das Füllen von `worldX/worldY` durch welt-fähige Sensoren, die Empfangs-/Feuerlogik der Aktoren und die Display-Sonderfall-Logik.


**Aufgabe: No-Shot-Karte über den Manager verteilen**

Das System braucht eine **Schusszonen-Karte** (im Sprachgebrauch weiter „No-Shot-Karte"). Die Karte beschreibt als geschlossene Welt-Fläche den **erlaubten Bereich** (den Rasen, bewusst etwas **enger** gefasst als der reale Rasen, weil teils Bewuchs über den Rand ragt). Es gilt: **innerhalb** der Karte ist Feuern erlaubt, **außerhalb** ist No-Shot. Die Karte wird zentral auf dem **Controller/Manager** gehalten und von dort an die Sensoren (bzw. welt-fähigen Geräte) verteilt, damit diese die In/Out-Entscheidung autonom und feuerschnell treffen können. Architektur-Entscheidungen (bereits getroffen):

- **Format:** Polygon als **CSV in Welt-mm** (ganzzahlig, konsistent mit `worldX/worldY`), Kommentar-Header mit Versions-/CRC-Kennung; mehrere Teilflächen/Löcher durch Leerzeile getrennt. (Begründung + Laufzeit-/Point-in-Polygon-Überlegungen siehe Projektnotiz `no-shot-map-format`.)
- **Ablage:** auf Manager **und** Sensor je im **LittleFS** (nicht SPIFFS). Auf den Manager kommt die Karte per OTA/Filesystem-Upload; der Sensor lädt sie vom Manager.
- **Abgrenzung:** Die VPS-Lokalisierungskarte (`Map/*.csv`) ist eine *andere* Karte und kein Teil dieser Aufgabe — der VPS zieht sie direkt aus dem Repo.

Konkret:

1\. **Manager:** No-Shot-CSV im LittleFS ablegen und pflegen (Upload per OTA-Filesystem-Image). Versions-/CRC-Kennung aus dem Header bereitstellen.

2\. **xComDef erweitern:** neue msgCodes und Payload-Structs für die Karten-Abfrage/-Übertragung (z.B. `mapRequest` mit Karten-Typ; `mapInfo` mit Typ/Version/CRC/Gesamtlänge/Chunk-Anzahl; `mapChunk` mit Chunk-Index + Daten). Karten-Typ-Enum vorsehen (zunächst `mapNoShot`), damit später weitere Kartentypen möglich sind.

3\. **xComProc erweitern:** gemeinsame Prozeduren für (a) Sender-Seite: Karte aus LittleFS lesen und gechunkt versenden; (b) Empfänger-Seite: Karte anfordern, Chunks sammeln, CRC/Version prüfen, in eigenes LittleFS schreiben. So kann jedes welt-fähige Gerät die Karte generisch vom Manager beziehen.

4\. **Sensor-Seite (Abfrage):** beim Boot (oder bei abweichender Version) die No-Shot-Karte vom Manager anfordern und lokal im LittleFS cachen; Quickcheck der Version gegen den Manager.

5\. **Transport (entschieden):** über das bestehende **UDP-xCom-Protokoll** (gechunkt, stilkonsistent über die neuen msgCodes in Punkt 2/3) — kein separater HTTP-Server.

6\. **Nicht Teil dieser Aufgabe** (Folge-Aufgaben): der eigentliche Point-in-Polygon-Test (In/Out) und die daraus folgende Feuer-/Melde-Konsequenz im Sensor/Aktor (siehe Projektnotiz `no-shot-map-format`).

Aufgabe: Sensor mit absoluten Koordinaten hinzufügen
Füge einen Lidar sensor zum Projekt CatFinder, als Sensor mit Weltkoordinaten, zum Projekt als neuen Sensor hinzu. Der Sensor (Sensor und Teile des Konzepts bekannt aus "C:\Users\stefan\Documents\Arduino\Lidar_C1_Prog" und "C:\Users\stefan\Documents\Arduino\Lidar_C1_Prog\Position_estimate" - jedoch Ablauf, wer den Prozess startetet und wo er läuft ist unterschiedlich).
Grobkonzept: Der Sensor soll seine eigene Position mit hilfe von geeigneten Programmen (Python) die auf einem VPS server laufen, ermitteln (verfahren wie bei PositionEstimate). Die IP des VPS wird durch Einbindung von Credentials.h in der Form IPAddress ipVPS(xxx.xxx.xxx.xxx); bereit gestellt. alles was auf dem VPS server läuft soll in  Docker geschen, damit ein Container im Repositary gespeichert werden kann und auf andere Maschienen portiert werden kann. Die RasenKarte.csv im Verzeichniss Map muss/kann vom VPS von CatFinder repositary (https://github.com/smily77/CatFind) geladen werden, das soll sicher stellen, dass bei änderungen nichts am VPS gemacht werden muss.
Der Ablauf ist volgendermassen. Beim Booten des Sensors, Positions und Ausrichtungsdaten aus den NVS lesen, prüfen, ob die plausibel sind (mit VPS), wenn nicht mit VPS wahrscheindlichse Position und Ausrichtung auf dem Rasen bestimmen -> Flag validWorldPose setzen, wenn validWorldPose No-Shot Karte aus speicher lesen und validieren, wenn und wenn nicht aktuelle neue vom Manager lesen - Wenn Manager nicht verfügbar, die alte No-Shotkarte verwenden. Wenn Position klar mit überwachung beginnen. Immer wenn ein Treffer mit No-Shot Karte überprüfen, ob da wo der Treffer ist im "Schiessbaren Bereich" liegt, wenn ja cat Observed broadcasten.
Weiteres 
-Die Status Pixel (2 bei Lidar_C1) sollen Auskunft über Initiaisierungsstatus geben und leuchten wenn eine Detektion im "Schiessbaren Bereich" erfolgte (ähnlich wie bisher)
-Natürlich OTA und alle anderen Konzepte von CatFinder
-Nach Abschluss der Initialisierung eine sehr knappe Text-Multicast über Status senden

**Festlegungen (Umsetzung):**

- **Gerät:** neuer Sketch `C1Lidar6_3_0` (Ordner `CF_LidarC1/`), RPLidar C1 (Serial1, 460800), 2× WS2812. Device-DB-Eintrag `LidarC1` (ID 17), Typ `Lidar`, **DHCP** (keine feste IP nötig; IP wird per HB gelernt), `group = groupNone` (welt-fähig, keine relative Gruppe). Flash per **OTA**.
- **Sensor↔VPS:** **HTTP-POST** vom Sensor an den VPS (`ipVPS` aus `Credentials.h` als `IPAddress ipVPS(46,225,81,240);`), Body = 360-Bin-Scan (mm), Antwort = Pose-JSON (`x_mm,y_mm,heading_deg,mirror,confidence,inlier_ratio`). Ausgehend → kein NAT-Port. **Offener Endpoint, kein Token.**
- **VPS:** Docker-Container `VPS/localizer/` im Repo (HTTP-Dienst, portiert aus `lidar_localize.py`), lädt `Map/RasenKarte.csv` aus dem GitHub-Repo (raw) beim Start/periodisch → Kartenänderung erfordert keinen VPS-Eingriff.
- **Scan-Quelle für die Lokalisierung:** der nach 20 s gelernte 360-Bin-Hintergrund (ortsunabhängig; **keine Wand/Nische mehr nötig** — Wand- und Landmark-Modell der alten Firmware entfallen, der Perimeter-/Hintergrund-Detektor bleibt und arbeitet rundum).
- **Plausibilitätsprüfung:** NVS-Pose wird gegen die globale VPS-Pose verglichen; stimmen sie (Toleranz) und ist die Konfidenz hoch → `validWorldPose=true` (NVS behalten); sonst VPS-Pose übernehmen und in NVS speichern.
- **No-Shot/Point-in-Polygon:** generischer Loader + In/Out-Test (innerhalb = schießbar) als gemeinsame Prozedur in `xComProc6_3.h`; Karte aus LittleFS, sonst per `requestMap` vom Manager (Bausteine bereits vorhanden), sonst alte Karte.
- **catObserved:** bei Treffer im schießbaren Bereich Broadcast mit relativen (x/y, radius/angle) **und** Welt-Koordinaten (`worldX/worldY`, `worldValid=1`).
- **Status:** Pixel zeigen Init-Phasen (WiFi/Kalibrierung/Lokalisierung/Karte) und leuchten bei Detektion im schießbaren Bereich; nach Init ein knapper Text-Multicast (`sendUdpTextln`, Port 8300).

Aufgabe: Treffervisualisierung
Grobkonzept. Der VPS stellt eine Webserver bereit auf dem man die CatObserved event sehen kann. Die Inforamtion dafür sendet der Master an den VPS - Zu beachten lokal läuft das CatFinder Netzwerk auch wenn der Master nicht zur Verfügung steht (ausnahme No-Shot Karte laden).
Anzeige des VPS Webservers:
1. Ein Fenster das die letzten paar System ereignisse zeigt - scrollend (nicht Funktion wie CatObserved etc. sondern Statusinformationen wie z.B. von der Initialisierund -> die als Debug Text gebroadcastet werden) - Dises kleine Fenster ist immer auf dem Schirm
2. Kleine Anzeige welche Geräte in der letzten 1- 3 Minuten einen HB gesenet haben (Dise kleine Anzeige bleibt auch immer auf dem Schirm 
3a. Eine scrollbare Liste mit allen CatObserved events - zusammengefast, so dass max. ein Eintrag pro Minute. Der Eintrag enthält Zeit und welche Sensoren ihn alles gemeldet haben (ID-Nr. des Sensors genügt)
3b. Karte - worldKoordinaten (RasenKarte) auf der alle CatObserved eingetragen werden. Jeder Sensor mit einer anderen Farbe (Achtung Radarsensoren können bis zu 3 Ziele verfolgen die sollten dann auch eine eigene Farbe haben. Die Karte hat einen Resetbutton - Alle CatObserved werden kummuliert bis der Resetbutton gedrückt wird
3c. Karte wie 3b aber in relativ Koordinaten der Koordinatenngruppe 1
3d. Karte wie 3b aber in relativ Koordinaten der Koordinatenngruppe 2
3e. Karte wie 3b aber in relativ Koordinaten der Koordinatenngruppe 3 (die gruppe gibts noch nicht, aber wirds noch geben
Die anzeige 3a - 3e können umgeschalzten werden so das der Bildschirm die Debug Msg, die HB Liste immer zeigt und die Ereigniss liste order Karten druchgeschalten werden können.

**Festlegungen (Umsetzung):**

- **Master als Gateway:** Der Manager lauscht ohnehin auf Multicast (catObserved, HB) und Text-Multicast (Debug, Port 8300). Er puffert diese und schickt sie **gebündelt per HTTP-POST** an den VPS (`ipVPS:80/ingest`, ~alle 1,5 s; Burst-Schutz mit Begrenzung pro Push). Fällt der Master aus, läuft das lokale Netz weiter (nur die Visualisierung pausiert). Pro Event überträgt der Master `sender, sensor, worldX/Y/valid, x/y` **und** `group` (= `device[sender].group`) — damit kann der VPS Welt- (3b) und relative Karten (3c–3e) zeichnen. Zeitstempel setzt der VPS beim Empfang (der Master braucht keine NTP-Zeit).
- **VPS-Webserver:** zweiter Docker-Container `VPS/dashboard/` auf **Port 80** (extern erreichbar als `http://<VPS-IP>/`). Single-Page-UI (Debug-Fenster + HB-Liste immer sichtbar; umschaltbar Liste 3a / Welt-Karte 3b / Gruppen-Karten 3c–3e; Reset-Button). State im RAM (Events kumulieren bis Reset; gehen bei Container-Neustart verloren). Welt-Karte nutzt `Map/RasenKarte.csv` (aus dem GitHub-Repo geladen).
- **Farben:** je `(sender, sensor)`-Kombination eine eigene Farbe (Radar bis zu 3 Ziele → 3 Farben).
- **Liste 3a:** catObserved werden pro Minute zu einem Eintrag zusammengefasst (Zeit + beteiligte Sensor-IDs).
- **Master-Flash:** über COM6 (USB).


Aufgabe: Welt-Pose eines Sensors per Co-Observation kalibrieren (Radar)

Grobkonzept: Sensoren mit 360°- oder beweglichem Lidar können ihre Welt-Pose selbst bestimmen (VPS-Lokalisierung), Radar-Sensoren nicht. Ein Radar (oder allgemein ein nicht selbst-lokalisierender Sensor) soll seine Welt-Pose dadurch erhalten, dass es **gleichzeitig mit einem welt-posierten Lidar dieselbe laufende Person beobachtet**. Aus den beiden Beobachtungsbahnen wird die starre Transformation Radar-Frame → Welt (Translation + Heading + Drehsinn = eine `worldPose`) geschätzt. Das ist klassische Extrinsik-Kalibrierung über ein gemeinsames bewegtes Ziel (Trajektorien-Registrierung).

Prinzip: Lidar (gültige Welt-Pose) liefert die Bahn in Welt-Koordinaten `p_welt`, das Radar dieselbe Bahn im Eigenframe `p_radar`. Gesucht ist `p_welt = R(heading)·(mirror·p_radar) + (tx,ty)`. Mit genug korrespondierenden Punktepaaren per Least-Squares-Registrierung (Umeyama/Kabsch) + RANSAC lösbar.

Ablauf (knopfgesteuert, im Radar-Sketch):
1. Voraussetzung: ein Lidar mit gültiger Welt-Pose broadcastet `catObserved` mit `worldValid=1`; das Radar läuft.
2. Knopfdruck am Radar → Kalibriermodus (~30–60 s, Status-Pixel zeigt es an).
3. Das Radar sammelt parallel: (a) seine eigenen Detektionen (Relativ-`x/y`, in Reihenfolge), (b) die `catObserved` des Lidars vom Bus (Welt-`x/y`).
4. Person läuft eine **kurvige** Bahn im gemeinsamen Sichtfeld.
5. Das Radar sendet beide Punktzüge an den VPS (neuer Endpunkt, z.B. `/calibrate`).
6. VPS macht Trajektorien-Registrierung (ICP-artig) + RANSAC + Umeyama → `tx, ty, heading, mirror, confidence`.
7. Das Radar übernimmt die Pose mit Quality-Gate (wie beim Lidar), `savePose`, `validWorldPose=true`, kurzer Status-Multicast. Danach füllt das Radar bei eigenen Detektionen auch `worldX/worldY` (`worldValid=1`).

**Festlegungen / wichtige Punkte:**

- **Korrespondenz über die Bahnform, nicht über die Zeit:** der Header-`timeStamp` ist `time_t` in **Sekunden** → bei Gehtempo zu grob für zeitliche Zuordnung. Daher Trajektorien-Matching (geordnete Punktzüge als Kurven ausrichten). Optionale spätere Verbesserung: ein Millisekunden-Feld einführen — vermeidbar, wenn Trajektorien-Matching genügt (kein Wire-Format-Eingriff).
- **Eindeutigkeit kommt vom Laufweg:** eine gerade Linie ist mehrdeutig (Translation entlang der Linie). Eine 2-D-strukturierte Bahn (Kurve/L/Acht) ist nötig. Beinbreite/Messrauschen helfen dabei NICHT (mitteln sich nur weg).
- **Wiederverwendung:** `posPayload` trägt bereits beides (Radar = Relativ-`x/y`, Lidar = Welt-`x/y` mit `worldValid`) → keine neue Datenstruktur. `worldPose`/`savePose`/`loadPose` (mirror-fähig), Quality-Gate, VPS-Muster, OTA, Status-Text, Knopf/`commandMsg` sind vorhanden.
- **Voraussetzungen/Grenzen:** überlappendes Sichtfeld, EINE Person, gute Lidar-Pose (Garbage-in→Garbage-out), genügend Punkte; VPS liefert Konfidenz → Gate verhindert eine schlechte Pose.
- **Generalisierung:** nicht radar-spezifisch — jeder nicht selbst-lokalisierende Sensor über jeden welt-posierten Sensor kalibrierbar. Die Sammel-/Sende-/Übernahme-Logik daher als gemeinsame Prozedur in `xComProc` (z.B. `coObserveCalibrate(...)`), VPS-Endpunkt einmal, nutzbar von Radar, künftigen Aktoren mit Turm-Sensor usw.
- **Offene Detailfragen (vor Umsetzung):** VPS-Endpunkt-/Daten-Format; Mindest-Bahnlänge/Konfidenzschwelle; Ziel-Assoziation, wenn das Radar bis zu 3 Targets liefert (RANSAC wählt die zur Lidar-Bahn passende Spur); ob die Lösung rein 2-D bleibt (mirror als ±1 wie beim Lidar).

