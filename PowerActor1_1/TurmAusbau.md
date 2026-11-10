# PA1_1 – Ausbau Drehturm (Konzept, Stand 2026-11-10)

Status: **Überlegungen, nichts entschieden oder gebaut.** Gesprächsstand vom 2026-11-10
(Vorarbeit 2026-11-03 und 2026-11-09). Strom über die Induktivkopplung wird später betrachtet. Annahme
bis dahin: Die Kopplung liefert genug Strom und 5 V.

## Ziel

Am Drehturm von PA1_1 sitzen Geräte, die ihren Strom **induktiv** bekommen. Damit fallen
die Kabel durch die Drehachse weg. Die Daten müssen deshalb **drahtlos** zwischen Turm
und Basis übertragen werden.

Auf den Turm kommen:
- Punkt-Lidar **AeroSelfie / Eppinno R50-C** (dToF, 50 m). Getestet in
  `C:\Users\stefan\Documents\Claude_Test\Aero_Selfie R50-C` (README und `R50C_Lidar.ino`)
- Ziellaser
- 1 LED-Pixel (WS2812)
- optional eine Kamera (siehe unten)

Das **Wasserventil bleibt in der Basis.** Feuern geht nie über Funk.

## Übertragungskonzept

- **ESP-NOW-Unicast** zwischen Turm-ESP und PA1_1-Basis (klassischer ESP32, .183).
- **Der Turm-ESP ist kein Busgerät.** Er hat keine Geräte-ID, keinen Eintrag in xComDef
  und kein deviceCount. Nach aussen bleibt PA1_1 ein einziges Gerät, und der Bus wird nicht
  belastet.
- **Kanal:** Die Basis muss für den Multicast-Bus im WLAN bleiben. ESP-NOW läuft deshalb
  auf dem WLAN-Kanal des Access Points. Seit 2026-11-10 meldet sich der Turm **nicht** mehr
  dauernd im WLAN an, weil er nach jedem Einschalten schnell bereit sein muss (siehe „Turm
  abschaltbar“):
  - Der Turm speichert den zuletzt gültigen Kanal im NVS. Er lernt ihn bei einer
    OTA-Anmeldung oder aus einer Nachricht der Basis.
  - Nach dem Booten sendet er sofort auf diesem Kanal. Kommt keine Antwort, probiert er die
    Kanäle 1–13 durch (je ca. 20 ms, zusammen höchstens ca. 0,3 s).
  - Der Kanal des Routers ändert sich praktisch nie, am besten stellt man ihn fest ein.
- **OTA nur auf Befehl:** Die Basis schickt „OTA-Modus“, erst dann meldet sich der Turm im
  WLAN an und lässt sich per OTA flashen (der Turm dreht sich, ein Kabel ist unpraktisch).
  Das stört ESP-NOW nicht, weil beides auf dem gleichen Kanal läuft.
- **Turm → Basis:** Das Lidar wird auf dem Turm ausgelesen und geprüft. Gesendet wird ein
  kompaktes Paket (Distanz mm, Stärke, Zähler, Zeitstempel), keine rohen UART-Bytes.
  Gebündelt etwa alle 20–50 ms, nur solange die Basis Messwerte anfordert (Zielen und
  Schwenken). Sonst sendet der Turm alle 1–2 s ein Lebenszeichen.
- **Basis → Turm:** Laser „an für X ms“ und die LED-Farbe.
- **Laser-Sicherheit:** Der Turm schaltet den Ziellaser selbst ab, sobald die Zeit abläuft
  oder von der Basis nichts mehr kommt. Ein Funkausfall darf den Laser nie eingeschaltet
  lassen.
- Die Datenmenge ist winzig (unter 1 kB/s), die Latenz von ESP-NOW liegt bei etwa 1–3 ms,
  und die Strecke ist nur Zentimeter lang.
- Reserve (vom 2026-11-03): IR-UART (IrDA) durch die Achse, falls die Spulenmitte frei ist.

### Bluetooth geprüft und verworfen (2026-11-10)

