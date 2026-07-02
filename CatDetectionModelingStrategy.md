# CatDetection Modeling Strategy

## Ziel

CatFind soll aus rohen Sensorbeobachtungen (`catObserved`) robuste lokale Katzenereignisse (`CatDetected`) erzeugen.

Die Live-Entscheidung `CatDetected` muss autonom im lokalen CatFind-Netz laufen und darf nicht vom VPS abhängen. Der VPS dient nur für Logging, Visualisierung, Replay, Review, Labeling, Parameterfindung und Modellbewertung.

Das CatFind-Netz muss auch ohne VPS eigenständig arbeiten. Der VPS darf nicht Teil der schnellen Live-Entscheidungskette sein, weil der Weg CatFind-Netz → Manager → VPS → zurück ins CatFind-Netz für `CatDetected` zu langsam ist.

## Grundprinzip

Es gibt zwei getrennte Ebenen.

### 1. Entwicklungs-/Analyseebene auf dem VPS

Der VPS soll:

- `catObserved`-Events dauerhaft speichern,
- Tracks offline/replay bilden,
- Sensor-/Aktorbereiche visualisieren,
- Störsignaturen analysieren,
- Track-Features berechnen,
- vorläufige Klassifikationen vorschlagen,
- manuelles Review/Labeling ermöglichen,
- Parameter und Regeln für die lokale Live-Detection ableiten.

### 2. Lokale Live-Ebene im CatFind-Netz

Ein lokaler DetectionActor/CatFusionNode soll:

- `catObserved` direkt im CatFind-Netz empfangen,
- Tracks lokal bilden,
- Sensor-/Aktorprofile nutzen,
- Track-Scores lokal berechnen,
- `CatDetected` lokal senden,
- ohne VPS funktionieren.

Zielplattform für die lokale Live-Ebene ist später entweder:

- ein ESP32 DetectionActor, wenn die Regeln kompakt und Arduino-tauglich bleiben,
- oder ein Raspberry Pi Zero 2 W CatFusionNode, wenn Python/mehr Rechenleistung/flexiblere Modellierung lokal notwendig wird.

## Teil 1: Zentrale Sensor-/Aktorprofile in `xComDef6_3.h`

### Entscheidung

Die geometrische Charakteristik von Sensoren und Aktoren muss verbindlich in der zentralen CatFind-Gerätedatenbank in `xComDef6_3.h` hinterlegt werden.

`xComDef6_3.h` ist die Quelle der Wahrheit.

Der VPS darf daraus eine JSON-/Analyse-Konfiguration ableiten, aber keine unabhängige, abweichende Sensor-Konfiguration führen.

Grund: Neue Sensoren sollen zentral in der CatFind-Gerätedatenbank beschrieben werden.

### Ausgangspunkt

`xComDef6_3.h` enthält bereits:

- Geräte-IDs,
- Gerätetypen,
- relative Koordinatengruppen,
- die zentrale `stationDefinitions device[]`-Tabelle.

`posPayload` enthält bereits:

- lokale Koordinaten `x/y`,
- Polarkoordinaten `radius/angle`,
- Target-Speed,
- weitere Radar-/Sensorwerte,
- Weltkoordinaten `worldX/worldY`,
- `worldValid`,
- Sensor-/Target-Slot.

Diese Struktur soll nicht unnötig aufgebläht werden. Sensor- und Aktorprofile sollen bevorzugt als zusätzliche statische Tabellen ergänzt werden, indexiert nach Geräte-ID.

### Vorgeschlagene Struktur

Nicht alles direkt in `stationDefinitions` packen.

Bevorzugt:

```cpp
sensorProfile sensorProfiles[DEVICE_COUNT];
actorProfile actorProfiles[DEVICE_COUNT];
```

oder eine vergleichbare saubere Struktur.

`stationDefinitions device[]` bleibt die Geräteliste.

Die neuen Tabellen ergänzen pro Geräte-ID die geometrischen und funktionalen Eigenschaften.

### Sensorprofil: Mindestfelder

Ein Sensorprofil soll mindestens enthalten:

