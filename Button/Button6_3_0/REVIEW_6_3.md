# Review Button6_3_0

(Punkte zu xComDef6_3.h/xComProc6_3.h stehen separat in
Controller/Manager6_3_0/REVIEW_xCom6_3.md)

## Fehler / Risiken

### 1. `ArduinoOTA.handle()` ohne `setUpOTA()`
In `loop()` wird `ArduinoOTA.handle()` aufgerufen, `setUpOTA()` ist aber
auskommentiert. Das ist zwar meist harmlos, aber inkonsistent — entweder
OTA aktivieren (praktisch beim fest verbauten Button!) oder den
handle()-Aufruf mit auskommentieren.

### 2. Fehlversand wird nicht angezeigt
`unicastMsg(...)` gibt `false` zurück, wenn die IP des PowerActors noch
unbekannt ist (`device[PA2i].IP == 0`, d.h. noch kein HB empfangen). Der
Tastendruck verpufft dann kommentarlos — der Benutzer steht im Garten und
wundert sich. Vorschlag:
```cpp
if (!unicastMsg(commandMsg, cmdToSend, device[PA2i].IP)) {
  allPixel(0xFF00FF);   // Magenta = PA noch nicht gesehen
}
```

## Verbesserungen

### 3. Zustand nur über HB-Rückmeldung — gut, aber träge bei Verlust
Das LED-Feedback kommt erst mit dem nächsten PA2-HB (PA2 sendet nach einem
Kommando sofort 2x HB — gut). Geht genau dieses HB-Paket verloren, bleibt
die Anzeige bis zur nächsten periodeForHB (5 s) auf "undef" (weiss).
Option: nach dem Senden einen Timeout starten und bei ausbleibendem HB
erneut anzeigen/warnen.

### 4. HB-Alter überwachen
Der Button weiss durch `getHbPayload()` jetzt, wie oft der PA2 senden
sollte (`HBperiode` steht im Payload). Bleibt der HB z.B. 3 Perioden aus
→ undefFireState() anzeigen, damit kein veralteter "ready"-Zustand
suggeriert wird (sicherheitsrelevant: der Button ist die Scharfschaltung).

### 5. Eigener Heartbeat
`periodeForHB 10000` ist definiert, wird aber nicht benutzt — ein
hbPayload-Broadcast würde den Button für den Manager sichtbar machen
(und die device dB lernt seine IP für Unicast).

## Kleinkram / Stil

- `M5.begin()` vor `Serial.begin(115200)`: M5Atom initialisiert Serial
  bereits selbst (mit eigener Baudrate) — der zweite begin ist redundant;
  Reihenfolge prüfen bzw. nur eine Stelle verwenden.
- `timer`, `blinkOn`, `targetAlarm` sind deklariert, werden aber nie
  verwendet — Überbleibsel aus der Manager-Vorlage, kann raus.
- `cmdToSend` ist global, wird aber nur im Tastendruck-Zweig gebraucht —
  lokal deklarieren.
- hwDef.h: `HB_blinkPeriode`/`Alarm_blinkPeriode` ungenutzt — entfernen.
- `byte ID = Schalter;` vs. `#define ID` (PA2i): projektweit vereinheitlichen.