- **BLE:** Die Basis müsste WLAN (für den Bus) und BLE zugleich betreiben. Der ESP32 hat nur
  ein 2,4-GHz-Funkteil und teilt die Sendezeit zwischen beiden auf (Coexistence). Dadurch
  schwankt die Latenz stärker, und auch der WLAN-Empfang leidet, besonders bei Multicast,
  weil verpasste Multicast-Pakete nicht wiederholt werden. Das Verbindungsintervall liegt bei
  mindestens 7,5 ms, üblich sind 15–50 ms. Dazu kommen der Verbindungsaufbau und
  GATT-Dienste sowie zusätzlicher Speicher (NimBLE grob 100 kB Flash und 50 kB RAM).
- **Bluetooth Classic (SPP):** Das ginge nur mit einem klassischen ESP32 auf dem Turm (z. B.
  Stamp Pico), nicht mit C3 oder S3. Der Vorteil wäre, dass der Turm kein WLAN braucht und
  die UART-Bytes nahezu 1:1 weiterreicht. Dagegen spricht:
  - In der Basis teilen sich WLAN und Bluetooth Classic das Funkteil noch schlechter als
    WLAN und BLE.
  - Der Bluetooth-Stack (Bluedroid) ist gross, die OTA-Partition wird dann knapp.
  - Die Latenz liegt eher bei 10–30 ms und schwankt.
  - SPP ist ein Datenstrom ohne Paketgrenzen.
  - **Ausschlaggebend ist die Anlaufzeit:** Bluedroid starten und die Verbindung aufbauen
    dauert ca. 1,5–3 s, ESP-NOW braucht ca. 0,3–0,5 s. Weil der Turm abgeschaltet wird
    (siehe unten), zählt das am meisten.

## Turm abschaltbar (2026-11-10)

Lidar, Laser und Pixel am Turm braucht es **nur** für die Pose, die Statusanzeige und das
Schussprotokoll. Deshalb schaltet die Basis die **Speisung der Induktivübertragung über das
Relais** ab, wenn der Turm nicht gebraucht wird. Dann misst auch das R50-C nicht dauernd
(es selbst kann nicht in Standby, siehe unten).

- **Einschalten bei `catObserved`**, nicht erst bei `catDetected`. Das gibt mehr Vorlauf.
  Ausserdem bei Pose-Schwenk und Zielen.
- **Nachlaufzeit:** `catObserved` kommt oft. Damit das Relais nicht flattert, bleibt der
  Turm nach dem letzten `catObserved`/`catDetected` noch eine Weile an (z. B. 30–60 s, als
  Parameter). Erst danach wird abgeschaltet.
- Vor einem Schuss wartet die Basis auf das erste Lebenszeichen des Turms. Ist der Turm noch
  nicht bereit, gibt es kein Lidar und kein Beweisbild, gefeuert wird trotzdem (Lidar und
  Kamera sind nur Protokoll, siehe unten).
- **Laser-Sicherheit:** Strom aus heisst Laser aus. Der Zeitablauf auf dem Turm („Laser an
  für X ms“, Abschalten bei Funkausfall) bleibt für den eingeschalteten Zustand.
- **Statusanzeige:** Der Pixel leuchtet nur, solange der Turm Strom hat. Soll ein Status
  dauernd sichtbar sein, gehört er in die Basis.
- **Relais:** Ein paar Dutzend Schaltungen am Tag sind unkritisch. Der Einschaltstrom der
  Induktivübertragung muss noch geprüft werden.

Geschätzte Zeit vom Einschalten des Relais bis zum ersten Lidar-Wert (nicht gemessen):

| Schritt | ESP-NOW |
|---|---|
| Induktivübertragung bis 5 V stabil | ca. 10–50 ms |
| ESP booten bis `setup()` | ca. 150–300 ms (mit Bootloader-Tuning weniger) |
| WLAN-Treiber ohne Anmeldung starten | ca. 50–100 ms |
| Verbindung | keine, sofort sendebereit (Kanal aus NVS) |
| **Summe** | **ca. 0,3–0,5 s** |
| R50-C bis zum ersten Frame | unbekannt, messen |