```text
hasSensorProfile
sensorKind
fovLeftDeg
fovRightDeg
rangeMinMm
rangeMaxMm
rangeSoftMm
nearNoiseMm
edgeAngleToleranceDeg
edgeRangeToleranceMm
maxTargets
sensorWeight
flags
```

#### Bedeutung

| Feld | Zweck |
|---|---|
| `hasSensorProfile` | Gerät hat überhaupt ein Sensorprofil |
| `sensorKind` | Radar, Lidar, US, IR, Kamera, Simulator usw. |
| `fovLeftDeg` / `fovRightDeg` | Sichtbereich relativ zur lokalen Geräteachse |
| `rangeMinMm` | Mindestdistanz / Deadzone |
| `rangeMaxMm` | nominelle Maximalreichweite |
| `rangeSoftMm` | weiche Toleranz über die nominelle Reichweite hinaus |
| `nearNoiseMm` | Nahbereich, in dem Insekten/Clutter wahrscheinlicher sind |
| `edgeAngleToleranceDeg` | Toleranz am FOV-Rand |
| `edgeRangeToleranceMm` | Toleranz am Reichweitenrand |
| `maxTargets` | z. B. Radar bis 3 Ziele |
| `sensorWeight` | Gewichtung für Fusion/Detection |
| `flags` | z. B. `FOV_SOFT_RANGE`, `CAN_DEGRADE`, `PRIMARY_CAT_SENSOR`, `POSE_REFERENCE` |

### Startprofil: aktueller Radar

Für den aktuellen Radar, z. B. Dome/Radar-HKL, soll als Startprofil verwendet werden:

```text
sensorKind = radar
fovLeftDeg = -60
fovRightDeg = +60
rangeMinMm = deadZone bzw. 500
rangeMaxMm = 7000
rangeSoftMm = z. B. 1000
nearNoiseMm = z. B. 700
edgeAngleToleranceDeg = z. B. 10
edgeRangeToleranceMm = z. B. 700
maxTargets = 3
sensorWeight = hoch
flags = PRIMARY_CAT_SENSOR | FOV_SOFT_RANGE | CAN_DEGRADE
```

Die Werte sind Startwerte und sollen später durch VPS-Analyse verifiziert oder angepasst werden.

### Startprofil: Lidar-C1

Für `LidarC1` soll festgehalten werden:

```text
sensorKind = lidar360
fovLeftDeg = -180
fovRightDeg = +180
rangeMinMm = passend zum Lidar/Detektor
rangeMaxMm = passend zum Lidar/Detektor
rangeSoftMm = passend zum Lidar/Detektor
maxTargets = passend zum Detektor
sensorWeight = niedrig/mittel für direkte CatDetection
flags = POSE_REFERENCE | CAN_DEGRADE | DEGRADED_BY_SUN_RISK
```

Wichtig:

- Lidar-C1 ist stark für WorldPose und Radar-Kalibrierung.
- Lidar-C1 ist schwächer als direkter Katzendetektor, weil Katzen je nach Fell/Reflektivität nicht zuverlässig erkannt werden und Sonnen-/Burst-Störungen auftreten können.
- Lidar-C1 darf CatDetection unterstützen, soll aber nicht als zwingende Bestätigung verlangt werden.

### Weitere Sensoren

Neue Sensoren müssen künftig zentral in `xComDef6_3.h` ein Sensorprofil erhalten.

Beispiele:

```text
US-Sensor:
  sensorKind = ultrasonic
  enger FOV
  kurze Reichweite
  gut als Nahbereichs-/Durchgangsbestätigung

ESP32-CAM:
  sensorKind = camera
  FOV abhängig vom Objektiv
  primär Ground-Truth-/Review-Trigger, nicht zwingend Live-Detector

zweites Radar:
  sensorKind = radar
  eigenes FOV
  eigene Reichweite
  eigene Gewichtung
```

## Teil 2: Aktorprofile in `xComDef6_3.h`

### Ziel

Nicht nur Sensoren, sondern auch Aktoren sollen eine statische Charakteristik bekommen.

Diese Profile dienen später für:

- Reichweitenprüfung,
- Auswahl des passenden Aktors,
- `FireAllowed`,
- Zielpriorisierung,
- Reaktionszeitabschätzung,
- Sicherheitslogik.

