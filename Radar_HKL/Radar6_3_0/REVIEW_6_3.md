# Review Radar6_3_0

(Punkte zu xComDef6_3.h/xComProc6_3.h stehen separat in
Controller/Manager6_3_0/REVIEW_xCom6_3.md)

## Fehler / Risiken

### 1. Ein `timer` für zwei Aufgaben (HB und Status-LED)
`heardBeat()` benutzt dieselbe Variable für die HB-Periode und die
LED-Dauer und setzt sie beim LED-Ausschalten erneut auf `millis()` —
dadurch verschiebt sich die HB-Periode um statusLightDuration und die
Logik ist schwer nachvollziehbar. Zwei getrennte Timer
(`hbTimer`, `ledTimer`) machen das robust. Zusätzlich gilt auch hier das
überlaufsichere Muster `millis() - t >= periode` (49-Tage-Problem).

### 2. MiniDome: `periodeForHB 500`
Der MiniDome sendet alle 0,5 s einen HB-Multicast — vermutlich ein
Debug-Wert (Dome/CompactDome: 5000). Im Normalbetrieb flutet das Netz
und lässt den Manager dauerblinken. Auf 5000 angleichen oder bewusst
dokumentieren.

## Verbesserungen

### 3. Radar-Targets bündeln
Pro Radar-Frame können bis zu 3 Targets als 3 einzelne UDP-Pakete
rausgehen. Mit dem variablen Payload liesse sich das in eine Nachricht
packen (count + 3 x Targetdaten) — halbiert/drittelt die Paketrate und
die Targets eines Frames bleiben zusammen (für den PA2 später nützlich,
wenn er das "beste" Ziel wählen soll).

### 4. deadZone-Filter sendeseitig UND im HB — gut, aber Display nutzt es nicht
Die Totzone wird jetzt im radarHbPayload mitgesendet (deadZoneDist) —
sinnvoll. Das Display zeichnet seine Totzone aber weiterhin aus dem
lokalen `initDeadZone`. Folge-Idee: Display übernimmt die gemeldete
Totzone des aktiven Sensors (siehe Udisp-Review Punkt 4).

### 5. `ucDataReceived` wird nicht ausgewertet
`initUnicast()` läuft, aber Kommandos werden ignoriert. Kandidat:
cmd zum Setzen der deadZone zur Laufzeit (analog zu den PA2-Limits,
inkl. Vault-Persistenz) — oder initUnicast() weglassen.

### 6. Plausibilitätsprüfung der Radar-Daten
`calculateRadarPositions()` übernimmt x/y/geschw ungeprüft aus dem
Seriellen-Frame; ein Bitfehler erzeugt ein Phantom-Target am Rand.
Einfacher Filter: Distanz > maxDist (8 m) verwerfen; optional erst nach
N aufeinanderfolgenden Frames mit Target melden (entprellen).

## Kleinkram / Stil

- Die Geschwindigkeits-/Vorzeichenlogik (`geschw += 0x8000` ...) ist
  schwer lesbar und sieht verdächtig aus (im positiven Fall wird 0x8000
  addiert?). Wenn sie stimmt: Kommentar mit dem Frameformat des HLK
  dazu; wenn nicht: mit `int16_t`-Cast sauber lösen wie bei `res`.
- `statusLightDuration 10` (10 ms) — LED-Blitz ist kaum sichtbar;
  gewollt? Sonst 50–100 ms.
- DomeDevice-Konfiguration definiert `laser 25`, geschaltet wird er nie.
- `obsData` global: vor jedem Senden werden alle Felder gesetzt — ok,
  aber ein lokales `posPayload obs{};` pro Target wäre narrensicherer.
- Auskommentierte Altberechnungen (toPaPol-Vorgänger) in
  calculateRadarPositions können raus.
