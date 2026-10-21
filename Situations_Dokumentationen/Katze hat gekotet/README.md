# Katze hat gekotet – Track 384530 (16.10.2026, 21:18)

Das erste Mal, dass CatFinder eine Katze beim Koten aufgezeichnet hat. Dieser Fall ist
die wichtigste Referenz, um das Koten vom normalen Durchgangsverkehr zu unterscheiden.

## Kurzfassung

| | |
|---|---|
| **Track** | Nr **384530** (VPS-Analyse-Tab) |
| **Datum / Zeit** | Freitag, 16.10.2026, ca. 21:18:30 – 21:19:40 |
| **Tier** | **Schwarze Katze**, laut Beobachtung des Users die Kot-Verursacherin |
| **Kotplatz** | ca. **(6150, 9780) mm** in Weltkoordinaten, Fund des Users am Folgetag |
| **Sitzphase** | ca. **37 s** fast ohne Ortsänderung |
| **Track-Kennzahlen** | 66 P, n = 307 Punkte, 58 s, 1,7 m Weg, 0,2 m/s |
| **CatIdentifier** | `catDetected` ausgelöst (2 × CatDetected im Fenster, Übereinstimmung 5/5) |
| **Sensoren** | Dome (Radar); Bilder von KIVision mit Dome-Weltkoordinaten |

## Was den Track ausmacht

Screenshot der Track-Ansicht: `Screenshot 2026-10-17 101221.png` (gelber Track oben).

- **Langsam und kurz:** nur 1,7 m in 58 s (0,2 m/s). Ein normaler Durchgang ist mehrere
  Meter lang und dauert wenige Sekunden (siehe `../Normale_Katzenbewegungen/`, Track 384514).
- **Gewundener Weg** mit einem Haken statt einer geraden Linie über den Rasen.
- **Langes Stillsitzen** an einer Stelle (Modell-Begründung „+8 sitzt still (koten?)“).
- Begründung des Modells für den Score von 66:
  `+30 kohärente Bewegung`, `+12 durchquert das Feld`, `+8 langer Track`,
  `−12 mitten in der Abdeckung aufgetaucht`, `+8 sitzt still (koten?)`.
- **Abseits vom Katzenweg:** Die beiden Tigerkatzen laufen fast immer denselben Weg. Die
  schwarze Katze hält sich etwas daneben auf.
- Danach folgt Track 384534 (18 s, 6,3 m, 1,6 m/s, ebenfalls `catDetected`). Das ist
  der grüne Track weiter unten im Screenshot. Möglicherweise ist es dieselbe Katze
  beim Weggehen; das ist aber **nicht geprüft**.

## Bilder

### KIVision (Pi-Kamera, IR-Nacht)

Die Bilder wurden vom Radar-Ereignis ausgelöst. In der Fusszeile stehen die Dome-Weltkoordinaten
des Radarziels in Metern.

| Datei | Zeit | Dome Welt (x/y m) | Inhalt |
|---|---|---|---|
| `20261016_211833_00.jpg` | 21:18:33 | 7,3 / 10,8 | Bild 1, Modell: nichts ≥ 20 %, 1 Bewegungsfleck oben rechts |
| `20261016_211836_00.jpg` | 21:18:36 | 7,7 / 10,0 | Bild 2, viele Bewegungsflecken (IR schaltet um/Belichtung springt), „car 44 %“ = Mäher in der Station |
| `20261016_211837_00.jpg` | 21:18:37 | 7,2 / 9,9 | Bild 3, nur „car 67 %“ = Mäher |
| `20261016_211840_00.jpg` | 21:18:40 | 6,6 / 9,7 | Bild 4, Modell: nichts ≥ 20 % |
| `20261016_211953_00.jpg` | 21:19:53 | 4,9 / 8,5 | Bild 5, mehrere kleine Bewegungsflecken oben rechts im Dunkeln |

**Wichtig:** Auf keinem KIVision-Bild ist die Katze erkennbar. Der Bereich oben rechts
liegt ausserhalb des IR-Lichtkegels und ist praktisch schwarz. Eine schwarze Katze ist dort
für Kamera und Coral-Modell unsichtbar. Das einzige erkannte Objekt („car“) ist der Mäher
in seiner Station. Die Radarpositionen laufen von (7,3 / 10,8) nach (6,6 / 9,7). Das passt
grob zum Kotplatz bei (6,15 / 9,78).

### Wildkamera

`image.jpg`: Wildkamera, 16.10.2026, 21:19:44, 11 °C. Ein dunkles Tier läuft vor der
Hecke über den Rasen. Das ist das einzige Bild, auf dem das Tier selbst zu sehen ist.

### Nicht im Repo

KIVision-Treffer vom **07.10.2026, 19:06:05** („cat 48 %“, rechts oben an der Hecke).
Er zeigt vermutlich dieselbe schwarze Katze. Das Original liegt **nur auf dem Pi**:
`~/kivision/hits/20261007_190605_48.jpg`. Der User will es bewusst **nicht** auf GitHub haben.

## Vorbehalte

- Viele Radar-Tracks zeigen eine ca. 37 s lange Haltephase bei etwa (7300, 10600) mm.
  Diese stammt vermutlich vom Radar selbst und ist keine echte Sitzphase. Das Stillsitzen
  allein reicht deshalb **nicht** als Kot-Merkmal. Hier passt aber der Kotfund zum Ort.
- Dass die Katze schwarz ist, stammt aus der Beobachtung des Users und der Wildkamera,
  nicht aus der Radarspur.

## Offen: zweiter Kot

Der User hat einen zweiten, wenige Tage alten Kot gefunden. Seine Fundstelle passt zu
**keinem** der bisherigen Kandidaten:

- 367039 / 367040 (07.10.2026)
- 367457 / 367458 (10.10.2026)

Diese Tracks liegen alle bei ungefähr (7550–7600, 9480–9700) mm. Der passende Track ist
noch nicht gefunden.

## Folgerungen für die Erkennung

1. Merkmale für „koten“: langsamer, kurzer, gewundener Track mit langer Sitzphase
   **abseits** vom üblichen Katzenweg.
2. Nachts reicht die Kamera für eine schwarze Katze ausserhalb des IR-Kegels nicht. Die
   Radarspur ist dort die Hauptquelle.
3. Den Fall im Analyse-Tab per Kästchen als **Katze** markieren und als Positivbeispiel
   für einen späteren eigenen Klassifikator verwenden (Phase 5 D).