### Aktorprofil: Mindestfelder

Ein Aktorprofil soll mindestens enthalten:

```text
hasActorProfile
rangeMinMm
rangeMaxMm
leftLimitDeg
rightLimitDeg
nearLimitMm
farLimitMm
reactionTimeMs
sprayWidthMm
flags
```

#### Bedeutung

| Feld | Zweck |
|---|---|
| `hasActorProfile` | Gerät hat ein Aktorprofil |
| `rangeMinMm` | minimale sinnvolle Wasser-/Aktionsdistanz |
| `rangeMaxMm` | maximale sinnvolle Wasser-/Aktionsdistanz |
| `leftLimitDeg` / `rightLimitDeg` | statische Turm-/Ausrichtungslimits |
| `nearLimitMm` / `farLimitMm` | Default-Limits |
| `reactionTimeMs` | Zeit bis Wasserstrahl wirksam ist |
| `sprayWidthMm` | grobe Breite/Unsicherheit des Wasserstrahls |
| `flags` | z. B. `CAN_FIRE`, `HAS_LIMITS`, `NEEDS_WORLDPOSE` |

### Beziehung zu bestehenden HB-Daten

Bestehende HB-Daten wie `readyToFire`, `limitsActive`, `leftLimit`, `rightLimit`, `farLimit`, `nearLimit` bleiben dynamische Zustände.

Die neuen Aktorprofile sind statische Default-/Modellinformationen.

Dynamische HB-Werte überschreiben bzw. ergänzen statische Profilwerte zur Laufzeit.

## Teil 3: Gemeinsame Hilfsfunktionen für Profile

Die KI soll gemeinsame Funktionen bereitstellen, vorzugsweise in `xComProc6_3.h` oder einer passenden gemeinsamen Datei.

### Beispiele

```text
bool hasSensorProfile(uint8_t deviceId)
const sensorProfile& getSensorProfile(uint8_t deviceId)

bool hasActorProfile(uint8_t deviceId)
const actorProfile& getActorProfile(uint8_t deviceId)

bool pointInSensorFootprint(uint8_t deviceId, int32_t localX, int32_t localY)
float sensorFootprintScore(uint8_t deviceId, int32_t localX, int32_t localY)

bool pointInActorRange(uint8_t actorId, int32_t localX, int32_t localY)
float actorReachabilityScore(uint8_t actorId, int32_t localX, int32_t localY)
```

Für ESP32 müssen diese Funktionen leichtgewichtig sein.

Keine unnötigen dynamischen Allokationen.

Keine komplexen Abhängigkeiten.

## Teil 4: VPS muss Profile aus `xComDef6_3.h` ableiten

### Ziel

Der VPS soll nicht eine eigene Sensor-Wahrheit führen.

Die führende Datenquelle ist `xComDef6_3.h`.

### Mögliche Lösungen

#### Option A: Python-Parser für `xComDef6_3.h`

Der VPS liest die Profile direkt aus der Headerdatei.

Vorteil:

- nur eine Datenquelle.

Nachteil:

- C++-Header zuverlässig zu parsen kann aufwendig sein.

#### Option B: Generierte JSON-Datei

Ein Skript erzeugt aus der zentralen Definition eine JSON-Datei:

```text
VPS/dashboard/sensor_profiles.generated.json
```

oder:

```text
VPS/trackrecognition/sensor_profiles.generated.json
```

Vorteil:

- VPS kann komfortabel JSON lesen.
- `xComDef6_3.h` bleibt führend.
- Gut testbar.

Nachteil:

- Generator muss gepflegt werden.

#### Option C: Manuelle Spiegeldatei

Nur als Notlösung.

Dann muss klar dokumentiert sein:

```text
xComDef6_3.h ist führend.
Die JSON-Datei ist nur eine Kopie und muss synchron gehalten werden.
```

### Empfehlung

Bevorzugt Option B:

```text
xComDef6_3.h -> Generator-Skript -> sensor_profiles.generated.json
```

## Teil 5: VPS-Datenerfassung persistent machen

### Ziel

Der VPS soll alle vom Manager an `/ingest` gelieferten `catObserved`-Events persistent speichern.

