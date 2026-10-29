// Basis-Prozeduren: HB und Anzeige auf dem AtomS3-Display (128x128).
//   SCHARF = roter Grund, AUS = dunkel. Darunter: aktive Zeit heute, Licht-Zustand
//   mit Restzeit, Hue-Zustand, letzte Meldung. Knopf (ganzes Display) = umschalten.

M5Canvas canvas(&M5.Display);
unsigned long dispNextMs = 0;

void heardBeat() {
  if (millis() - timer >= periodeForHB) {   // wrap-sicher
    hbPayload hb;
    hb.ip = getLastIpByte();
    hb.HBperiode = periodeForHB;
    broadcastMsg(HB, hb);
    timer = millis();
  }
}

void displayInit() {
  M5.Display.setRotation(0);
  M5.Display.setBrightness(60);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  displayBoot("Start ...");
}

void displayBoot(const char* what) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.drawString("Besucherlicht", M5.Display.width() / 2, 40);
  M5.Display.drawString(what, M5.Display.width() / 2, 70);
}

void displayForce() { dispNextMs = 0; }

static String timeStr(time_t t) {
  if (!t) return "-";
  struct tm lt; localtime_r(&t, &lt);
  char b[6]; snprintf(b, sizeof b, "%02d:%02d", lt.tm_hour, lt.tm_min);
  return String(b);
}

void displayTick() {
  if ((long)(millis() - dispNextMs) < 0) return;
  dispNextMs = millis() + 1000;
  bool armed = settingOn(stgArmed);
  int W = canvas.width();
  canvas.fillSprite(armed ? 0x8000 : TFT_BLACK);          // dunkelrot / schwarz
  canvas.setTextDatum(top_center);
  canvas.setTextColor(TFT_WHITE);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.drawString(armed ? "SCHARF" : "AUS", W / 2, 4);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(top_left);
  int y = 34;
  auto line = [&](const String& s, uint16_t col) { canvas.setTextColor(col); canvas.drawString(s, 3, y); y += 16; };
  if (timeValid) {
    int s, e; activeWindow(s, e);
    line(String(inActiveTime() ? "aktiv " : "Zeit ") + hhmm(s) + "-" + hhmm(e), inActiveTime() ? TFT_YELLOW : 0xAD55);
  } else line("ohne Uhrzeit", TFT_ORANGE);
  if (lightMine) {
    long rest = (long)(offAtMs - millis()) / 1000;
    if (rest < 0) rest = 0;
    line("Licht AN " + String(rest / 60) + ":" + (rest % 60 < 10 ? "0" : "") + String(rest % 60), TFT_GREEN);
  } else line("Licht: " + timeStr(lastTrigT), 0xAD55);
  if (!hueOk) line("Hue FEHLER", TFT_RED);
  else if (hueErr.length()) line("Hue: Gruppe?", TFT_ORANGE);
  else line("Hue ok", 0xAD55);
  line("Besuch " + timeStr(lastVisitT), 0xAD55);
  line(String("Cfg ") + cfgSource + (WiFi.status() == WL_CONNECTED ? "" : " !WLAN"),
       cfgSource == "VPS" ? 0xAD55 : TFT_ORANGE);
  canvas.pushSprite(0, 0);
  M5.Display.setBrightness(armed ? 80 : 25);
}

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}