## Lidar R50-C: UART bleibt, kein I2C

- **UART beibehalten** (Werkseinstellung, 115200 Baud). Der Sensor sendet von selbst und
  braucht nur eine Leitung (Sensor-TX → ESP-RX). Jeder Frame hat Prüfsumme und Paketzähler.
  Der Parser `uartRead()` aus dem Test-Sketch lässt sich übernehmen.
- I2C ist am Modul **nicht getestet**. Unklar ist auch, ob man aus dem I2C-Modus per Tool
  wieder auf UART zurückkommt. Ein abgeschalteter Sensor würde über die I2C-Pull-ups
  halb mitversorgt.
- **Es gibt keine Einzelmessung und kein Stopp/Standby.** Geprüft am 2026-11-09:
  - Im Datenblatt R50 V1.1 steht nur das Ausgabeformat (UART-Frame `0xDF…`, I2C-Register
    `0x00`–`0x04` nur lesen). Es gibt keinen Befehl an den Sensor und keinen Enable-Pin.
    Die Messrate ist mit 200 Hz angegeben.
  - Das Konfigurations-Tool `tof.corvon.tech` kennt nur `CMD_Set`. Damit lassen sich
    Protokoll, Baudrate, I2C-Adresse, Ausrichtung und Ausgabefrequenz (1, 2, 5, 10, 25, 50,
    100 Hz) einstellen. Alles wird im Modul gespeichert und gilt erst nach einem Neustart.
    Befehle werden offenbar nur kurz nach dem Einschalten angenommen („boot window“).
  - Ob eine niedrigere Ausgabefrequenz auch das interne Messen bremst, ist nicht belegt.
- **Entscheidung: damit leben.** Das Lidar misst dauernd, solange es Strom hat. Es bekommt
  keinen eigenen Schalter. Seit 2026-11-10 wird aber der **ganze Turm** über das Relais der
  Induktivübertragung abgeschaltet (siehe „Turm abschaltbar“), damit ist auch das Lidar aus.
- Verworfene Alternativen: 5-V-Versorgung nur des Lidars per GPIO schalten, ein anderes Lidar
  mit Trigger-Modus (z. B. Benewake TFmini-S, TF-Luna, TF02-Pro; nicht geprüft, geringere
  Reichweite).
- Der **Ziellaser** ist davon nicht betroffen. Er hängt an einem eigenen GPIO
  („an für X ms“).

## Controller für den Turm

- Die Rechenarbeit (UART, GPIO, WS2812, ESP-NOW) ist trivial. **Ohne Kamera** reichen ein
  **ESP32-C3** oder ein **M5Stamp Pico**. Ein ESP32-S3 ist dafür überdimensioniert.
- Weil Bluetooth Classic verworfen ist, braucht es keinen klassischen ESP32 auf dem Turm.
  Stand 2026-11-10 ist der **XIAO ESP32-C3** die erste Wahl:
  - ESP-NOW zwischen C3 und dem klassischen ESP32 der Basis geht ohne Weiteres.
  - USB-C ist eingebaut, das erste Flashen geht also ohne USB-Seriell-Adapter. Der Stamp
    Pico hat keinen USB-Anschluss und braucht einen Adapter.
  - Er hat eine externe U.FL-Antenne für die OTA-Anmeldung im WLAN. Für ESP-NOW über
    wenige Zentimeter ist die Antenne egal.
  - Er braucht wenig Strom und bietet genug Pins: Lidar-RX auf einem eigenen UART (`Serial`
    läuft über USB), Laser-GPIO, WS2812 über RMT. Strapping-Pins (GPIO2, 8, 9) für Laser und
    Pixel meiden.
  - Der Stamp Pico bleibt als Ersatz möglich und bringt keinen Nachteil, solange kein
    Bluetooth Classic gebraucht wird.
- **Mit Kamera** wäre die **XIAO ESP32-S3 Sense** die erste Wahl, nicht die ESP32-CAM:
  - genug freie Pins (Lidar-RX, Laser, Pixel)
  - externe U.FL-Antenne (Bilder müssen per WLAN in den Garten hinaus)
  - 8 MB PSRAM, USB-C und OTA
  - Bei der ESP32-CAM sind die Pins knapp (mit Boot-Fallen), die Antenne ist auf der
    Platine und Flashen geht nur über einen USB-Seriell-Adapter.
