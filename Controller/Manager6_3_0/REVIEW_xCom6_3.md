# Review xComDef6_3.h / xComProc6_3.h (gemeinsame Dateien)

Stand: 2026-06-12, Branch claude/gallant-carson-n6jkkw

## Fehler / Risiken

### 1. Kein Bounds-Check auf `sender` (wichtig!)
In `initMcUdp()` wird bei HB-Nachrichten `device[m.header.sender].IP = hb.ip;`
geschrieben. `sender` kommt ungeprüft aus dem Netz — ein Wert >= 15 schreibt
**ausserhalb des device-Arrays** (Speicherkorruption). Dasselbe Risiko besteht in
allen Sketchen, die `device[lastMcMsg.header.sender]` lesen (Button, Manager).

Empfehlung: in `parseXMsg()` zusätzlich prüfen:
```cpp
if (m.header.sender >= sizeof(device)/sizeof(device[0])) return false;
```
Damit ist es zentral für alle Programme erledigt.

### 2. Race Condition zwischen UDP-Callback und loop()
Die AsyncUDP-Callbacks laufen im LwIP-Task, `loop()` liest `lastMcMsg` im
Arduino-Task. Trifft ein zweites Paket ein, während `loop()` gerade kopiert,
entsteht eine zerrissene Nachricht (war in 6_2 genauso, durch `payloadLen` im
Header fällt es jetzt aber eher auf). Optionen:
- `portMUX_TYPE` + `taskENTER_CRITICAL` um Schreiben/Lesen von `lastMcMsg`
- oder kleiner Ringpuffer (z.B. 4 x xMsg), Callback schreibt, loop() liest
- mindestens: in den Sketchen zuerst kopieren, dann Flag löschen (machen sie).

### 3. `setUpOTA()`: fehlendes `else` (aus 6_2 geerbt)
```cpp
if (ArduinoOTA.getCommand() == U_FLASH)
  type = "sketch";
   type = "filesystem";   // wird IMMER ausgeführt
```
`type` ist damit immer "filesystem". Es fehlt ein `else`.

### 4. `setUpOTA()`: `setPixel(maxPix, ...)` ohne Guard (aus 6_2 geerbt)
Beim Manager ist `maxPix 230` bei `pixelNum 24` (maxPix > pixelNum wird sonst
als "alle Pixel"-Trick benutzt). `setPixel(maxPix,...)` in `onStart` schreibt
dann `leds[230]` → Pufferüberlauf genau beim OTA-Start. Gleiche Guard-Logik wie
in `setUpTime()` verwenden:
```cpp
if (maxPix > pixelNum) allPixel(0x0000FF); else setPixel(maxPix,0x0000FF);
```

### 5. Keinerlei Absicherung der Kommandos
Jeder im WLAN kann ein `commandMsg` (z.B. cmdArmFire) an den PowerActor
schicken — das Gerät steuert ein Wasserventil. Minimal sinnvoll:
- ein gemeinsames Shared-Secret/Magic-Feld im Header (4 Bytes), das der
  Empfänger prüft, oder
- `commandMsg` nur von bekannten Absender-IPs akzeptieren
  (`packet.remoteIP()` gegen device dB prüfen).

## Verbesserungen

### 6. UDP-Text auf das 6_3-Format umziehen
Der separate Text-Socket (Port 8300) ist mit dem variablen Payload eigentlich
überflüssig: ein `msgCode textMsg` mit dem String als Payload (bis
maxPayloadLen) spart den dritten Socket und gibt Textmeldungen automatisch
sender/timeStamp. `udpText`, `initText2Udp`, `sendUdpText*` könnten entfallen.

### 7. HB-Varianten über Gerätetyp statt Payload-Grösse unterscheiden
`printSensorData()` erkennt pa2HB/radarHB nur an `payloadLen`. Bekommen zwei
Payloads zufällig dieselbe Grösse, wird falsch interpretiert. Robuster:
`device[m.header.sender].type` (PowerActor/HLK) zur Auswahl heranziehen —
der Längen-Check in `getPayload()` bleibt als zweite Sicherung.

### 8. Sequenznummer im Header
Ein `uint16_t seq` im Header (Sender zählt hoch) würde Paketverlust und
Duplikate sichtbar machen — bei UDP-Multicast im WLAN sehr nützlich für die
Fehlersuche, kostet 2 Bytes.

### 9. Subnetz nicht hart codieren
`unicastMsg()` und `setUpWifi()` bauen IPs fest als `192,168,0,x`. Besser das
Präfix aus `WiFi.localIP()` ableiten oder als define (`NET_PREFIX`) an eine
Stelle ziehen — sonst bricht alles beim Routerwechsel.

### 10. Rückgabewerte beim Senden auswerten
`sendXMsg()` ignoriert den Rückgabewert von `writeTo()`. Bei vollem
Sendepuffer geht die Nachricht still verloren. `return`-Wert von `writeTo`
mit der erwarteten Länge vergleichen und zurückgeben.

### 11. `static_assert` der Wire-Grössen in den Header aufnehmen
Im Review-Build geprüft: msgHeader=12, hbPayload=5, radarHbPayload=9,
pa2HbPayload=23, posPayload=25, cmdPayload=5 Bytes. Diese Asserts direkt in
xComDef6_3.h aufnehmen, dann fällt eine versehentliche Strukturänderung
sofort beim Kompilieren auf:
```cpp
static_assert(sizeof(msgHeader) == 12, "Wire-Format geaendert!");
```

### 12. Template-Sendefunktionen gegen Pointer absichern
`broadcastMsg(code, ptr)` mit einem Pointer statt Struct würde die
Pointer-Bytes senden. Schutz:
```cpp
static_assert(!std::is_pointer<T>::value, "Struct uebergeben, keinen Pointer");
```

## Kleinkram / Stil

- `time_t now;` und `struct tm timeinfo;` in xComDef6_3.h: `now` wird in 6_3
  von keinem Programm mehr benutzt (Timestamp füllt die Sendeprozedur) —
  kann raus. `timeinfo` braucht nur noch `setUpTime()` → dort lokal machen.
- `stationDefinitions.Name` als `String` hält 15 Heap-Strings im RAM;
  `const char*` reicht und erlaubt wieder `const stationDefinitions device[]`
  (IP müsste dann in ein separates, beschreibbares Array, z.B.
  `byte deviceIP[15]`).
- `MAC`-Feld in der device dB wird nirgends verwendet — entfernen oder nutzen.
- Hinweis (gewollt, aber dokumentieren): durch den Versions-Check in
  `parseXMsg()` ignorieren 6_3-Geräte alle 6_2-Pakete stillschweigend.
  Mischbetrieb 6_2/6_3 ist also nicht möglich; während der Migration müssen
  alle beteiligten Geräte gleichzeitig umgestellt werden.
- Wire-Format ist Little-Endian (alle ESP32) — als Kommentar im Header
  festhalten, falls später mal ein PC-Tool mitlesen soll.
