# Review Manager6_3_0

(Punkte zu xComDef6_3.h/xComProc6_3.h stehen separat in REVIEW_xCom6_3.md)

## Fehler / Risiken

### 1. OTA-Start kann `leds[230]` schreiben
hwDef.h: `maxPix 230` bei `pixelNum 24` — der Manager hat OTA aktiv, und
`setUpOTA()` ruft beim Update-Start ungeschützt `setPixel(maxPix,...)` auf
→ Schreibzugriff weit hinter dem leds-Array, ausgerechnet während des
Flashens. Bis der Guard in xComProc drin ist (siehe REVIEW_xCom6_3.md
Punkt 4): `maxPix` hier auf `pixelNum-1` setzen oder den Trick
`maxPix > pixelNum` anders lösen.

### 2. millis()-Überlauf in der Blink-Logik
`timer = millis() + HB_blinkPeriode` und `millis() > timer` läuft nach
49 Tagen über. Überlaufsicher ist das Muster
`if (millis() - blinkStart > periode)`. Betrifft alle Programme, beim
Dauerläufer Manager aber am ehesten relevant.

## Verbesserungen

### 3. blinkOn und targetAlarm teilen sich `timer`
Kommt während des Alarm-Blau (500 ms) ein HB an, überschreibt der HB den
Timer (1 ms) und löscht den Alarm praktisch sofort. Wenn der Alarm Vorrang
haben soll: bei `targetAlarm == true` keine HB-Anzeige, oder zwei getrennte
Timer verwenden.

### 4. `HB_blinkPeriode 1` (1 ms)
Das Grün ist faktisch unsichtbar (allPixel + FastLED.show dauert länger).
Vermutlich gewollt kurz — sonst auf z.B. 50–100 ms erhöhen.

### 5. Unicast-Empfang wird ignoriert
`initUnicast()` wird aufgerufen, aber `ucDataReceived` nie ausgewertet.
Naheliegend für ein Management-Gerät: eingehende commandMsg mit
`printCmdData(lastUcMsg)` loggen — oder `initUnicast()` weglassen.

### 6. Eigener Heartbeat fehlt
`periodeForHB 10000` ist definiert, der Manager sendet aber nie HB. Damit
lernen die anderen Geräte seine IP nicht (device dB bleibt auf der
statischen 180 — funktioniert, aber nur solange die statische Konfiguration
greift). Ein simpler hbPayload-Broadcast alle 10 s wäre konsistent.

### 7. Status-Übersicht statt nur Serial-Log
Der Manager sieht jetzt dank `getHbPayload()` von jedem Gerät IP und
HB-Periode. Daraus liesse sich einfach eine "wer lebt"-Überwachung bauen:
letzte HB-Zeit je Gerät merken und bei Ausbleiben (z.B. 3x periode) rot
blinken / sendUdpTextln-Warnung. Die HBperiode steht ja im Payload.

## Kleinkram / Stil

- `byte ID = Manager;` — PA2i benutzt `#define ID`, andere die Variable.
  Einheitlich machen (define reicht überall, spart 1 Byte RAM).
- `#define containLed` steht im Sketch NACH `#include <FastLED.h>` aber vor
  der leds-Definition — funktioniert, aber besser direkt zu den anderen
  defines an den Anfang.
- hwDef.h: `hostName "Manager_Dev"` wird nicht verwendet (OTA nimmt den
  Namen aus der device dB) — entfernen oder verwenden.
