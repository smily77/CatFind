# Abbildung des Erfassungsbereichs

Diese Datei beschreibt das Prinzip, die Umsetzung und die Bedienung der gelernten
Erfassungsbereiche im CatFinder-System. Ziel ist, den in der Praxis sichtbaren
Erfassungsbereich eines Sensors nicht nur aus dem Datenblatt abzuleiten, sondern
bei Bedarf aus einer kontrollierten Robomäher-Fahrt als einfaches Weltpolygon zu
lernen.

## 1. Motivation

Die nominellen Radar-Datenblattwerte (`covLeftDeg`, `covRightDeg`,
`covRangeMm`) beschreiben den realen Erfassungsbereich nur grob. In der Praxis
ist der Radarbereich kein ideales Kreissegment. Er wirkt eher wie ein
unregelmäßiges, abgeflachtes Polygon. Zusätzlich meldet der Radar bei gültiger
WorldPose und geladener Rasenkarte nur Ziele auf dem Rasen. Damit ist für das
Katzenmodell nicht der physikalische Vollbereich des Sensors entscheidend,
sondern der praktisch gemeldete Bereich auf dem Rasen.

Eine Robomäher-Fahrt bei gutem Wetter ist dafür ein sehr guter Ausleuchter:

- der Mäher fährt systematisch über den Rasen,
- Katzen sind währenddessen sehr unwahrscheinlich im Bereich,
- die Radar-Punktwolke deckt den praktisch relevanten Bereich ab,
- Störungen außerhalb des Rasens werden durch die Rasenkarte bereits unterdrückt.

## 2. Grundprinzip

Für jeden Sensor kann optional ein gelerntes Erfassungsprofil gespeichert werden:

```text
Sensor + gültige WorldPose + Mäherzeitfenster -> Weltpolygon
```

Das Polygon wird in Weltkoordinaten gespeichert und ist an die dabei gültige
WorldPose gebunden. Wird die Pose gelöscht oder geändert, wird das Polygon
deaktiviert. Gibt es kein passendes gelerntes Polygon, wird der Default-Bereich
aus `xComDef` verwendet und ebenfalls als Polygon dargestellt/exportiert.

Die Reihenfolge ist:

1. gültiges gelerntes Polygon mit passendem Pose-Hash,
2. sonst Default-Polygon aus `xComDef` + gültiger Pose,
3. sonst empirischer Fallback, wenn keine posebasierte Coverage verfügbar ist.

Damit denken VPS und ESP32 gleich: Coverage ist eine Liste von Polygonen.

## 3. Posebindung

Beim Speichern eines Polygons wird ein `pose_hash` aus der aktuellen WorldPose
gebildet:

```text
x:y:heading:mirror
```

Dieser Hash wird mit dem Polygon gespeichert. Bei jeder späteren Coverage-
Berechnung prüft der VPS, ob der aktuelle Pose-Hash des Sensors noch zum
Polygon passt. Wenn nicht, ist das Profil inaktiv und es wird der Default-Bereich
verwendet.

Deaktiviert wird ein Polygon bei:

- ungültigem `poseReport`,
- geänderter Pose,
- manuell gesendeter neuer Pose,
- manueller Löschung im Dashboard.

## 4. Polygonbildung Variante B

Die Polygonbildung verwendet die polare Hüllkurven-Methode um den Sensorstandort.
Eingangsdaten sind die `catObserved`-Weltpunkte des ausgewählten Sensors in einem
manuell kontrollierten Mäherzeitfenster.

Ablauf:

1. Sensorpose laden: Welt-X, Welt-Y, Heading, Mirror.
2. Alle Weltpunkte des Sensors im Zeitraum holen.
3. Für jeden Punkt den Vektor vom Sensorstandort zum Punkt berechnen.
4. Winkel und Distanz bestimmen.
5. Winkel in Bins einteilen, z. B. 10 Grad.
6. Pro Winkel-Bin eine robuste Maximaldistanz bestimmen, z. B. das 90%-Quantil.
7. Aus den Winkel-Bin-Distanzen eine Hüllkurve in Weltkoordinaten erzeugen.
8. Diese Hüllkurve auf ca. 6-10 Punkte vereinfachen.
9. Qualitätswerte berechnen: Punktzahl, Binzahl, Fläche, Eckpunktzahl.

Warum Quantil statt Maximum? Einzelne Ausreißer sollen den Bereich nicht zu groß
machen. Ein 90%-Quantil beschreibt den stabil gesehenen Bereich besser.

## 5. Speicherung

Die Tabelle `coverage_polygons` enthält pro Sensor das aktive oder zuletzt
bekannte Profil:

```text
sender        Sensor-ID
active        1 = verwendbar, 0 = deaktiviert
pose_x/y      Pose beim Lernen
pose_head     Heading beim Lernen
pose_mir      Mirror beim Lernen
pose_hash     Fingerprint der Pose
polygon_json  Weltpolygon als JSON
point_count   Anzahl Punkte im Lernfenster
source_t0/t1  Zeitfenster der Mäherfahrt
quality       aktuell Polygonfläche in mm²
created       Erstellzeit
note          Grund/Kommentar
```

Das Polygon selbst wird als Liste von Weltpunkten in mm gespeichert:

```json
[[x1,y1], [x2,y2], ...]
```

