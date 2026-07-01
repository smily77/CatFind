// hwProc.ino -- Sim6_3_0: Datei-/Szenen-Verwaltung, Tastatur und UI

// --- Debug-/Boot-Log-Ausgabe (xComProc-Hooks) -----------------------------
// Waehrend des Boots scrollender Text auf dem Display; im UI-Betrieb nur Serial
// (die UI-Seite wuerde sonst ueberschrieben, z.B. beim WLAN-Reconnect).
void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
  if (!uiMode) { canvas << comment << endl; canvas.pushSprite(0, 0); }
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
  if (!uiMode) { canvas << comment; canvas.pushSprite(0, 0); }
}

void println_log(const char *str) { writelnComment(String(str)); }

// --- Zeit ------------------------------------------------------------------
// setUpTime() aus xComProc wartet endlos auf NTP - der mobile Cardputer haengt
// evtl. in einem Netz ohne Internet. Deshalb eigener Sync mit Timeout; ohne
// NTP laeuft alles weiter, nur die Uhrzeit-Anzeige bleibt leer.
void setupTimeShort() {
  writelnComment("NTP-Zeit (max. 10 s) ...");
  configTzTime(time_zone, ntpServer1, ntpServer2);
  if (getLocalTime(&timeinfo, NTP_TIMEOUT_MS)) writelnComment("Zeit ok");
  else writelnComment("keine NTP-Zeit");
}

String fmtTime(time_t t) {
  if (t < 1000000000) return "--:--:--";      // keine plausible Zeit (kein NTP)
  struct tm tmv;
  localtime_r(&t, &tmv);
  char b[10];
  snprintf(b, sizeof(b), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(b);
}

// --- WLAN-Ueberwachung (Langzeitaufnahme) -----------------------------------
// Faellt das WLAN nachts aus, verbindet der Sketch neu und tritt dem Multicast
// wieder bei (die AsyncUDP-Mitgliedschaft ueberlebt den Reconnect nicht).
void checkWifi() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < WIFI_CHECK_MS) return;
  lastCheck = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiOk) {                     // gerade wieder verbunden
      wifiOk = true;
      wifiReconnects++;
      udpMc.close();
      initMcUdp();                     // Multicast-Gruppe neu beitreten
      if (displayOn) drawUI();
    }
    return;
  }
  if (wifiOk) {                        // gerade verloren
    wifiOk = false;
    if (displayOn) drawUI();
  }
  WiFi.reconnect();                    // alle WIFI_CHECK_MS erneut versuchen
}

// --- Szenen-Dateien ----------------------------------------------------------
const char* slotPath(int slot) {
  static char p[16];
  snprintf(p, sizeof(p), "/scene%d.bin", slot);
  return p;
}

// einen Record (Header + Payload) aus der Datei lesen - Recordlänge ist variabel
bool readRecord(File &file, xMsg &m) {
  if (file.read((uint8_t*)&m.header, sizeof(msgHeader)) != sizeof(msgHeader)) return false;
  if (m.header.payloadLen > maxPayloadLen) return false;
  if (file.read(m.payload, m.header.payloadLen) != m.header.payloadLen) return false;
  return true;
}

// Record anhaengen; Datei wird pro Record geoeffnet und geschlossen -> bei
// Stromausfall in einer Langzeitaufnahme gehen keine frueheren Records verloren.
// Schreibfehler (volle/defekte Karte) werden gemeldet statt still ignoriert.
bool saveRecord(int slot, const xMsg &m) {
  File file = SD.open(slotPath(slot), FILE_APPEND);
  if (!file) { sdError = true; return false; }
  size_t want = sizeof(msgHeader) + m.header.payloadLen;
  size_t got  = file.write((uint8_t*)&m.header, sizeof(msgHeader));
  if (m.header.payloadLen) got += file.write(m.payload, m.header.payloadLen);
  file.close();
  sdError = (got != want);
  return !sdError;
}

long countRecords(int slot) {
  File file = SD.open(slotPath(slot), FILE_READ);
  if (!file) return 0;                       // Szene (noch) leer
  long record = 0;
  xMsg m;
  while (file.available() >= (int)sizeof(msgHeader)) {
    if (!readRecord(file, m)) {
      Serial << "Szene " << slot << ": Datei beschaedigt ab Record " << record << endl;
      break;
    }
    record++;
  }
  file.close();
  return record;
}

// --- Aktionen (Tastatur) ------------------------------------------------------
void handleKeys() {
  auto &kb = M5Cardputer.Keyboard;
  for (char c = '1'; c <= '0' + NUM_SLOTS; c++)
    if (kb.isKeyPressed(c)) { selectSlot(c - '0'); return; }
  if (kb.isKeyPressed('r')) { toggleRecord(); return; }
  if (kb.isKeyPressed('p')) { playScene(currentSlot); return; }
  if (kb.isKeyPressed('d')) { deleteRequest(); return; }
}

void wakeDisplay() {
  displayOn = true;
  M5Cardputer.Display.setBrightness(DISP_BRIGHT);
  drawUI();
}

void selectSlot(int slot) {
  if (doRecord) doRecord = false;            // Szenenwechsel beendet die Aufnahme
  deleteArmedMs = 0;
  currentSlot = slot;
  if (slotRecords[slot] < 0) slotRecords[slot] = countRecords(slot);
  drawUI();
}

void toggleRecord() {
  doRecord = !doRecord;
  deleteArmedMs = 0;
  if (doRecord) recStartTs = time(nullptr);
  drawUI();
}

