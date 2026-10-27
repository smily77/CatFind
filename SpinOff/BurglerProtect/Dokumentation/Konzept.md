# Spin-off BurglerProtect – Besucherabhängiges Wohnzimmerlicht

Stand: 2026-10-27 – Konzept, noch nichts umgesetzt.
Ursprüngliche Anforderung: [Anforderung_Original.md](Anforderung_Original.md)

## 1. Idee in einem Satz

Ist das System scharf und nähert sich jemand im Dunkeln über die Veranda dem Haus, schaltet ein lokales Steuergerät über Philips Hue das **Wohnzimmerlicht** für einige Minuten ein. Von aussen sieht es dann so aus, als wäre jemand zu Hause. Für den Betrieb braucht es weder den VPS noch den Manager.

## 2. Einschätzung

Die Idee passt gut zu CatFinder. Die Sensoren, der Bus, die Welt-Koordinaten und die Polygon-Werkzeuge (No-Shot-Karte, Coverage) gibt es schon. Neu sind im Kern nur ein kleines Steuergerät mit Taster und die Hue-Anbindung. Wohnzimmerlicht statt Aussenlicht ist ein guter Ansatz, weil es Anwesenheit vortäuscht, statt nur den Eindringling anzuleuchten.

Die Aufteilung «VPS = Konfiguration und Anzeige, Gerät = Entscheidung» entspricht dem Grundsatz, dass der laufende Betrieb nicht von einer zentralen Stelle abhängen darf.

Bei der Prüfung gegen das bestehende System haben sich drei Punkte gezeigt, die die Anforderung so nicht abdeckt. Sie müssen vor der Umsetzung geklärt sein.

### 2.1 Auf der Veranda kommt heute kein einziges Ereignis an

Abfrage der VPS-Datenbank vom 27.10.2026, letzte 14 Tage, alle Sensoren: **0 Ereignisse mit X > 0 und Y < 0**. Der Dome-C1 meldet nur Y ≥ 0,99 m.

Grund: Jedes Radar mit gültiger Welt-Pose meldet nur Ziele **innerhalb der Filterkarte** (No-Shot- oder Rasenkarte, `processTargets()` in `Radar6_3_0.ino`). Das wurde absichtlich so gebaut, damit Nachbargrundstück und Strasse nicht dauernd Störungen erzeugen. Die Veranda liegt ausserhalb der Rasenkarte, ihre Ziele werden also im Radar verworfen.

Ausserdem ist offen, ob der Dome-C1 die Veranda überhaupt sieht. Er steht bei (8,21 / 2,91 m), Ausrichtung 130,4°. Die Veranda (Y < 0) ist mindestens 3 m entfernt, das liegt noch in der Reichweite des HLK-LD2450 (≈ 6 m), aber vielleicht nicht im Öffnungswinkel (±60°). Die KIVision-Kamera steht selbst auf der Veranda (6,87 / −0,18 m) und blickt auf den Rasen, für die Veranda fällt sie aus.

→ **Erster Schritt vor allem anderen:** messen, ob und wie gut der Dome-C1 eine Person auf der Veranda erfasst (siehe Abschnitt 8, Schritt 0).

### 2.2 Besucher dürfen NICHT als `catObserved` gemeldet werden

Die Anforderung sieht `catObserved` als Quelle vor. Das wäre gefährlich: **PowerActor1_1 feuert auf jedes `catObserved` ohne Absenderfilter.** Ein Besucher auf der Veranda würde damit zum Ziel des Wasserstrahls. Ausserdem würden CatIdentifier und VPS-Analysierer Veranda-Spuren als Katzenkandidaten bewerten.

Darum gibt es dafür einen **eigenen msgCode `visitorObserved`**. Das ist dieselbe Lehre wie bei `visionObserved` (Code 16) für die Kamera. Der Sensor meldet Ziele ausserhalb der Filterkarte, aber im Quadranten X > 0, Y < 0, zusätzlich als `visitorObserved` (gleicher `posPayload`). Damit ändert sich für alle bisherigen Geräte nichts.

