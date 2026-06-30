# CatFinder VPS — Lokalisierungsdienst

HTTP-Service, der einen 360-Bin Lidar-Scan gegen die Rasenkarte global mit einem
korrelativen Scan-Matcher (Likelihood-Field) zur Deckung bringt und die
wahrscheinlichste Pose zurückgibt. Portiert aus
`Lidar_C1_Prog/Position_estimate/lidar_localize.py`.

Der Sensor (`C1Lidar6_3_0`) ruft den Dienst beim Boot auf, um seine
**Welt-Pose** (X, Y, Heading, Drehsinn) zu bestimmen.

## Endpunkte

| Methode | Pfad | Zweck |
|---|---|---|
| `POST` | `/localize` | Body `{"scan":[360 ints mm]}` → Pose-JSON |
| `POST` | `/calibrate` | Body Trajektorien → Co-Observation-Pose (Radar etc.) |
| `GET`  | `/health` | Status + geladene Kartenpunkte |
| `POST` | `/reload` | Karte sofort neu aus dem Repo laden |

Antwort von `/localize`:
```json
{ "x_mm":3175, "y_mm":8822, "heading_deg":283.4, "mirror":-1,
  "confidence":"HOCH", "inlier_ratio":0.57, "beams":143 }
```

### `/calibrate` — Welt-Pose per Co-Observation

Ein nicht selbst-lokalisierender Sensor (Radar, Turm-Sensor …) schickt **seine
eigenen** Detektionsbahnen (relativ, mm) und die gleichzeitig beobachteten Bahnen
**welt-posierter** Sensoren (Welt-mm, je Sender getrennt). Der Dienst richtet die
Bahnen per Trajektorien-Registrierung aus (ICP mit reihenfolge-erhaltender
DTW-Korrespondenz + Umeyama/Kabsch, über `mirror = ±1`) und liefert die beste
starre Transformation `welt = R(heading)·[x, mirror·y] + (tx,ty)` — konsistent mit
`localToWorld()` in `xComProc6_3.h`.

```json
// Request
{ "radar_tracks": [ [[x,y],…], … ],                 // bis zu 3 Eigen-Spuren (relativ mm)
  "world_tracks": [ {"id":7, "pts":[[x,y],…]}, … ] } // je welt-posiertem Sender eine Bahn (Welt-mm)

// Antwort
{ "ok":true, "source":7, "tx_mm":4989, "ty_mm":-1984, "heading_deg":39.4,
  "mirror":1, "confidence":"HOCH", "inlier_ratio":0.98, "linearity":0.21 }
```

Der VPS probiert **jede (Eigen-Spur × Welt-Quelle)-Kombination** durch (RANSAC-artige
Auswahl) und gibt die mit dem höchsten Inlier-Anteil zurück; `source` nennt den
gewinnenden Sender. `confidence` = `HOCH` nur bei hohem Inlier-Anteil **und**
ausreichender 2-D-Struktur der Bahn (`linearity ≥ CAL_MIN_LINEAR`) — eine **gerade
Linie ist mehrdeutig** und wird nie `HOCH`. Das Gerät übernimmt die Pose nur bei
`HOCH` (Quality-Gate in `coCalibFinish`).

**Bekannte Grenze:** DTW erzwingt Matching der Bahn-Endpunkte; starten/enden die
beiden Sensoren stark versetzt, verschlechtert das den Fit an den Rändern. Eine
gemeinsam und vollständig beobachtete, kurvige Bahn (Acht/L) ist daher wichtig.

Parameter per Env: `CAL_INLIER_MM` (300), `CAL_MIN_PTS` (12), `CAL_RESAMPLE_N` (80),
`CAL_ICP_ITERS` (40), `CAL_MIN_LINEAR` (0.01).

## Karte

Beim Start (und alle `MAP_REFRESH_S`, Default 600 s, sowie per `/reload`) wird
`Map/RasenKarte.csv` aus dem GitHub-Repo geladen
(`MAP_URL`, Default `…/smily77/CatFind/main/Map/RasenKarte.csv`).
**Kartenänderung = `git push`** — kein Eingriff am VPS nötig. Ist das Repo nicht
erreichbar, greift die ins Image gebackene `RasenKarte.csv` als Fallback.

## Deployment

```bash
# auf dem VPS (Docker + compose vorausgesetzt)
cd /opt/catfinder/localizer
docker compose up -d --build
curl -s localhost:8080/health
```

Der Dienst lauscht auf Port **8080** (offen, kein Token — der VPS ist nur für
diese Aufgabe da). Erreichbar unter `http://<VPS-IP>:8080/`.

## Test

```bash
curl -s -X POST http://<VPS-IP>:8080/localize \
     -H 'Content-Type: application/json' \
     --data @scan.json          # scan.json = {"scan":[ ...360 ints... ]}
```
