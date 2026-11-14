// =============================================================================
//  EPF1_Stell - CYD28 (mit Drehknopf) als Bediengeraet zum Ausrichten des EPF1
//  Basis: PowerActor2/Development/AfterSerialRebuild/Step3_Display
//
//  Bedienung:
//    Drehknopf      : dreht den Turm (langsam drehen = fein, schnell = grob)
//    Touch AUF 0    : faehrt auf die gespeicherte 0
//    Touch NULL HIER: aktuelle Stellung wird die neue 0 (im EPF1 gespeichert).
//                     Sicherheitsabfrage: zweimal tippen (innert 3 s).
//                     Eine vorhandene Pose wird dabei verworfen (sie haengt an der 0).
//    Touch SCAN     : Rundumscan + Lokalisierung (Betriebsart Lidar)
//  Der Fahrbereich ist das physische Fenster 2048 +/-170 Grad (Kabel), unabhaengig
//  von der 0.
//
//  Protokoll: Stell-Protokoll aus ../EPF1Config.h (UDP 4330, kein xCom). Das CYD
//  sucht den EPF1 per Broadcast und merkt sich dann dessen IP (DHCP).
//  Board: CYD28 (ESP32-2432S028), FQBN esp32:esp32:esp32, per USB-COM flashen.
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Credentials.h>   // ssid, password
#include <LovyanGFX.hpp>
#define LGFX_AUTODETECT
#include <LGFX_AUTODETECT.hpp>
#include "../EPF1Config.h"

LGFX gfx;

// Encoder am CYD28 (aus Pa1_1_encoderTest6_3_1/dispDef.h)
#define ENC_A 22
#define ENC_B 27

#define ENC_FINE   4          // Ticks je Raste langsam (~0,35 Grad)
#define ENC_COARSE 32         // Ticks je Raste schnell (~2,8 Grad)
#define ENC_FAST_MS 60        // Rasten dichter als das = schnell
#define POLL_MS    300
#define SEND_GAP   60         // ms: Pan-Ziel hoechstens so oft senden

WiFiUDP udp;
IPAddress epfIp(255, 255, 255, 255);   // bis zur ersten Antwort Broadcast
StellState st = {};
bool     haveState = false;
uint32_t lastRx = 0;

int  target = panServoOrigin;
bool targetDirty = false;
bool touchDown = false;
int  flashBtn = -1;
uint32_t flashUntil = 0;
uint32_t zeroArmUntil = 0;             // NULL HIER: erste Beruehrung "scharf" bis ...

const char* BTN_TEXT[3] = {"AUF 0", "NULL HIER", "SCAN"};
#define BTN_Y   150
#define BTN_H   60
#define BTN_W   100
#define BTN_GAP 7
int btnX(int i) { return BTN_GAP + i * (BTN_W + BTN_GAP); }

// ── Encoder (1:1 aus Pa1_1_encoderTest6_3_1/dispProc.ino, Schritt variabel) ────
bool readEncoder(int &dir) {
  static int counter = 0, lastCounter = 0;
  static uint8_t old_AB = 3;
  static int8_t encval = 0;
  static const int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  lastCounter = counter;
  old_AB <<= 2;
  if (digitalRead(ENC_A)) old_AB |= 0x02;
  if (digitalRead(ENC_B)) old_AB |= 0x01;
  encval += enc_states[(old_AB & 0x0f)];
  if (encval > 3)       { counter++; encval = 0; }
  else if (encval < -3) { counter--; encval = 0; }
  if (lastCounter != counter) { dir = counter > lastCounter ? 1 : -1; return true; }
  return false;
}

// ── Senden / Empfangen ───────────────────────────────────────────────────────
void sendCmd(uint8_t type, uint16_t value) {
  StellCmd c = {STELL_MAGIC, type, value};
  udp.beginPacket(epfIp, STELL_PORT);
  udp.write((const uint8_t *)&c, sizeof(c));
  udp.endPacket();
}

bool receiveState() {
  int len = udp.parsePacket();
  if (len <= 0) return false;
  StellState s;
  if (len != sizeof(s)) { udp.flush(); return false; }
  IPAddress from = udp.remoteIP();
  udp.read((uint8_t *)&s, sizeof(s));
  if (s.magic != STELL_MAGIC) return false;
  epfIp = from;                                   // ab jetzt Unicast
  // Soll von EPF1 uebernehmen (erster Kontakt und immer, wenn gerade nichts Neues zu senden ist):
  // EPF1 begrenzt selbst (Kabelfenster, Betriebsart z.B. IR +/-85 Grad) -> die Anzeige
  // bleibt dann am Anschlag stehen, statt weiterzuzaehlen.
  if (!haveState || !targetDirty) target = s.panGoal;
  st = s;
  haveState = true;
  lastRx = millis();
  return true;
}

// ── Anzeige ──────────────────────────────────────────────────────────────────
float relDeg(int ticks, int origin) { return (ticks - origin) * 360.0f / 4096.0f; }

void drawButton(int i) {
  uint16_t bg = (i == flashBtn) ? TFT_YELLOW : TFT_DARKGREY;
  const char* txt = BTN_TEXT[i];
  if (i == 1 && millis() < zeroArmUntil) { bg = TFT_RED; txt = "SICHER?"; }
  if (i == 2 && haveState && st.busy) { bg = TFT_BLUE; txt = "scannt"; }
  int x = btnX(i);
  gfx.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 10, bg);
  gfx.setTextColor(TFT_BLACK, bg);
  gfx.setCursor(x + (BTN_W - (int)strlen(txt) * 12) / 2, BTN_Y + BTN_H / 2 - 8);
  gfx.print(txt);
}