**Beschluss 2026-10-27: Im Sensor nur der Quadrant, das Polygon nur im Steuergerät.** Der Sensor läuft wie bisher weiter und bekommt nur eine zusätzliche Bedingung (`else if (wx > 0 && wy < 0) broadcastMsg(visitorObserved, pos);`), also wenige Bytes Code. Das ist wichtig für den Lidar-C1: Seine Firmware ist 1,23 MB gross, die App-Partition fasst 1,31 MB (≈ 94 % voll). Eine zweite Karte samt Kartentransfer hätte dort keinen Platz.

Das ist zwar anders als beim Katzenteil, wo jeder Sensor selbst nach Karte filtert, hat aber auch Vorteile: Das Auslösepolygon liegt nur an **einer** Stelle, und `mapVisitor` muss nur an das Steuergerät verteilt werden, nicht an jeden Sensor. Weil auf der Veranda selten jemand ist, bleibt der zusätzliche Busverkehr klein. Der Quadrant kann aber auch Hauswand-Reflexe oder Strasse enthalten. Das zeigt sich in Schritt 2. Falls es zu viel wird, kann man den Quadranten im Sensor mit festen Grenzen enger fassen, statt dort ein Polygon einzubauen.

Später möglich (eleganter, braucht aber Speicher): zwei Zonen im Sensor, eine Karte für `catObserved` und eine für `visitorObserved`. Für die Radare (genug Platz) ginge das, für den Lidar-C1 nicht.

**Welcher Sensor?** In Frage kommen der Dome-C1 (Radar, 8,21 / 2,91 m, Öffnung ±60°, Reichweite ≈ 6–7 m) und der Lidar-C1 (3,18 / 8,88 m, 360°, Reichweite 12 m, Veranda ≥ 9 m entfernt). Beim Lidar-C1 gilt eine Eigenheit: Er meldet pro Umdrehung nur **einen** Punkt, nämlich den Schwerpunkt aller Punkte, die vor dem Hintergrund liegen. Sind gleichzeitig eine Katze auf dem Rasen und eine Person auf der Veranda zu sehen, liegt der gemeldete Punkt irgendwo dazwischen. Die Quadrant-Prüfung muss deshalb auf dem gemeinsamen Schwerpunkt arbeiten, oder die Firmware müsste die Punkte nach Quadrant getrennt sammeln (wenige Bytes mehr). Beide Sensoren können `visitorObserved` senden, und das Steuergerät nimmt beide.

### 2.3 Neues Busgerät = neuer Eintrag in `device[]`

Das Steuergerät wird Busteilnehmer Nr. 22 (`deviceCount` 22 → 23). Wie bei Dome-C1 und KIVision verwerfen alle Geräte mit altem `deviceCount` die Meldungen des neuen Geräts, bis sie neu geflasht sind. Das betrifft vor allem den Manager (Konfiguration, Anzeige) und das Display. Für die eigentliche Funktion reicht es, Steuergerät, Dome-C1 und Manager zu flashen.

Alternative ohne neues Gerät: die Funktion in den bestehenden **Button** (Gerät `Schalter`) einbauen. Davon rate ich ab. Der Button ist der Hauptschalter der Aktoren, und eine Verwechslung «Licht scharf» ↔ «Wasser scharf» wäre ein schlechter Fehler. Auch der Standort ist ein anderer (Button beim Aktor, Besucherlicht beim Hauseingang bzw. Wohnzimmer).

## 3. Übersicht

```
 Dome-C1 (Radar)                      Steuergerät "Besucherlicht" (Nr. 22)
 ───────────────                      ────────────────────────────────────
 Ziel ausserhalb Filterkarte  ──MC──▶  visitorObserved empfangen
 und X>0, Y<0                         ├─ im Auslösepolygon?          (lokal gespeichert)
   → visitorObserved (neu)            ├─ Spur bilden, Plausibilität  (lokal gespeichert)
                                      ├─ scharf? (Taster / VPS)       (lokal gespeichert)
                                      ├─ Hue: Wohnzimmer aus?         ─HTTP─▶ Hue Bridge (LAN)
                                      └─ Licht EIN, Timer, Licht AUS  ─HTTP─▶ Hue Bridge (LAN)
                                         HB mit Zustand ──MC──▶ Manager ──▶ VPS (Anzeige)

 VPS Tab "Besucher": Karte + Liste + Polygon + Parameter + Scharf-Knopf
   Konfiguration ──▶ Manager ──▶ Steuergerät (dort im Flash gespeichert)
```

