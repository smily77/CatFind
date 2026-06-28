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
| `GET`  | `/health` | Status + geladene Kartenpunkte |
| `POST` | `/reload` | Karte sofort neu aus dem Repo laden |

Antwort von `/localize`:
```json
{ "x_mm":3175, "y_mm":8822, "heading_deg":283.4, "mirror":-1,
  "confidence":"HOCH", "inlier_ratio":0.57, "beams":143 }
```

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