void drawScreen() {
  gfx.startWrite();
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextSize(2);
  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setCursor(6, 6);
  gfx.print("EPF1 Ausrichtung");

  bool online = haveState && millis() - lastRx < 2000;
  gfx.setCursor(250, 6);
  gfx.setTextColor(online ? TFT_GREEN : TFT_RED, TFT_BLACK);
  gfx.print(online ? "ok" : "--");

  int origin = haveState ? st.panOrigin : panServoOrigin;
  gfx.setTextColor(TFT_YELLOW, TFT_BLACK);
  gfx.setCursor(6, 36);
  gfx.printf("Soll %4d  %+7.1f Grad", target, relDeg(target, origin));

  gfx.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  gfx.setCursor(6, 62);
  if (online && st.servoOk) gfx.printf(" Ist %4u  %+7.1f Grad", st.panPos, relDeg(st.panPos, origin));
  else if (online)          gfx.print(" Pan antwortet nicht");
  else                      gfx.print(" suche EPF1 ...");

  gfx.setCursor(6, 88);
  gfx.printf("   0 = %4d Ticks  %uC", origin, online ? st.tempC : 0);

  gfx.setTextSize(1);
  gfx.setCursor(6, 118);
  if (online && st.poseValid)
    gfx.printf("Pose: x %.2f m  y %.2f m  0 zeigt %.1f Grad  Spiegel %+d",
               st.poseX / 1000.0f, st.poseY / 1000.0f, st.poseHead * 360.0f / 4096.0f, st.poseMir);
  else
    gfx.print("Pose: keine");
  gfx.setCursor(6, 132);
  if (online) gfx.printf("EPF1 %s", epfIp.toString().c_str());

  gfx.setTextSize(2);
  for (int i = 0; i < 3; i++) drawButton(i);

  gfx.setTextSize(1);
  gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
  gfx.setCursor(6, 220);
  gfx.print("Drehknopf: langsam = fein (0,35 Grad), schnell = grob");
  gfx.setCursor(6, 230);
  gfx.print("NULL HIER zweimal tippen");
  gfx.endWrite();
}

// ── Arduino ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  gfx.init();
  gfx.setRotation(3);
  gfx.setBrightness(255);
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextSize(2);
  gfx.setTextColor(TFT_YELLOW, TFT_BLACK);
  gfx.setCursor(6, 6);
  gfx.print("WiFi ...");
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  udp.begin(STELL_PORT + 1);
  Serial.printf("EPF1_Stell ready, IP %s\n", WiFi.localIP().toString().c_str());
  sendCmd(ST_POLL, 0);
  drawScreen();
}

void loop() {
  static uint32_t tPoll = 0, tSend = 0, tDraw = 0, tLastDetent = 0;
  static bool redraw = false;

  int dir;
  if (readEncoder(dir)) {
    int step = (millis() - tLastDetent < ENC_FAST_MS) ? ENC_COARSE : ENC_FINE;
    tLastDetent = millis();
    target = constrain(target + dir * step, PAN_PHYS_MIN, PAN_PHYS_MAX);
    targetDirty = true;
    redraw = true;
  }

  uint16_t tx, ty;
  if (gfx.getTouch(&tx, &ty)) {
    if (!touchDown) {
      touchDown = true;
      for (int i = 0; i < 3; i++) {
        if ((int)tx >= btnX(i) && (int)tx < btnX(i) + BTN_W &&
            (int)ty >= BTN_Y && (int)ty < BTN_Y + BTN_H) {
          if (i == 0) { sendCmd(ST_GO_ZERO, 0); target = haveState ? st.panOrigin : panServoOrigin; targetDirty = false; }
          if (i == 1) {
            if (millis() < zeroArmUntil) { sendCmd(ST_ZERO_HERE, 0); zeroArmUntil = 0; }
            else zeroArmUntil = millis() + 3000;
          }
          if (i == 2) sendCmd(ST_SCAN, 0);
          tSend = tPoll = millis();
          flashBtn = i;
          flashUntil = millis() + 300;
          redraw = true;
          break;
        }
      }
    }
  } else {
    touchDown = false;
  }
  if (flashBtn >= 0 && millis() > flashUntil) { flashBtn = -1; redraw = true; }
  static bool armShown = false;
  bool armed = millis() < zeroArmUntil;
  if (armed != armShown) { armShown = armed; redraw = true; }

  if (targetDirty && millis() - tSend > SEND_GAP) {
    sendCmd(ST_PAN, target);
    targetDirty = false;
    tSend = tPoll = millis();
  }
  if (millis() - tPoll > POLL_MS) {
    if (haveState && millis() - lastRx > 5000) epfIp = IPAddress(255, 255, 255, 255);   // neu suchen
    sendCmd(ST_POLL, 0);
    tPoll = millis();
  }
  while (receiveState()) redraw = true;

  // Neuzeichnen hoechstens alle 150 ms, sonst verpasst readEncoder Rasten
  if (redraw && millis() - tDraw > 150) {
    drawScreen();
    tDraw = millis();
    redraw = false;
  }
}