Ohne VPS und Manager läuft der Pfad Dome-C1 → Steuergerät → Hue weiter. Es fehlen dann nur Anzeige und Fernbedienung.

## 4. Erkennung und Plausibilisierung

### 4.1 Messgrundlage

Der Dome-C1 meldet bei einem Ziel im Sichtfeld etwa **10 Ereignisse pro Sekunde** (gemessen über 3 Tage: Median 10/s, 90 % ≤ 12/s, Abstand zwischen zwei Meldungen Median 0,10 s). Eine Person auf dem Weg zur Haustür ist erfahrungsgemäss 2–5 s im Bereich.

### 4.2 Regel (Vorschlag, alles im VPS einstellbar)

1. **Vorfilter im Radar:** nur Ziele mit X > 0, Y < 0, ausserhalb der Filterkarte.
2. **Polygon im Steuergerät:** nur Ereignisse innerhalb des Auslösepolygons zählen (Punkt-im-Polygon, wie bei der No-Shot-Karte).
3. **Spur:** aufeinanderfolgende Ereignisse gehören zum selben Objekt, wenn sie höchstens `LINK_MM` = 1000 mm und `LINK_S` = 1,0 s auseinanderliegen. Einzelne Ausreisser beginnen eine eigene, kurzlebige Spur und verfälschen die echte nicht.
4. **Auslösung**, sobald eine Spur
   - mindestens `MIN_EVENTS` = **12** Ereignisse im Polygon hat,
   - innerhalb von `WINDOW_S` = **3 s**,
   - und dabei mindestens `MIN_SPAN_S` = **1,0 s** lang besteht (nicht nur ein Schub in einem Augenblick).

   Reaktionszeit typisch 1,2–2 s. Das ist kurz genug, weil das Licht spätestens dann brennt, wenn die Person an der Tür ist.
5. **Sperre nach Auslösung:** Während das Licht brennt und noch `COOLDOWN_S` = 300 s danach löst nichts erneut aus. Zusätzlich muss das Polygon vor einer neuen Auslösung mindestens 30 s leer gewesen sein. So schalten dieselben Erkennungen nicht gleich ein zweites Mal.

Die Werte sind Startwerte. Sobald `visitorObserved` läuft, werden sie an echten Durchgängen im Tab «Besucher» abgestimmt (Abschnitt 8, Schritt 2).

### 4.3 Was die Regel nicht unterscheidet

Das Radar unterscheidet nicht zwischen Person, Katze oder Fuchs. Eine Katze, die über die Veranda läuft, kann das Licht einschalten. Das ist harmlos und eher selten, weil Tiere meist rasch durchlaufen. Bewohner, die nach Hause kommen, lösen es ebenfalls aus, und das ist sogar erwünscht. Eine Personenerkennung per Kamera ist auf der Veranda heute nicht möglich (siehe 2.1).

## 5. Lichtsteuerung über Philips Hue

### 5.1 Anbindung

- **Lokal über die Hue Bridge im LAN, ohne Cloud** (passt zur VPS-Unabhängigkeit). Die Bridge bietet eine einfache HTTP-REST-Schnittstelle (API v1): `GET /api/<key>/groups/<id>` liefert den Zustand, `PUT …/action` schaltet. Das ist auf einem ESP32 problemlos machbar. Die API v2 (HTTPS + Ereignisstrom) ist für den ESP32 schwerer und hier nicht nötig.
- Einmalig wird ein **App-Key** erzeugt: Knopf auf der Bridge drücken, dann `POST /api` vom Steuergerät aus. Der Key wird im Flash gespeichert und im VPS nur als «vorhanden» angezeigt.
- Gesteuert wird der **Raum (Gruppe) «Wohnzimmer»** statt einer einzelnen Lampe. `state.any_on` zeigt, ob dort irgendein Licht brennt.
- Bridge-IP fest (DHCP-Reservierung) oder per mDNS/Discovery, Raum-ID im VPS auswählbar.