- Strom: Das R50-C braucht **5 V** (bei 3,3 V unzuverlässig, ca. 50 mA). Mit Kamera und
  WLAN eher 250–350 mA und Sendespitzen. Ein Pufferkondensator ist nötig. Ein Strombudget
  der Kopplung folgt später.

## Option Kamera auf dem Turm (offen, nicht sicher, ob wir das machen)

Die Kamera dreht sich mit und sieht genau das, worauf Laser und Wasserstrahl zeigen.

**Zweck: Schussprotokoll.** Der User hat am 2026-11-09 festgehalten: Das Lidar sieht die
Katze nicht immer, die Kamera auch nicht. Kamera und Lidar dienen deshalb **nur zur
Protokollierung der Schüsse** und sind keine harte Freigabe.

Mögliche Abläufe:
1. **Beweisbild pro Schuss:** ein Bild beim Zielen bzw. vor dem Schuss und eines etwa
   300 ms nach dem Schuss. Beide liegen im PSRAM und werden **erst nach dem Schuss** per
   WLAN direkt an den VPS geschickt (`POST /photo`, wie CatCam/KIVision), mit Track- und
   Schussnummer passend zu `actor_log`. Bilder gehen nicht über ESP-NOW und nicht über
   die Basis. Es gibt kein Dauer-Livebild, sonst wird die Steuerverbindung träge.
2. **Zielkontrolle vor dem Schuss** (zunächst nur protokolliert):
   - Lidar-Distanz mit der erwarteten Distanz zum Ziel vergleichen: etwa gleich = etwas
     im Strahl, viel weiter = ins Leere, viel näher = Hindernis.
   - Ein Ausschnitt um die Bildmitte, dessen Grösse sich aus der Lidar-Distanz ergibt
     (erwartete Katzengrösse), geht an ein kleines Modell auf dem S3 (Edge Impulse FOMO
     oder ESP-DL, geschätzt 100–300 ms, nicht gemessen). Ein passendes Modell gibt es noch
     nicht. Es liefe im Schattenbetrieb, und die Beweisbilder wären die Trainingsdaten.
   - Lokal auf dem Turm, ohne VPS, Manager oder Pi (Regel „keine zentrale Abhängigkeit“).
   - Falls die Kontrolle später doch eingreifen soll, wäre die Vorstellung ein Schalter
     wie bei der Zielquelle: a) trotzdem feuern / b) nicht feuern / c) einstellbar, am
     Anfang auf a.
- **Nachtsicht ist Pflicht** (die Tiere kommen nachts): Kameramodul ohne IR-Filter (NoIR)
  und ein **IR-Blitz** nur für den Moment der Aufnahme (spart Strom gegenüber Dauer-IR).
  Noch zu prüfen: ob ein NoIR-OV2640 an den 24-poligen Kamerastecker der Sense passt.
  Ausserdem wird die Sense warm, also auf das Gehäuse achten.

## Offene Punkte

- Welches Induktivmodul, und was liefert es auf der Turmseite (Spannung, Strom)?
- Ob die Kamera kommt (falls ja, XIAO S3 Sense; sonst XIAO C3, Ersatz Stamp Pico).
- Anlaufzeit messen: Relais ein bis zum ersten gültigen Lidar-Frame bei der Basis
  (Induktivübertragung, Boot, R50-C-Anlauf, Kanal aus NVS).
- Nachlaufzeit nach `catObserved`/`catDetected` festlegen (Parameter, Vorschlag 30–60 s).
- Einschaltstrom der Induktivübertragung (Relais, Netzteil).
- Bewegt sich der Turm nur zum Zielen oder auch für einen Pose-Schwenk?
- Test vor dem Bau: Ping-Pong-Sketches unter `PowerActor1_1/Development/` (Verlust und
  Latenz von ESP-NOW bei laufender Spule und laufendem Schrittmotor).
