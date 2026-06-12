# Review PA2i6_3_0

(Punkte zu xComDef6_3.h/xComProc6_3.h stehen separat in
Controller/Manager6_3_0/REVIEW_xCom6_3.md)

## Fehler / Risiken

### 1. Kommandos sind unauthentifiziert (sicherheitsrelevant)
`cmdArmFire` & Co. werden von jedem WLAN-Teilnehmer akzeptiert; das Gerät
steuert Ventil/Servo/Laser. Mindestens Absender-IP gegen die device dB
prüfen oder ein Shared-Secret im Protokoll ergänzen (siehe
REVIEW_xCom6_3.md Punkt 5). Bis dahin: `lastUcMsg.header.sender` prüfen
(nur Schalter/Screens zulassen) — schwach, aber besser als nichts.

### 2. millis()-Überlauf in `heardBeat()`
`(HBtimer + periodeForHB) < millis()` läuft nach 49 Tagen über — beim fest
installierten PA2 realistisch. Überlaufsicher:
`if (millis() - HBtimer >= periodeForHB)`.

### 3. Limits werden ohne Plausibilitätsprüfung übernommen
`hbState.leftLimit = cmd.info;` übernimmt jeden Wert (auch ausserhalb
leftStopp/rightStopp bzw. < 0 oder > maxDist) und persistiert ihn im
Vault. Ein vertipptes Kommando bleibt damit dauerhaft gespeichert.
Vor dem Übernehmen clampen (analog `servoGoTo()`).

## Verbesserungen

### 4. `readyToFire` absichtlich nicht persistiert? Dokumentieren.
cmdArmFire toggelt `hbState.readyToFire` ohne `saveHBToVault()` — nach
einem Reboot ist das System also immer entschärft. Das ist mutmasslich so
gewollt (gut!), sollte aber als Kommentar an der Stelle stehen, damit es
nicht versehentlich "gefixt" wird.

### 5. Doppeltes `sendHBmsg()` als Sammel-Antwort
Nach jedem Kommando wird 2x identisch gesendet, um Paketverlust
abzufedern. Mit der Sequenznummer aus dem Header-Vorschlag (REVIEW_xCom
Punkt 8) könnten Empfänger Duplikate erkennen; alternativ zwischen den
beiden Sends ein paar ms Abstand — zwei direkt aufeinanderfolgende Pakete
gehen im WLAN gern gemeinsam verloren.

### 6. Auskommentierte Zielverfolgung referenziert undefinierte Symbole
Der grosse /* ... */-Block in `loop()` nutzt `adjustmentMode` und
`angleAdjust`, die nirgends definiert sind — beim Einkommentieren knallt
es sofort. Entweder die Variablen (auskommentiert) mit anlegen oder den
Block in eine eigene, per `#ifdef` deaktivierte Funktion ziehen, damit er
mitkompiliert werden kann und nicht verrottet.

### 7. Ventil wird nie angesteuert
`valve` (Pin 27) wird als OUTPUT initialisiert, aber nirgends geschaltet —
die eigentliche "Wasser marsch"-Logik fehlt noch komplett bzw. liegt im
auskommentierten Block (der nur den Laser schaltet). Als TODO markieren,
inkl. Sicherheitslogik: Ventil nur wenn `readyToFire && limitsActive` und
Ziel innerhalb der Limits.

### 8. Limits beim Feuern auch durchsetzen
`limitsActive`/left/right/far/near werden gespeichert und im HB
verteilt, aber im (auskommentierten) Schiess-Code nicht geprüft. Die
Limit-Prüfung gehört in den PA2 selbst (nicht nur ins Display), sonst
schiesst er auf alles, was `catObserved` meldet.

## Kleinkram / Stil

- `limitsActiveWert`, `leftLimitWert`, `rightLimitWert`, `farLimitWert`,
  `nearLimitWert` sind tote Globals aus 6_2 (Vault übernimmt das) — raus.
  Nebenbei: `rightLimitWert` war ohnehin falsch (`2048 - systemAngle`
  statt `+`).
- `scanWithLidar()`: `erfassungsWinkel` ungenutzt; Schleifengrenzen
  1365/2732 hart codiert statt aus leftStopp/rightStopp abgeleitet.
- `laserOn`/`switchLaser()` und `txPos` werden nur von auskommentiertem
  Code gebraucht — zusammen mit Punkt 6 behandeln.
- `intLed` wird initialisiert, aber nie benutzt — z.B. als
  readyToFire-Anzeige verwenden.
- hwDef.h: `LD06Pwr`/`hlkPwr` sind als "not in use" markiert — ok, aber
  `digitalWrite(hlkPwr, ...)` fehlt sogar in initHw (kein definierter
  Pegel). LOW setzen wie bei LD06Pwr.
- `Serial1.begin(...)` + `st.pSerial` wird doppelt gemacht (initHw und
  initServo) — eine Stelle reicht.
