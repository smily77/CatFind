# Map/

Vermessungs-Rohdaten + Backup-Kopien der Betriebskarten. **Keine Source of
truth** — die liegt bei `Controller/Manager6_3_0/data/` (siehe
`Controller/Manager6_3_0/KartenUpload.md` und `MapConcept.md` im Repo-Root
für den vollständigen Karten-Sync-Ablauf und den Single-Source-of-Truth-
Gedanken dahinter).

- `backup/noshot.csv`, `backup/rasen.csv` — **reine Sicherheitskopien** des
  aktuell aktiven Kartenstands, den der Manager zuletzt per `/mapsync`
  bestätigt hat (wird bei jeder Änderung überschrieben). Der VPS committet
  sie automatisch mit, zusätzlich zum eigentlichen Ziel
  `Controller/Manager6_3_0/data/`. Diese Dateien von Hand zu editieren hat
  **keine Wirkung** auf den Manager — dafür immer
  `Controller/Manager6_3_0/data/<typ>.csv` verwenden.
- `backup/history/<typ>_v<NNNNN>.csv` — **eine unveränderliche Datei pro
  Version** (z. B. `noshot_v00013.csv`), vom VPS bei jeder bestätigten
  Änderung zusätzlich angelegt, nie überschrieben. Zum Durchblättern alter
  Kartenstände ohne Git-Kenntnisse (Datei-Explorer/GitHub-Weboberfläche).
  Dieselbe Historie steckt auch schon im Git-Log von `backup/<typ>.csv`
  bzw. `Controller/Manager6_3_0/data/<typ>.csv` (`git log`/`git show`) —
  dieser Ordner ist bewusst redundant dazu, für den schnellen Zugriff ohne
  Kommandozeile. Wächst unbegrenzt (kleine Textdateien, bislang keine
  Rotation/Aufräumung).
- `RasenKarte.csv` — Vermessungs-Original des Rasen-Umrisses in **Metern**,
  Quelle für den ursprünglichen `rasen.csv`-mm-Import und für den
  VPS-Localizer (`VPS/localizer`, eigener Anwendungsfall, unabhängig vom
  Karten-Sync).
- `karte_*.csv`, `Landmark.csv`, `All/` — sonstige Vermessungs-Rohdaten.
