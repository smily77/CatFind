# Review Udisp6_3_0

(Punkte zu xComDef6_3.h/xComProc6_3.h stehen separat in
Controller/Manager6_3_0/REVIEW_xCom6_3.md)

## Fehler / Risiken

### 1. detectLayer läuft voll
Jede catObserved-Meldung setzt einen Pixel, gelöscht wird der Layer nie —
nach einigen Stunden ist die Karte zugemalt. Vorschläge:
- zeitbasiert ausblenden (Layer alle X Minuten löschen), oder
- Menüpunkt "clear detections", oder
- Ringpuffer der letzten N Punkte neu zeichnen (aufwendiger, schöner).

### 2. Inkonsistente Arc-Zeichnung in `menueSelector()`
In `switchActions()` werden far/nearLimit mit
`PX(limitLayer,0), PY(limitLayer,0)` (Pivot) gezeichnet, in
`menueSelector()` dagegen mit `screenWidth, screenHight-1` — beim
Sprite mit spritFactor 2 ist das zufällig dasselbe, bei
spritFactor 1 (stEnable false, z.B. CYD35!) liegt das Zentrum falsch.
Auf die PX/PY-Variante vereinheitlichen.

### 3. Encoder-Positionen ohne Grenzen
In den Limit-Seiten kann encoderPos beliebig weit gedreht werden
(negativ, > maxDist, Winkel ausserhalb leftStopp/rightStopp). Der Wert
geht ungefiltert per commandMsg an den PA2 und wird dort persistiert
(siehe PA2-Review Punkt 3). Im Display clampen: Winkel auf
2048 ± systemAngle, Distanzen auf 0..maxDist.

## Verbesserungen

### 4. Vom Radar gemeldete Totzone übernehmen
`deadZone` ist lokal hart auf initDeadZone gesetzt. Die Radar-HBs liefern
jetzt radarHbPayload.deadZoneDist — beim HB-Empfang eines HLK-Geräts
übernehmen und drawGrid() aktualisieren, dann stimmt die Anzeige immer
mit dem Sensor überein.

### 5. readyToFire anzeigen
pa2HbPayload.readyToFire wird empfangen, aber nicht dargestellt. Ein
roter Rahmen/Statuspunkt "ARMED" wäre die wichtigste Information
überhaupt auf diesem Schirm.

### 6. catHit / measurement darstellen
hitLayer existiert (gelb in der Palette), aber es wird nie hineingezeichnet;
msgCode catHit/measurement werden ignoriert. Treffer auf dem hitLayer und
Messungen (vom scanWithLidar) auf einem eigenen Layer wären mit dem neuen
getPayload()-Dispatch je 5 Zeilen.

### 7. Bestätigung der Kommandos abwarten
Nach setLeftLimit & Co. zeichnet das Display die Limits sofort lokal.
Geht das Unicast-Paket verloren, zeigt es einen Zustand, den der PA2 nie
übernommen hat. Da der PA2 nach jedem Kommando 2x HB sendet, reicht es,
die lokale Zeichnung wegzulassen und auf den HB zu warten (der zeichnet
die Limits ohnehin) — dann ist die Anzeige immer die Wahrheit des PA2.
Rückgabewert von `unicastMsg()` prüfen (false = IP unbekannt) und dem
Benutzer anzeigen.

### 8. `ucDataReceived` / Text-UDP ungenutzt
initUnicast() und initText2Udp() laufen, die Flags werden nie gelesen.
Entweder nutzen (z.B. Textmeldungen in einer Statuszeile anzeigen) oder
die Inits entfernen.

## Kleinkram / Stil

- `#define back 8` wird nie benutzt (pages=8, default-Zweig fängt das ab).
- `encoder` (global int) ungenutzt.
- `int leftWinkel = ((pa2.leftLimit*360)/4096)+90;` — Magic-Number-Umrechnung
  4096er-Welt → Grad taucht hier, in drawVectorLine (dort 4046 — Tippfehler?
  prüfen!) und drawGrid auf. **drawVectorLine rechnet mit 4046 statt 4096** —
  das ist ein ~1%-Winkelfehler und vermutlich ein echter Bug aus 6_2;
  eine gemeinsame Hilfsfunktion `paToDeg()` würde das vereinheitlichen.
- `calcScale()` nutzt Parameter `dist` nicht (rechnet mit maxDist) und
  `3.141` statt M_PI.
- dispDef.h: Farb-defines doppeln sich mit der Palette; GRAU 0x010101 ist
  fast schwarz (vermutlich 0x808080 gemeint?).
- testOnce()/testLoop() sind Testroutinen, werden nie aufgerufen — per
  #ifdef TESTMODE kapseln oder entfernen.
