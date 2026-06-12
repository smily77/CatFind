# Review LD06_6_3_0

(Punkte zu xComDef6_3.h/xComProc6_3.h stehen separat in
Controller/Manager6_3_0/REVIEW_xCom6_3.md)

## Fehler / Risiken

### 1. Bis zu 12 Broadcasts pro Lidar-Sentence
Pro Sentence (12 Messpunkte) wird für jeden Treffer sofort ein eigenes
UDP-Paket gesendet. Bei einer Katze im Erfassungsbereich sind das bei
~480 Sentences/s im schlimmsten Fall mehrere tausend Pakete pro Sekunde —
das flutet das WLAN und die Empfänger. Der variable Payload kann das jetzt
elegant lösen: **mehrere Punkte in einer Nachricht bündeln**, z.B.
```cpp
struct __attribute__((packed)) multiPosPayload {
  uint8_t count;
  struct { int16_t x, y; } p[12];   // 49 Bytes, passt in maxPayloadLen 64
};
```
oder mindestens eine Mindest-Sendepause (z.B. 50 ms) zwischen Beobachtungen.

### 2. `obsData.sensor/targetSpeed/res` werden nie gesetzt
Als globale Variable sind sie zwar 0-initialisiert, aber das ist implizit.
Beim Wiederverwenden (z.B. wenn später doch Geschwindigkeit berechnet wird)
bleiben alte Werte stehen. Vor dem Senden explizit setzen
(`obsData.sensor = 0;` etc.) oder pro Messung ein frisches
`posPayload obs{};` lokal anlegen.

## Verbesserungen

### 3. Kein Heartbeat
Der LD06 sendet nie HB → der Manager sieht ihn nicht, und seine IP wird in
keiner device dB eingetragen (Unicast an den LD06 bleibt unmöglich, z.B.
für ein späteres "Lidar an/aus"-Kommando). hbPayload-Broadcast mit
periodeForHB ergänzen (Define in hwDef.h fehlt ebenfalls).

### 4. Erfassungsfenster als Kommandos konfigurierbar machen
`minAngleC/maxAngleC/nearEndC/farEndC` sind hart codiert. Da der LD06 schon
auf dem Unicast-Port lauscht (`initUnicast()` wird aufgerufen, aber
`ucDataReceived` nie ausgewertet!), bieten sich commandMsg-Codes analog zu
cmdSetLeftLimit/cmdSetFarLimit an — dann liesse sich das Fenster vom
Display aus einstellen. Alternativ `initUnicast()` entfernen, solange es
nicht genutzt wird.

### 5. Tote Fire-Logik entfernen
`fireOn`/`fireTimer` können nie true werden (die setzenden Zeilen sind
auskommentiert) — der Block in `loop()` ist toter Code. Entfernen oder die
LED-Anzeige wieder aktivieren.

### 6. Winkelberechnung dokumentieren
`obsData.angle = (450 - mData[i].winkel)*2048/180;` mischt die
360°-Lidar-Welt und die 4096er-PA-Welt in einer Formel. Ein Kommentar
(oder eine Hilfsfunktion `lidarToPaAngle()` in xComProc neben toPaPol/
toPaKart) würde späteres Debugging deutlich erleichtern.

## Kleinkram / Stil

- `rawData[maxSentenceLength + 10]` mit Kommentar "eigentlich 46" — der
  Parser prüft `recordPos > 46`, d.h. genutzt werden 47 Bytes. Die +10
  Reserve ist ok, aber die Konstanten sollten zusammenpassen
  (`maxSentenceLength 47` und Schleifengrenze daraus ableiten).
- hwDef.h: `extRelais`, `ext1`, `ext2`, `laser` werden initialisiert, aber
  nie geschaltet — wenn die Hardware nicht bestückt ist, Kommentar dran.
- `messDaten.x/y` (float) werden nie gefüllt (Berechnung auskommentiert) —
  Felder entfernen spart 8 Bytes x 12.
- `DEBUG false`: beim WiFi-Verbinden gibt es dadurch gar keine Ausgabe,
  auch keine Punkte — fürs Feld ok, bei Inbetriebnahme dran denken.
- `byte ID = LD06;` vs. `#define ID`: projektweit vereinheitlichen.