Aktuell dient das Dashboard primär der Anzeige. Für Trackrecognition müssen die Daten dauerhaft verfügbar sein.

### Anforderungen

1. `/ingest` bleibt kompatibel zum bestehenden Manager-Gateway.
2. Alle Events werden persistent gespeichert.
3. Speicherung nicht in GitHub.
4. Speicherung in Docker-Volume oder Host-Verzeichnis auf dem VPS.
5. Geeignetes Format:
   - SQLite bevorzugt,
   - optional zusätzlich JSONL-Rohlogs pro Tag.
6. Ereignisse müssen mindestens speichern:
   - Serverzeit,
   - Sender-ID,
   - Sensor-/Target-Slot,
   - lokale Koordinaten `x/y`,
   - Weltkoordinaten `worldX/worldY`,
   - `worldValid`,
   - Koordinatengruppe,
   - falls verfügbar Target-Speed,
   - falls verfügbar Radar-/Sensorqualität.

## Teil 6: VPS-Trackrecognition für Analyse und Replay

### Ziel

Der VPS soll anhand gespeicherter `catObserved`-Events Tracks bilden und bewerten.

Es geht zunächst nicht um Live-Feuerfreigabe, sondern um:

- Modellverständnis,
- Störungsanalyse,
- Parametertuning,
- Review.

### Trackbildung

Anforderungen:

1. Events nach Zeitfenstern laden.
2. Weltkoordinaten bevorzugen, wenn `worldValid=1`.
3. Lokale Koordinaten für Sensor-Footprint-Prüfung verwenden.
4. Tracks per Nearest-Neighbor/Gating bilden.
5. Mehrere aktive Tracks unterstützen.
6. Trackverlust erkennen.
7. Trackstart und Trackende speichern.

### Track-Features

Für jeden Track berechnen:

```text
track_id
start_time
end_time
duration_ms
num_points
source_sensors
primary_sensor
start_x/start_y
end_x/end_y
net_displacement_mm
path_length_mm
mean_speed_mm_s
max_speed_mm_s
acceleration_score
turn_score
fov_entry_score
fov_exit_score
fov_crossing_score
inside_sensor_footprint_ratio
near_sensor_ratio
stationary_score
clutter_score
storm/degraded_sensor_state
classification_candidate
confidence
reason_flags
```

### Sensor-FOV-Logik

Insbesondere für Radar:

1. Track beginnt nahe FOV-Kante → Bonus.
2. Track endet nahe FOV-Kante → Bonus.
3. Track überstreicht großen Teil des FOV → Bonus.
4. Track bleibt lange in Nahzone → Malus.
5. Track entsteht und verschwindet mitten im FOV → Malus, aber kein harter Ausschluss.
6. Track hat geringe Netto-Verschiebung → starker Malus.
7. Track springt unphysikalisch → starker Malus.

Die Radar-Reichweite ist weich zu behandeln.

Der Öffnungswinkel ist stärker zu gewichten als die nominelle Reichweite.

### Störungsfeatures

Typische Störungen:

```text
Einzelereignis:
  1 Punkt, keine Fortsetzung

Insekt:
  Nahbereich, erratisch, kurze Dauer

Vegetation:
  viele Punkte, geringe Netto-Verschiebung, oszillierend

Vogel:
  schnelle/unplausible Bewegung oder nur kurzer Track

Sonne/Lidar:
  Burst, viele Punkte gleichzeitig, weite Winkelstreuung

Regen/Sturm:
  viele Sensoren rauschen gleichzeitig
```

## Teil 7: Dashboard-Review und Labeling

### Ziel

Das Dashboard soll Tracks reviewbar machen.

### Anforderungen

1. Rohpunkte anzeigen.
2. Tracks als Linien anzeigen.
3. Sensor-FOV-Sektoren anzeigen.
4. Trackdetails anzeigen:
   - Score,
   - Features,
   - Klassifikationsvorschlag,
   - Gründe/Flags.
5. Manuelles Labeling ermöglichen:
   - Katze,
   - keine Katze,
   - Insekt,
   - Vogel,
   - Vegetation,
   - Sonne/Lidar,
   - Regen/Sturm,
   - unbekannt.
