# PA1_1 – Ausbau Drehturm (Konzept, Stand 2026-11-09)

Status: **Überlegungen, nichts entschieden oder gebaut.** Gesprächsstand vom 2026-11-09
(Vorarbeit 2026-11-03). Strom über die Induktivkopplung wird später betrachtet. Annahme
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
  auf dem WLAN-Kanal des Access Points. Der Turm-ESP meldet sich ebenfalls im WLAN an.
  Dann stimmt der Kanal automatisch, und er lässt sich per **OTA** flashen (der Turm dreht
  sich, ein Kabel ist unpraktisch).
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
- **Entscheidung: damit leben.** Das Lidar misst dauernd, solange es Strom hat. Es wird
  nicht ein- und ausgeschaltet.
- Verworfene Alternativen: 5-V-Versorgung des Lidars per GPIO schalten, ein anderes Lidar
  mit Trigger-Modus (z. B. Benewake TFmini-S, TF-Luna, TF02-Pro; nicht geprüft, geringere
  Reichweite).
- Der **Ziellaser** ist davon nicht betroffen. Er hängt an einem eigenen GPIO
  („an für X ms“).

## Controller für den Turm

- Die Rechenarbeit (UART, GPIO, WS2812, ESP-NOW) ist trivial. **Ohne Kamera** reichen ein
  **ESP32-C3** (z. B. XIAO C3 mit externer Antenne, wenig Strom) oder ein **M5Stamp Pico**.
  Ein ESP32-S3 ist dafür überdimensioniert.
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
- Ob die Kamera kommt (falls ja, XIAO S3 Sense; sonst C3 oder Stamp Pico).
- Bewegt sich der Turm nur zum Zielen oder auch für einen Pose-Schwenk?
- Test vor dem Bau: Ping-Pong-Sketches unter `PowerActor1_1/Development/` (Verlust und
  Latenz von ESP-NOW bei laufender Spule und laufendem Schrittmotor).