### 5.2 Ablauf und manuelle Bedienung

| Lage | Verhalten |
|---|---|
| Wohnzimmer brennt schon (`any_on`) | nichts tun |
| Auslösung, Licht aus | einschalten mit fester Helligkeit `BRI` (z. B. 70 %) und Farbtemperatur `CT` (warm), merken «das Licht gehört mir» |
| Während der Einschaltdauer (alle 5 s abfragen) | Wird das Licht ausgeschaltet oder ändern sich Helligkeit/Farbe → «gehört nicht mehr mir» |
| Ende der Einschaltdauer (`ON_MIN` = 10 min) | nur ausschalten, wenn es mir noch gehört, also noch an ist **und** noch genau `BRI`/`CT` hat |
| Lampe nicht erreichbar (Wandschalter aus, `reachable=false`) | nicht nachschalten, Fehler im HB melden |
| Bridge nicht erreichbar | Taster blinkt, Fehler im HB |

Das deckt den Fall «Bewohner kommt heim und lässt das Licht an» ab. Er schaltet es aus und wieder ein oder dimmt es, und schon schaltet das System es nicht mehr ab. Bleibt eine Lücke: Wer nur auf denselben Wert schaltet, ohne etwas zu ändern, bei dem schaltet das System das Licht nach Ablauf trotzdem aus. Für den Zweck ist das vertretbar.

Optional später: Einschaltdauer leicht zufällig (± 20 %), damit es weniger nach Automatik aussieht.

### 5.3 Nur bei Dunkelheit (Vorschlag, nicht in der Anforderung)

Bei Tageslicht bringt eingeschaltetes Wohnzimmerlicht nichts. Vorschlag: zusätzliche Bedingung **«zwischen Sonnenuntergang − 30 min und Sonnenaufgang + 30 min»**, lokal berechnet aus Datum, NTP-Zeit und fester Position. Dafür braucht es keinen Sensor und keinen VPS. Im VPS abschaltbar, falls der Taster ohnehin nur beim Weggehen am Abend gedrückt wird.

## 6. Taster und Scharfschaltung

- Gerät: ESP32 mit einem **beleuchteten Taster** (z. B. Metall-Drucktaster mit LED-Ring oder ganzflächig leuchtender Arcade-Taster, über einen Transistor geschaltet). Alternative ohne Bastelarbeit: M5 Atom Matrix wie beim Button (der ganze Knopf leuchtet).
- **Leuchtet = scharf, dunkel = aus.** Blinkt = Fehler (Hue nicht erreichbar, kein Key).
- **Das Steuergerät ist der Master des Zustands.** Der Zustand wird im Flash gespeichert, sodass das System nach einem Stromausfall im selben Zustand weiterläuft. Der Zustand steht in jedem HB des Geräts, VPS und Manager zeigen ihn nur an.
- Der VPS schaltet per Kommando mit **absolutem Wert** (`info` 1 = scharf, 2 = aus), **nicht als Umschalter**. Geht ein UDP-Paket verloren oder kommt es doppelt an, stimmt der Zustand trotzdem (dieselbe Lehre wie im Button/Feuersperre-Plan). Der Taster am Gerät schaltet um, weil er lokal ist und keine Paketverluste hat.

## 7. VPS-Tab «Besucher»