6. Labels persistent speichern.
7. Zeitfenster/Replay ermöglichen.
8. Export von gelabelten Tracks ermöglichen.

## Teil 8: Parameter-/Modell-Export

### Ziel

Aus VPS-Analyse und Review sollen lokale Detection-Parameter abgeleitet werden.

### Beispiel-Export

```json
{
  "model_version": "catdetect_0_1",
  "profile_version": "xcomdef_6_3_profiles_001",

  "confirm_min_points": 4,
  "confirm_window_ms": 800,

  "min_net_displacement_mm": 400,
  "speed_min_mm_s": 100,
  "speed_max_mm_s": 4000,

  "fov_entry_bonus": 25,
  "fov_exit_bonus": 25,
  "fov_crossing_bonus": 20,
  "mid_fov_birth_penalty": 10,
  "mid_fov_death_penalty": 10,

  "near_sensor_penalty": 20,
  "stationary_reject_mm": 250,

  "fusion_distance_mm": 500,
  "fusion_time_ms": 500,

  "storm_block_ms": 3000
}
```

Der Export muss angeben, welche `xComDef6_3.h`-Profilversion zugrunde liegt.

## Teil 9: Entscheidung Zielplattform lokal

Nach Datenanalyse wird entschieden.

### ESP32 DetectionActor, wenn:

- die Regeln kompakt bleiben,
- wenige aktive Tracks ausreichen,
- kein komplexes Python lokal nötig ist,
- maximale Robustheit und Arduino-Nähe wichtig sind,
- geringer Stromverbrauch und einfache Integration Priorität haben.

### Pi Zero 2 W CatFusionNode, wenn:

- die Logik komplexer bleibt,
- Python lokal hilfreich ist,
- Sensorfusion umfangreicher wird,
- Parameterdateien ohne Flash geändert werden sollen,
- spätere ML-/Scoremodelle wahrscheinlich sind.

## Teil 10A: Lokaler ESP32 DetectionActor

Falls ESP32 gewählt wird:

### Anforderungen

1. Neuer ESP32-Sketch `DetectionActor`.
2. Nutzt `xComDef6_3.h` mit statischen Sensor-/Aktorprofilen.
3. Empfängt `catObserved` im CatFind-Netz.
4. Bildet lokale Tracks.
5. Nutzt exportierte Detection-Parameter.
6. Nutzt Sensor-FOV-/Reichweitenlogik.
7. Sendet `CatDetected` lokal im CatFind-Netz.
8. Optional sendet:
   - `CatTrackUpdate`,
   - `CatTrackLost`.
9. Sendet Heartbeat und Debug-Status.
10. Funktioniert vollständig ohne VPS.

## Teil 10B: Lokaler Pi Zero 2 W CatFusionNode

Falls Pi gewählt wird:

### Anforderungen

1. Pi Zero 2 W läuft als lokale Appliance.
2. Dienst startet automatisch beim Boot.
3. Dienst empfängt UDP/xCom `catObserved`.
4. Dienst nutzt aus `xComDef6_3.h` generierte Sensor-/Aktorprofile.
5. Dienst nutzt exportierte Detection-Parameter.
6. Dienst bildet Tracks lokal.
7. Dienst sendet `CatDetected` lokal per UDP/xCom.
8. Dienst sendet Heartbeat/Status.
9. Dienst funktioniert vollständig ohne VPS.
10. Optional puffert er Events lokal, wenn VPS nicht erreichbar ist.

Der Pi soll im Alltag nicht manuell administriert werden müssen.

Ziel:

```text
Strom an -> Pi bootet -> CatFusion-Dienst startet -> empfängt catObserved -> sendet CatDetected
```

## Teil 11: Neues Eventmodell `CatDetected`

### Ziel

`xComDef6_3.h` soll um ein neues Event für validierte Katzen-Detektion erweitert werden.

`catObserved` bleibt Roh-/Sensorereignis.

`CatDetected` bedeutet:

```text
Das System hat aus Beobachtungen/Tracks geschlossen:
Wahrscheinlich echte Katze.
```

`CatDetected` ist aber nicht automatisch Feuerfreigabe.

