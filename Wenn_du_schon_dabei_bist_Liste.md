# CatFind 6_3 – Wenn-du-schon-dabei-bist-Liste

Diese Liste ist keine Sofort-Aufgabe, sondern eine Merkliste für den Moment, in dem am jeweiligen Sensor, Aktor oder an `xComProc6_3.h` gearbeitet wird.

## Zentrale Punkte

### 1. Aktor-Koordinatenprüfung zwingend einbauen

Wenn an einem Aktor gearbeitet wird, muss vor jeder Verarbeitung von `catObserved` geprüft werden, ob die Koordinaten für diesen Aktor gültig sind.

Regel:

- Wenn `pos.worldValid == 1` und der Aktor selbst eine gültige `myPose.validWorldPose` hat: Weltkoordinaten mit `worldToLocal()` in das lokale Aktor-System umrechnen.
- Sonst nur verwenden, wenn `device[sender].group == device[ID].group` und die Gruppe nicht `groupNone` ist.
- Alles andere ignorieren.
- Erst danach dürfen Limits, Zielen und Schusslogik geprüft werden.

Das soll möglichst zentral in `xComProc6_3.h` gelöst werden, damit PA2i, PA1_1 und spätere Aktoren gleich funktionieren.

### 2. Sender-Bounds-Check in `parseXMsg()`

In `xComProc6_3.h` muss zentral geprüft werden, ob `m.header.sender` innerhalb der `device[]`-Grenzen liegt.

Beispiel-Ziel:

```cpp
if (m.header.sender >= sizeof(device) / sizeof(device[0])) return false;
```

Damit werden falsche oder kaputte Pakete verworfen, bevor irgendwo `device[m.header.sender]` verwendet wird.

### 3. Kein Schuss ohne aktive No-Shot-Karte

Wenn keine gültige No-Shot-Karte aktiv ist, darf nicht geschossen werden.

Gewünschtes Verhalten:

- Wenn der Manager die Karte nicht liefert, soll die lokal gespeicherte Karte verwendet werden.
- Wenn keine lokale Karte vorhanden oder ladbar ist, darf der Sensor/Aktor nicht in einen Zustand kommen, in dem geschossen wird.
- Kein `fail-open` für die Schussentscheidung.
- Sensoren können optional weiter Status/Debug melden, aber die Feuerfreigabe muss fail-safe bleiben.

### 4. Doku zur VPS-/NVS-Pose anpassen

Die Dokumentation soll den tatsächlichen Mechanismus korrekt beschreiben:

- Eine hochwertige VPS-Pose überschreibt die gespeicherte NVS-Pose.
- Wenn die VPS-Messung schlecht ist, aber eine NVS-Pose vorhanden ist, wird die NVS-Pose als Fallback akzeptiert.
- Es gibt aktuell keinen echten Vergleich “NVS-Pose stimmt mit VPS-Pose innerhalb einer Toleranz überein”, ausser dieser wird später bewusst implementiert.

### 5. Pose-Request / Pose-Report bei Gerätearbeit nachziehen

Wenn an einem welt-fähigen Gerät oder Display gearbeitet wird, soll `poseRequest` / `poseReport` praktisch implementiert werden.

Ziel:

- Ein Gerät antwortet auf `poseRequest` mit seiner aktuellen `worldPosePayload`.
- Displays oder andere Geräte können damit relative Meldungen bei Bedarf in Weltkoordinaten umrechnen.
- Die Logik soll möglichst generisch in `xComProc6_3.h` liegen.

### 6. Heartbeat bei Geräten nachziehen, wenn man ohnehin daran arbeitet

Nicht alle Geräte senden aktuell selbst einen Heartbeat. Das ist nicht sofort kritisch, soll aber beim nächsten Arbeiten am jeweiligen Gerät mit erledigt werden.

Betroffen prüfen:

- Button
- LD06
- Displays
- Manager, falls er für Status/Discovery sichtbar sein soll

Ziel: Jedes relevante Gerät ist für Manager/Dashboard/IP-Lernen sichtbar.

## Bewusst nicht weiterverfolgen

### Review-Dateien nicht jetzt aufräumen

Die alten `REVIEW_6_3.md`-Dateien müssen nicht aktiv bereinigt oder mit Status versehen werden. Sie können als historische Hinweise stehen bleiben.

## Namenskonvention / Master

`xComProc6_3.h` ist der Master für gemeinsame Defines und Verhalten.

Wenn ein Sketch lokale Defines für gemeinsame Funktionen setzt, müssen sie zu den Namen passen, die `xComProc6_3.h` tatsächlich verwendet.

Beispiel:

- Nicht parallel `VPS_PORT` und `VPS_LOC_PORT` pflegen.
- Massgeblich ist die Benennung aus `xComProc6_3.h`.
- Lokale `hwDef.h`-Dateien sollen sich daran anpassen, nicht umgekehrt.