## 6. Verwendung im VPS-Modell

Das VPS-Modell arbeitet intern weiter mit der bestehenden Coverage-Struktur aus
Zellen, Union und Rand. Gelernte Polygone und Default-Polygone werden dazu auf
das bestehende Coverage-Raster gerastert.

Dadurch bleiben bestehende Modellfeatures erhalten:

- Track-Geburt nahe am Rand,
- Track-Ende nahe am Rand,
- Mid-field Geburt als weicher Malus,
- Edge-Aktivierung nur bei genügend Coverage-Zellen.

Neu ist nur die Quelle der Coverage: gelerntes Polygon oder Default-Polygon.

## 7. Verwendung im ESP32 CatIdentifier

Der CatIdentifier lädt die aktive Polygonliste vom VPS über:

```text
GET /coverage_export.csv
```

Das CSV-Format ist kompakt:

```text
sender,source,n,x1,y1,x2,y2,...
```

Der ESP32 speichert diese Liste im LittleFS-Cache `/coverage.csv`. Wenn der VPS
nicht erreichbar ist, wird der Cache verwendet. Wenn keine Coverage geladen ist,
fällt der CatIdentifier auf die bisherige analytische Sektorlogik aus
`xComDef + poseReport` zurück.

Der ESP32 berechnet keine aufwendige Polygon-Union. Stattdessen nutzt er:

```text
inside-any-polygon
```

Für die Randtiefe wird bei Punkten innerhalb eines Polygons die minimale Distanz
zu dessen Kanten berechnet. Bei mehreren Polygonen wird die größte Tiefe als
beste Innenlage verwendet.

## 8. Bedienung im Dashboard

### Voraussetzungen

- Sensor hat gültige WorldPose.
- Robomäher ist in einem bekannten Zeitraum gefahren.
- Zeitraum ist möglichst störungsarm.
- Idealerweise ist das Fenster als „Mäher“ gelabelt oder bewusst im Analyse-Tab
  ausgewählt.

### Lernen

1. Analyse-Tab öffnen.
2. Zeitfenster der Mäherfahrt wählen oder per Shift-Ziehen markieren.
3. Optional Sensor im Label-Sender auswählen.
4. Button **Coverage lernen** klicken.
5. Sensor-ID eingeben.
6. Dashboard erzeugt eine Vorschau und zeigt:
   - Anzahl Eckpunkte,
   - Anzahl Punkte,
   - Fläche.
7. Bei plausibler Vorschau bestätigen.
8. Das Polygon wird gespeichert und sofort in der Coverage verwendet.

### Löschen

1. Button **Coverage löschen** klicken.
2. Sensor-ID eingeben.
3. Das gelernte Polygon wird deaktiviert.
4. Danach gilt wieder der Default-Bereich aus xComDef, solange eine gültige Pose
   vorhanden ist.

## 9. API-Endpunkte

### `GET /coverage_profiles`

Liefert gespeicherte Profile inklusive Pose-Match-Status.

### `POST /coverage_learn`

Erzeugt aus einem Zeitraum ein Polygon. Mit `save=false` wird nur eine Vorschau
geliefert. Mit `save=true` wird das Profil gespeichert.

Beispiel:

```json
{
  "sender": 1,
  "t0": 1780000000.0,
  "t1": 1780003600.0,
  "angle_bin_deg": 10,
  "quantile": 0.90,
  "target_vertices": 8,
  "save": true
}
```

### `POST /coverage_delete`

Deaktiviert das Profil eines Sensors.

```json
{"sender": 1}
```

### `GET /coverage_export`

JSON-Export für Diagnose und spätere Tools.

### `GET /coverage_export.csv`

Kompakter Export für den ESP32 CatIdentifier.

## 10. Zusammenspiel mit manueller Pose

Wenn eine neue manuelle Pose an einen Sensor gesendet wird, deaktiviert der VPS
das gelernte Polygon dieses Sensors. Der Grund ist, dass das Polygon an die alte
WorldPose gebunden war. Nach erfolgreicher neuer Pose kann erneut eine
Mäherperiode gelernt werden.

## 11. Grenzen und Qualitätskontrolle

Eine Mäherfahrt kann schlechte Daten liefern, wenn:

- der Mäher nur einen kleinen Teil des Rasens gefahren ist,
- starker Regen/Sonne/Störungen vorhanden waren,
- zu wenige Punkte erzeugt wurden,
- der Sensor während oder vor der Mäherfahrt keine stabile Pose hatte.

Darum muss die Vorschau immer vom Benutzer plausibilisiert werden.

## 12. Deployment-Hinweise

Für den VPS genügt ein Dashboard-Neubau:

```bash
cd /opt/catfinder/dashboard
git pull --ff-only
docker compose up -d --build
```

Für den CatIdentifier muss `CatId6_3_0` neu kompiliert und hochgeladen werden,
damit er `/coverage_export.csv` lädt und die Polygone in der Echtzeitlogik
verwendet.

Radarsensoren müssen für diese Coverage-Funktion nicht neu geflasht werden,
sofern sie bereits Weltpunkte mit RasenMap-Filter melden. Für die separate
manuelle Pose-Funktion benötigen Zielgeräte weiterhin die entsprechende
xCom-Erweiterung.