// Loeschen erst beim zweiten 'd' innerhalb DELETE_CONFIRM_MS (Schutz fuer
// muehsam aufgezeichnete Langzeit-Szenen)
void deleteRequest() {
  if (deleteArmedMs && millis() - deleteArmedMs < DELETE_CONFIRM_MS) {
    deleteArmedMs = 0;
    doRecord = false;
    SD.remove(slotPath(currentSlot));
    slotRecords[currentSlot] = 0;
  } else {
    deleteArmedMs = millis();
  }
  drawUI();
}

// --- Playback -------------------------------------------------------------------
void playScene(int slot) {
  doRecord = false;                          // nie die eigene Wiedergabe aufnehmen!
  deleteArmedMs = 0;
  File file = SD.open(slotPath(slot), FILE_READ);
  if (!file) { drawUI(); return; }           // Szene leer
  long total = slotRecords[slot];
  long n = 0;
  xMsg m;
  while (file.available() >= (int)sizeof(msgHeader)) {
    if (!readRecord(file, m)) break;
    broadcastRawMsg(m);                      // Header unveraendert: Original-sender/-timeStamp
    n++;
    if ((n & 7) == 1 || n == total) drawPlayback(slot, n, total);
    delay(PLAY_GAP_MS);
    M5Cardputer.update();                    // beliebige Taste bricht ab
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) break;
  }
  file.close();
  mcDataReceived = false;                    // eigene, mitgehoerte Pakete verwerfen
  lastActivityMs = millis();
  drawUI();
}

// --- UI ----------------------------------------------------------------------------
// 240x135, drei Zonen: Kopfzeile (Titel/Uhr/WLAN), Hauptfeld (Szene + grosser
// Record-Zaehler + Status), Fusszeile (Tastenhilfe).
#define COL_HEAD   0x2124u   // dunkles Grau-Blau
#define COL_FOOT   0x18E3u   // dunkles Grau

void drawHeader(const char *title) {
  canvas.fillSprite(TFT_BLACK);
  canvas.fillRect(0, 0, 240, 20, COL_HEAD);
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(TFT_WHITE, COL_HEAD);
  canvas.drawString(title, 4, 10);
  canvas.setTextDatum(middle_center);
  canvas.drawString(fmtTime(time(nullptr)), 120, 10);
  canvas.setTextDatum(middle_right);
  if (wifiOk) {
    canvas.setTextColor(TFT_GREENYELLOW, COL_HEAD);
    canvas.drawString("WLAN", 222, 10);
  } else {
    canvas.setTextColor(TFT_RED, COL_HEAD);
    canvas.drawString("kein WLAN", 222, 10);
  }
  // gruener Punkt: in den letzten 2 s kam Multicast-Verkehr an (Netz lebt)
  if (millis() - lastTrafficMs < 2000) canvas.fillCircle(232, 10, 4, TFT_GREEN);
  else                                 canvas.drawCircle(232, 10, 4, TFT_DARKGREY);
}

void drawFooter(const char *help) {
  canvas.fillRect(0, 117, 240, 18, COL_FOOT);
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_LIGHTGREY, COL_FOOT);
  canvas.drawString(help, 120, 126);
}

void drawUI() {
  lastUiMs = millis();
  if (!uiMode) return;
  drawHeader("CatFind Sim");

  // Szene gross links, Record-Zaehler gross rechts
  canvas.setTextDatum(top_left);
  canvas.setFont(&fonts::DejaVu24);
  canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
  canvas.drawString("Szene " + String(currentSlot), 6, 30);
  canvas.setTextDatum(top_right);
  canvas.setFont(&fonts::DejaVu40);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.drawString(String(slotRecords[currentSlot]), 234, 24);
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
  canvas.drawString("Records", 234, 64);

  // Statuszeile
  canvas.setTextDatum(top_left);
  canvas.setFont(&fonts::DejaVu18);
  if (sdError) {
    canvas.setTextColor(TFT_RED, TFT_BLACK);
    canvas.drawString("SD-FEHLER!", 6, 90);
  } else if (deleteArmedMs && millis() - deleteArmedMs < DELETE_CONFIRM_MS) {
    canvas.setTextColor(TFT_ORANGE, TFT_BLACK);
    canvas.drawString("d nochmal = loeschen!", 6, 90);
  } else if (doRecord) {
    if ((millis() / 700) & 1) canvas.fillCircle(13, 99, 7, TFT_RED);
    canvas.setTextColor(TFT_RED, TFT_BLACK);
    canvas.drawString("REC seit " + fmtTime(recStartTs), 26, 90);
  } else {
    canvas.setTextColor(TFT_GREEN, TFT_BLACK);
    canvas.drawString("bereit", 6, 90);
  }
  // letzter gespeicherter Record (bei Langzeitaufnahme: lebt die Aufnahme noch?)
  if (lastRecTs) {
    canvas.setFont(&fonts::DejaVu12);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    canvas.drawString("letzter " + fmtTime(lastRecTs), 234, 96);
  }

  drawFooter("1-9 Szene   r Rec   p Play   d Del");
  canvas.pushSprite(0, 0);
}

void drawPlayback(int slot, long n, long total) {
  drawHeader("Playback");
  canvas.setTextDatum(top_left);
  canvas.setFont(&fonts::DejaVu24);
  canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
  canvas.drawString("Szene " + String(slot), 6, 30);
  canvas.setTextDatum(top_right);
  canvas.setFont(&fonts::DejaVu40);
  canvas.setTextColor(TFT_CYAN, TFT_BLACK);
  canvas.drawString(String(n), 234, 24);
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (total > 0) canvas.drawString("von " + String(total), 234, 64);
  // Fortschrittsbalken
  if (total > 0) {
    canvas.drawRect(6, 96, 228, 12, TFT_DARKGREY);
    canvas.fillRect(8, 98, (int)(224.0f * n / total), 8, TFT_CYAN);
  }
  drawFooter("beliebige Taste = Abbruch");
  canvas.pushSprite(0, 0);
}