| Teil | Inhalt | Vorbild |
|---|---|---|
| Karte | alle `visitorObserved` im Quadranten X > 0, Y < 0, Spuren farbig, Auslösungen markiert | Tab Welt_Karte |
| Polygon | Auslösebereich zeichnen, Ecken verschieben/löschen, speichern | Coverage-Tab, No-Shot-Editor |
| Liste | Zeit, Position, Spur-Nr., «hat ausgelöst» / «gesperrt» / «Licht war an» | Tab Liste |
| Löschen | löscht **nur** die Besucherdaten (eigene Tabelle `visitor_events`), nichts sonst | Karte zurücksetzen |
| Zustand | Scharf/Aus (aus HB) + Knopf, Hue erreichbar ja/nein, letzte Auslösung | Steuerungs-Tab |
| Parameter | `MIN_EVENTS`, `WINDOW_S`, `MIN_SPAN_S`, `LINK_MM`, `COOLDOWN_S`, `ON_MIN`, `BRI`, `CT`, Dunkelheit ja/nein, Hue-Raum | Modell-Parameter |

**Wie die Konfiguration aufs Gerät kommt:** Das Polygon läuft über den bestehenden Kartenweg als neuer Kartentyp `mapVisitor` (VPS → Manager → Gerät, `mapInfo`/`mapChunk`, Versionsabgleich). Das Gerät legt es im LittleFS ab, genau wie die Radare die No-Shot-Karte. Die Parameter gehen als Einstellungen über den Manager und werden ebenfalls im Flash gespeichert. Das Gerät holt nie Betriebsdaten vom VPS, nur Konfiguration, und es läuft mit der zuletzt gespeicherten weiter.

Datenschutz: Die Tabelle enthält Bewegungen von Personen auf dem eigenen Grundstück. Vorschlag: automatisch nach 30 Tagen löschen, zusätzlich zum Löschknopf.

## 8. Vorgehen

0. **Sichtprüfung Dome-C1 und/oder Lidar-C1 (Voraussetzung):** Testfirmware mit `visitorObserved` (nur Quadrant, siehe 2.2) flashen, dann über die Veranda gehen. Ergebnis: Wird die Person erfasst, wie weit, wie stabil? Fällt das negativ aus, braucht es einen eigenen Sensor an der Veranda (z. B. ein zweites LD2450 Richtung Hauseingang, als weitere Radar-Variante).
1. **`visitorObserved`** in `xComDef6_3.h` und `Radar6_3_0.ino` bzw. `C1Lidar6_3_0.ino` (CatFinder-Erweiterung, nicht in diesem Ordner). VPS speichert sie in `visitor_events`, Tab «Besucher» zuerst nur mit Karte und Liste.
2. **Ein paar Tage beobachten:** echte Durchgänge, Störungen (Hauswand-Reflexe, Strasse), dann Polygon zeichnen und Parameter festlegen.
3. **Steuergerät** bauen (Taster, Hue-Anbindung, Plausibilisierung, Flash-Speicher), Gerät Nr. 22 in `device[]`.
4. **Konfigurationsweg:** `mapVisitor`, Parameter, Scharf-Kommando, HB-Anzeige im VPS.
5. **Probebetrieb** mit Protokoll im Tab, danach Werte nachschärfen.

## 9. Ablage

| Was | Wo |
|---|---|
| Konzept, Anleitungen, Schema | `SpinOff/BurglerProtect/Dokumentation/` |
| Firmware Steuergerät | `SpinOff/BurglerProtect/Firmware/` (geplant) |
| `visitorObserved`, `mapVisitor`, Gerät Nr. 22 | `Controller/Manager6_3_0/xComDef6_3.h` (gemeinsamer Header) |
| Sensor meldet Veranda-Ziele (nur Quadrant) | `Radar_HKL/Radar6_3_0/`, `CF_LidarC1/C1Lidar6_3_0/` |
| Tab «Besucher», Tabelle `visitor_events` | `VPS/dashboard/` |

## 10. Offene Fragen an den User

1. Welcher Sensor soll die Veranda liefern: Dome-C1, Lidar-C1 oder beide?
2. Wo kommt der Taster hin (Hauseingang, Wohnzimmer)? Gibt es dort Strom?
3. Hue: Wie heisst der Raum, ist die Bridge-IP fest?
4. Bedingung «nur bei Dunkelheit» einbauen (Vorschlag 5.3)?
5. Soll der scharfe Zustand einen Stromausfall überdauern (Vorschlag: ja)?
