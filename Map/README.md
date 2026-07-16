# Map/

Rohdaten + laufend aktuelle Betriebskarten (siehe `MapConcept.md` im Repo-Root
für den vollständigen Karten-Sync-Ablauf).

- `noshot.csv`, `rasen.csv` — Referenzkopien der Betriebskarten (Welt-mm, mit
  Header). Werden vom VPS automatisch committet, sobald der Manager eine neue
  Version übernommen hat (`/mapsync`). **Nicht die laufende Wahrheit** — die
  ist im LittleFS des Managers; diese Dateien sind Backup + Notweg-Basis
  (Repo-CSV von Hand anpassen, Firmware/LittleFS-Image neu flashen, wenn kein
  VPS erreichbar ist).
- `RasenKarte.csv` — Vermessungs-Original des Rasen-Umrisses in **Metern**,
  Quelle für den ersten `rasen.csv`-mm-Import und für den VPS-Localizer
  (`VPS/localizer`, eigener Anwendungsfall, unabhängig vom Karten-Sync).
- `karte_*.csv`, `Landmark.csv`, `All/` — Vermessungs-Rohdaten.
