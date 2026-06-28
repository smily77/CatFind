# CatFinder VPS — Dashboard (Treffervisualisierung)

Web-Dashboard, das die System-Ereignisse des CatFinder-Netzes zeigt. Die Daten
liefert der **Manager als Gateway** per HTTP-POST (`/ingest`); der lokale
Betrieb läuft unabhängig vom VPS weiter.

Erreichbar unter **`http://<VPS-IP>/`** (Port 80).

## Anzeige

- **Immer sichtbar:** scrollendes System-Debug-Fenster (die per Text-Multicast
  gebroadcasteten Statusmeldungen) + Liste der Geräte, die in den letzten 3 min
  einen HB gesendet haben.
- **Umschaltbar:**
  - **Liste** — alle `catObserved`, pro Minute zu einem Eintrag zusammengefasst
    (Zeit + meldende Sensor-IDs).
  - **Welt-Karte** — `catObserved` in Welt-Koordinaten über der `RasenKarte`.
  - **Gruppe 1/2/3** — `catObserved` in relativen Koordinaten der jeweiligen
    Koordinatengruppe.
  - Farbe je `(Sensor, Ziel)`-Kombination (Radar bis 3 Ziele).
  - **Reset** löscht die akkumulierten `catObserved` (alle Karten).

## Endpunkte

| Methode | Pfad | Zweck |
|---|---|---|
| `GET`  | `/` | Web-UI |
| `POST` | `/ingest` | Manager-Push `{events,debug,hb}` |
| `GET`  | `/state` | Debug + aktive Geräte + Minuten-Zusammenfassung |
| `GET`  | `/events?since=N` | neue catObserved ab Index N (für die Karten) |
| `POST` | `/reset` | akkumulierte Ereignisse löschen |
| `GET`  | `/map` | RasenKarte-Punkte (aus dem GitHub-Repo) |

State liegt im RAM (Ereignisse kumulieren bis Reset; gehen bei Container-Neustart
verloren). `RasenKarte.csv` wird aus dem Repo geladen (Fallback: ins Image
gebacken).

## Deployment

```bash
cd /opt/catfinder/dashboard
docker compose up -d --build
curl -s localhost/state | head
```
