# Review Sim6_3_0

(Punkte zu xComDef6_3.h/xComProc6_3.h stehen separat in
Controller/Manager6_3_0/REVIEW_xCom6_3.md)

## Fehler / Risiken

### 1. Playback kann sich selbst aufnehmen
AsyncUDP-Multicast empfängt auch die eigenen Pakete. Steht `doRecord`
während "2 - Play Back" auf true, zeichnet der Simulator seine eigene
Wiedergabe wieder auf (die Records tragen den Original-Sender, nicht
Sim — Duplikate sind in der Datei nicht erkennbar!). Mindestens:
```cpp
// am Anfang von sendData():
if (doRecord) { doRecord = false; writeMenue(); }
// am Ende von sendData():
mcDataReceived = false;   // während Playback aufgelaufene Pakete verwerfen
```

### 2. Dateiformat ohne Kennung
data.bin beginnt direkt mit dem ersten Record. Eine alte 5_4-Datei
(feste comMsgStruct-Records) wird von readRecord() klaglos als Müll
interpretiert (erst der payloadLen-Check bricht zufällig ab). Ein kurzer
Datei-Header ("XC63" + Version) am Anfang würde Format-Verwechslungen
sauber erkennen; beim Anlegen schreiben, beim Öffnen prüfen.

## Verbesserungen

### 3. Wiedergabe mit Original-Timing
Die Records enthalten jetzt den Original-timeStamp — statt fixem
`delay(20)` liesse sich die echte Zeitdifferenz zwischen den Records
abspielen (auf z.B. max. 2 s gedeckelt). Damit wird die Simulation
realistisch (Katzenbewegung in Echtzeit statt Zeitraffer). Achtung:
timeStamp ist sekundengenau — wenn feiner nötig, beim Aufzeichnen
zusätzlich millis()-Differenzen im Record speichern (eigenes Feld vor
dem xMsg-Record, betrifft dann auch Punkt 2/Dateiformat).

### 4. Mehr als nur catObserved aufzeichnen
Aktuell wird nur catObserved gespeichert. HB-, measurement- und
catHit-Nachrichten wären für eine vollständige Wiedergabe eines
"Vorfalls" ebenfalls interessant. Vorschlag: Taste '4' schaltet einen
Filter um (nur catObserved / alles ausser HB / alles).

### 5. Mehrere Aufnahme-Dateien
Immer /data.bin: eine neue Aufnahme hängt an die alte an (FILE_APPEND),
löschen geht nur ganz. Pro Aufnahme eine Datei (/rec_001.bin, ...) und
ein kleines Auswahlmenü würde Szenen-Verwaltung ermöglichen ("Katze von
links", "Katze von rechts" ...).

### 6. SD-Schreibfehler prüfen
saveRecord() ignoriert die Rückgabewerte von file.write() — bei voller
Karte wächst `records` weiter, obwohl nichts gespeichert wird. Rückgabe
prüfen und im Display melden.

### 7. OTA aktivieren
Der Simulator ist jetzt in der device dB ("Simulator") — `setUpOTA()` +
`ArduinoOTA.handle()` wären zwei Zeilen und ersparen das USB-Kabel am
Cardputer.

## Kleinkram / Stil

- hwDef.h: `blinkPeriode` wird nicht verwendet — entfernen.
- `records` wird beim Abbruch von countRecords() (defekter Schwanz der
  Datei) stillschweigend zu klein gezählt — bei Punkt 2 gleich eine
  Warnung ausgeben ("Datei beschädigt ab Record N").
- Bei doRecord == false bleibt ein empfangenes Paket im Puffer liegen
  (`mcDataReceived` bleibt true) — beim Einschalten der Aufnahme wird
  dann sofort diese eine veraltete Nachricht aufgezeichnet. Beim
  Umschalten auf Record `mcDataReceived = false;` setzen.
- Der Record-Zweig könnte ein kurzes Feedback geben, dass überhaupt
  Netzverkehr ankommt (Zeichen in der Statuszeile), auch wenn die
  Nachricht kein catObserved ist.
- `setUpTime()` blockiert ohne Netz/NTP endlos — beim mobilen Cardputer
  realistischer als bei den fest verbauten Geräten; Timeout mit Warnung
  wäre hier sinnvoll (Records bekommen dann halt Epoch-Zeitstempel).