PowerActor muss weiterhin prüfen:

- No-Shot,
- Aktorreichweite,
- Turmlimits,
- Sicherheit,
- Bereitschaft,
- ggf. eigene WorldPose.

### Event-Vorschlag

```cpp
#define catDetected 13
#define catTrackUpdate 14
#define catTrackLost 15
```

Alternativ ein gemeinsames Track-Event mit Statusfeld.

### Payload-Mindestfelder

```text
trackId
worldX
worldY
worldValid
confidence
primarySensor
sourceMask
ageMs
speedMmS
flags
```

Optional:

```text
vx
vy
classification
reasonFlags
trackState
```

### Confidence

Mögliche Werte:

```text
LOW
MEDIUM
HIGH
```

oder numerisch:

```text
0..100
```

### ReasonFlags

Beispiele:

```text
TRACK_CONTINUOUS
FOV_ENTRY_OK
FOV_EXIT_OK
FOV_CROSSING_OK
SPEED_OK
NET_DISPLACEMENT_OK
MULTISENSOR_CONFIRMED
NEAR_SENSOR_PENALTY
STATIONARY_PENALTY
SENSOR_DEGRADED
```

## Teil 12: PowerActor-Anpassung

Langfristig sollen PowerActors nicht mehr direkt auf rohe `catObserved` schießen.

Ziel:

```text
catObserved -> DetectionActor/CatFusionNode -> CatDetected -> PowerActor
```

PowerActor:

1. empfängt `CatDetected`,
2. prüft eigene Pose,
3. prüft No-Shot,
4. prüft Aktorprofil/Reichweite,
5. prüft Limits,
6. richtet Wasserstrahl,
7. feuert nur bei `FireAllowed`.

## Reihenfolge der Umsetzung

1. `xComDef6_3.h` um Sensor-/Aktorprofile erweitern.
2. Hilfsfunktionen für Profile ergänzen.
3. VPS so erweitern, dass er Profile aus `xComDef6_3.h` oder einer daraus generierten JSON-Datei nutzt.
4. VPS-Logging persistent machen.
5. Einige Tage/Wochen Daten sammeln.
6. VPS-Trackrecognition und Dashboard-Review bauen.
7. Tracks reviewen und labeln.
8. Parameter extrahieren und versioniert exportieren.
9. Entscheiden: ESP32 DetectionActor oder Pi Zero 2 W CatFusionNode.
10. Lokale Live-Detection implementieren.
11. `CatDetected` lokal ins CatFind-Netz senden.
12. PowerActor auf `CatDetected` umstellen.

## Wichtige Architekturregeln

1. `catObserved` bleibt Roh-/Sensorereignis.
2. `CatDetected` ist validierter Track.
3. `CatDetected` wird lokal erzeugt, nicht auf dem VPS.
4. VPS ist Entwicklungs-, Logging-, Replay- und Analysewerkzeug.
5. Sensor-/Aktorprofile sind zentral in `xComDef6_3.h`.
6. Neue Sensoren müssen dort ein Profil bekommen.
7. Der VPS nutzt aus `xComDef6_3.h` abgeleitete Profile.
8. Große Rohdaten und Labels gehören nicht nach GitHub.
9. Persistente VPS-Daten liegen in Docker-Volume oder Host-Verzeichnis.
10. PowerActor prüft trotz `CatDetected` weiterhin Sicherheit, No-Shot und Aktorlimits.

## Erwartetes Ergebnis

Am Ende existiert:

1. Eine zentrale Sensor-/Aktorprofilierung in `xComDef6_3.h`.
2. Persistentes VPS-Logging aller relevanten `catObserved`-Events.
3. VPS-Trackrecognition mit Replay, Review und Labeling.
4. Ein exportierbarer Parameter-/Modellsatz.
5. Ein lokaler DetectionActor oder CatFusionNode.
6. Ein neues `CatDetected`-Event.
7. Eine lokale autonome Live-Erkennung ohne VPS-Abhängigkeit.
8. Eine klare Trennung zwischen:
   - Beobachtung (`catObserved`),
   - Detektion (`CatDetected`),
   - Feuerfreigabe (`FireAllowed` / PowerActor-Logik).
