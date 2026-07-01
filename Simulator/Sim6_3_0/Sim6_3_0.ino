//Sim_5_4 - letzte Version mit eigener udpDef/udpProc
//6_3 - analog zu den übrigen Programmen auf die gemeinsamen xComDef6_3/xComProc6_3 umgestellt:
//      * Nachrichten = fixer Header + variabler Payload, Empfang über initMcUdp Callback
//      * Aufzeichnung auf SD speichert Header + payloadLen Bytes (variable Recordlänge)
//      * Playback sendet mit broadcastRawMsg, originaler sender/timeStamp bleibt erhalten
//      * WLAN-Zugangsdaten aus Credentials.h, Simulator ist als Sim in der device dB
//6_3 (2. Ausbau) - Szenen, Langzeitaufnahme, UI:
//      * 9 Szenen-Dateien /scene1.bin../scene9.bin (Tasten 1-9); alte /data.bin
//        wird beim Boot automatisch zu Szene 1
//      * Langzeit-tauglich: nur catObserved wird aufgezeichnet (keine HBs), jeder
//        Record wird einzeln geschrieben und die Datei geschlossen (stromausfall-
//        sicher), Display schaltet nach Timeout dunkel, WLAN-Reconnect mit
//        Multicast-Rejoin (Aufnahme läuft über Nacht / mehrere Tage durch)
//      * farbige UI mit grossen Fonts statt Scrolltext; r=Record, p=Play,
//        d(2x)=Löschen; Playback mit beliebiger Taste abbrechbar
//      * Fixes aus REVIEW_6_3: Playback nimmt sich nicht mehr selbst auf,
//        kein veraltetes Paket beim Aufnahmestart, SD-Schreibfehler werden
//        gemeldet, OTA aktiv, NTP mit Timeout (mobil evtl. ohne Internet)
#include <xComDef6_3.h>
#include "hwDef.h"
#include <SPI.h>
#include <SD.h>
#include <M5Cardputer.h>

#define DEBUG true
byte ID = Sim;

M5Canvas canvas(&M5Cardputer.Display);

// --- Szenen-Verwaltung ---
int  currentSlot = 1;               // aktive Szene (1..NUM_SLOTS)
long slotRecords[NUM_SLOTS + 1];    // Record-Zahl je Szene (-1 = noch nicht gezählt)
bool doRecord = false;

// --- Status (Langzeit/UI) ---
bool   uiMode        = false;       // false = Boot-Log (Scrolltext), true = UI-Seite
bool   displayOn     = true;
bool   sdError       = false;       // letzter SD-Schreibversuch fehlgeschlagen
bool   wifiOk        = true;
int    wifiReconnects = 0;
time_t recStartTs    = 0;           // Beginn der laufenden Aufzeichnung
time_t lastRecTs     = 0;           // Zeitpunkt des letzten gespeicherten Records
unsigned long lastActivityMs = 0;   // letzte Taste (fürs Display-Timeout)
unsigned long lastTrafficMs  = 0;   // letztes empfangenes Multicast-Paket
unsigned long lastUiMs       = 0;   // letzter UI-Refresh (Uhr)
unsigned long deleteArmedMs  = 0;   // Löschen scharf: zweiter 'd'-Druck nötig

#include <xComProc6_3.h>

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(DISP_BRIGHT);
  canvas.setColorDepth(8);
  canvas.createSprite(M5Cardputer.Display.width(),
                      M5Cardputer.Display.height());
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextColor(TFT_GREEN, TFT_BLACK);
  canvas.setTextScroll(true);              // Boot-Log scrollt

  for (int i = 0; i <= NUM_SLOTS; i++) slotRecords[i] = -1;

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    println_log("SD-Karte fehlt/defekt!");
    while (1) delay(100);
  }
  // Migration: alte Einzeldatei /data.bin wird zu Szene 1
  if (SD.exists(LEGACY_PATH) && !SD.exists(slotPath(1)))
    SD.rename(LEGACY_PATH, slotPath(1));
  println_log("SD-Karte ok");

  setUpWifi(device[ID].IP);
  initMcUdp();
  setUpOTA();                              // OTA-Hostname "Simulator"
  setupTimeShort();                        // NTP mit Timeout statt Endlosschleife

  slotRecords[currentSlot] = countRecords(currentSlot);
  println_log("Simulator bereit");

  uiMode = true;                           // ab jetzt UI-Seite statt Boot-Log
  canvas.setTextScroll(false);
  lastActivityMs = millis();
  drawUI();
}

void loop() {
  ArduinoOTA.handle();
  M5Cardputer.update();
  checkWifi();                             // Langzeit: WLAN-Ausfall überbrücken

  // Tastatur: erster Druck bei dunklem Display weckt nur auf
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    lastActivityMs = millis();
    if (!displayOn) wakeDisplay();
    else            handleKeys();
  }

  // Multicast IMMER konsumieren - sonst bliebe bei Aufnahme-Start ein
  // veraltetes Paket im Puffer liegen (REVIEW_6_3 Punkt "Kleinkram")
  if (mcDataReceived) {
    xMsg m = lastMcMsg;
    mcDataReceived = false;
    lastTrafficMs = millis();
    if (doRecord && m.header.msgCode == catObserved) {
      if (saveRecord(currentSlot, m)) {
        slotRecords[currentSlot]++;
        lastRecTs = time(nullptr);
      }
      if (displayOn) drawUI();             // auch SD-Fehler sofort anzeigen
    }
  }

  // Display-Timeout: bei Langzeitaufnahme dunkel (Strom/Panel schonen)
  if (displayOn && millis() - lastActivityMs > DISPLAY_TIMEOUT_MS) {
    displayOn = false;
    M5Cardputer.Display.setBrightness(0);
  }

  // Uhr/Statuszeile im Sekundentakt auffrischen
  if (displayOn && millis() - lastUiMs > 1000) drawUI();
}
