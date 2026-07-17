# Map/

Vermessungs-Rohdaten + Backup-Kopien der Betriebskarten. **Keine Source of
truth** — die liegt bei `Controller/Manager6_3_0/data/` (siehe
`Controller/Manager6_3_0/KartenUpload.md` und `MapConcept.md` im Repo-Root
für den vollständigen Karten-Sync-Ablauf und den Single-Source-of-Truth-
Gedanken dahinter).

- `backup/noshot.csv`, `backup/rasen.csv` — **reine Sicherheitskopien** des
  Kartenstands, den der Manager zuletzt per `/mapsync` bestätigt hat. Der VPS
  committet sie automatisch mit, zusätzlich zum eigentlichen Ziel
  `Controller/Manager6_3_0/data/`. Diese Dateien von Hand zu editieren hat
  **keine Wirkung** auf den Manager — dafür immer
  `Controller/Manager6_3_0/data/<typ>.csv` verwenden.
- `RasenKarte.csv` — Vermessungs-Original des Rasen-Umrisses in **Metern**,
  Quelle für den ursprünglichen `rasen.csv`-mm-Import und für den
  VPS-Localizer (`VPS/localizer`, eigener Anwendungsfall, unabhängig vom
  Karten-Sync).
- `karte_*.csv`, `Landmark.csv`, `All/` — sonstige Vermessungs-Rohdaten.
