// =============================================================================
//  Step4_Display - Display-/Encoder-Seite zu Step4_LidarLaser (PowerActor2)
//  Basis: Step3_Display (Servo-Bedienung unveraendert) + Lidar-/Laser-Anzeige.
//
//  Bedienung:
//    Drehknopf          : faehrt den Servo direkt (ENC_STEP Ticks je Raste)
//    Touch << LINKS     : so schnell wie moeglich an den linken Rand (0 = -180 Grad)
//    Touch MITTE        : so schnell wie moeglich in die Mitte (2048 = 0 Grad)
//    Touch RECHTS >>    : so schnell wie moeglich an den rechten Rand (4095 = +180 Grad)
//  Anzeige: Servo Soll/Ist/Temperatur, Lidar-Distanz (bzw. Fehler), Laser EIN/AUS.
//  Der Laser schaltet der PowerActor2 selbst (< 0,5 m), das Display zeigt ihn nur.
//
//  Kein Flackern: der feste Teil (Titel, Knoepfe, Hinweise) wird EINMAL
//  gezeichnet; danach werden nur geaenderte Werte ueberschrieben - Text mit
//  Hintergrundfarbe auf fester Breite, kein fillScreen mehr.
//
//  Touch: XPT2046 des CYD28, von LGFX_AUTODETECT eingerichtet (wie
//  Displays/Bedienung_Einstellungen), nur die Druck-Flanke loest aus.
//  Protokoll: eigenes Test-Protokoll, identisch in Step4_LidarLaser (kein xCom).
//  Board: CYD28 (ESP32-2432S028), FQBN esp32:esp32:esp32, per USB-COM flashen.
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Credentials.h>   // ssid, password
#include <LovyanGFX.hpp>
#define LGFX_AUTODETECT
#include <LGFX_AUTODETECT.hpp>

LGFX gfx;

// Encoder am CYD28 (aus Pa1_1_encoderTest6_3_1/dispDef.h)
#define ENC_A 22
#define ENC_B 27

#define ENC_STEP  16          // Servo-Ticks je Raste (~1,4 Grad)
#define POLL_MS   150
#define SEND_GAP  60          // ms: Servo-Ziel hoechstens so oft senden

// ── Protokoll (identisch in Step4_LidarLaser) ────────────────────────────────

#define PA2_IP            IPAddress(192, 168, 0, 181)
#define STEP2_CMD_PORT    4310
#define STEP2_STATE_PORT  4311
#define STEP4_MAGIC       0x54
#define SERVO_MIN         0
#define SERVO_MAX         4095
#define SERVO_CENTER      2048
#define LIDAR_NONE        0xFF

enum : uint8_t { S2_POLL = 0, S2_SERVO = 1, S2_FAST = 3 };

struct __attribute__((packed)) Step2Cmd {
  uint8_t  magic;
  uint8_t  type;
  uint16_t value;
};

struct __attribute__((packed)) Step4State {
  uint8_t  magic;
  uint8_t  servoOk;
  uint16_t servoGoal;
  uint16_t servoPos;
  uint8_t  tempC;
  uint8_t  lidarErr;
  uint32_t lidarMm;
  uint8_t  laserOn;
};

WiFiUDP udp;
Step4State st = {};
bool     haveState = false;
uint32_t lastRx    = 0;

// ── Bedienzustand ────────────────────────────────────────────────────────────

int  target    = SERVO_CENTER; // Servo-Soll am Display
bool targetDirty = false;
bool touchDown = false;       // Entprellung: nur Druck-Flanke feuert
int  flashBtn  = -1;          // zuletzt gedrueckter Knopf (kurz hell)
uint32_t flashUntil = 0;

// Ein Anzeigefeld: schreibt nur, wenn sich Text oder Farbe geaendert haben,
// und fuellt immer auf die volle Feldbreite (alte, laengere Texte verschwinden
// ohne vorheriges Loeschen).
struct Field {
  int x, y, chars, size;
  char last[40];
  uint16_t lastFg, lastBg;
};

// Touch-Knoepfe: nebeneinander, gleiche Breite
struct FastBtn { const char *text; uint16_t pos; };
const FastBtn BTNS[3] = {{"<< LINKS", SERVO_MIN}, {"MITTE", SERVO_CENTER}, {"RECHTS >>", SERVO_MAX}};
#define BTN_Y   150
#define BTN_H   62
#define BTN_W   100
#define BTN_GAP 7
int btnX(int i) { return BTN_GAP + i * (BTN_W + BTN_GAP); }


// ── Encoder (1:1 aus Pa1_1_encoderTest6_3_1/dispProc.ino) ───────────────────

bool readEncoder(int &encoderValue, int counterStep) {
  static int counter = 0;
  static int lastCounter = 0;

  lastCounter = counter;
  static uint8_t old_AB = 3;
  static int8_t encval = 0;
  static const int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

  old_AB <<= 2;
  if (digitalRead(ENC_A)) old_AB |= 0x02;
  if (digitalRead(ENC_B)) old_AB |= 0x01;
  encval += enc_states[(old_AB & 0x0f)];

  if (encval > 3)       { counter++; encval = 0; }
  else if (encval < -3) { counter--; encval = 0; }

  if (lastCounter != counter) {
    if (counter > lastCounter) encoderValue += counterStep;
    if (counter < lastCounter) encoderValue -= counterStep;
    return true;
  }
  return false;
}

// ── Senden / Empfangen ───────────────────────────────────────────────────────

void sendCmd(uint8_t type, uint16_t value) {
  Step2Cmd c = {STEP4_MAGIC, type, value};
  udp.beginPacket(PA2_IP, STEP2_CMD_PORT);
  udp.write((const uint8_t *)&c, sizeof(c));
  udp.endPacket();
}

bool receiveState() {
  int len = udp.parsePacket();
  if (len <= 0) return false;
  Step4State s;
  if (len != sizeof(s)) { udp.flush(); return false; }
  udp.read((uint8_t *)&s, sizeof(s));
  if (s.magic != STEP4_MAGIC) return false;
  if (!haveState) target = s.servoGoal;   // beim ersten Kontakt Soll uebernehmen
  st = s;
  haveState = true;
  lastRx = millis();
  return true;
}

// ── Anzeige ──────────────────────────────────────────────────────────────────

float ticksToDeg(int t) { return (t - 2048) * 360.0f / 4096.0f; }

void drawField(Field &f, uint16_t fg, uint16_t bg, const char *fmt, ...) {
  char buf[40];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  int n = strlen(buf);
  while (n < f.chars && n < (int)sizeof(buf) - 1) buf[n++] = ' ';
  buf[n] = 0;
  if (!strcmp(buf, f.last) && fg == f.lastFg && bg == f.lastBg) return;
  gfx.setTextSize(f.size);
  gfx.setTextColor(fg, bg);
  gfx.setCursor(f.x, f.y);
  gfx.print(buf);
  strcpy(f.last, buf);
  f.lastFg = fg; f.lastBg = bg;
}

Field fOnline = {270, 6,   2, 2, "", 0, 0};
Field fSoll   = {6,   36, 26, 2, "", 0, 0};
Field fIst    = {6,   60, 26, 2, "", 0, 0};
Field fLidar  = {6,   92, 11, 3, "", 0, 0};
Field fLaser  = {216, 96,  8, 2, "", 0, 0};
Field fLidInfo= {6,  124, 52, 1, "", 0, 0};

void drawButton(int i) {
  uint16_t bg = (i == flashBtn) ? TFT_YELLOW : TFT_DARKGREY;
  int x = btnX(i);
  gfx.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 10, bg);
  gfx.setTextSize(2);
  gfx.setTextColor(TFT_BLACK, bg);
  gfx.setCursor(x + (BTN_W - (int)strlen(BTNS[i].text) * 12) / 2, BTN_Y + BTN_H / 2 - 8);
  gfx.print(BTNS[i].text);
}

void drawStatic() {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextSize(2);
  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setCursor(6, 6);
  gfx.print("PowerActor2 Step4");
  for (int i = 0; i < 3; i++) drawButton(i);
  gfx.setTextSize(1);
  gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
  gfx.setCursor(6, 228);
  gfx.print("Drehknopf = Servo  |  Touch = schnell nach -180 / 0 / +180");
}

const char *lidarErrText(uint8_t e) {
  switch (e) {
    case 1:  return "Signal zu schwach";
    case 2:  return "Signal zu stark";
    case 3:  return "ausser Reichweite";
    case 4:  return "Systemfehler";
    default: return "Fehler";
  }
}

void drawValues() {
  bool online = haveState && millis() - lastRx < 2000;
  drawField(fOnline, online ? TFT_GREEN : TFT_RED, TFT_BLACK, online ? "ok" : "--");
  drawField(fSoll, TFT_YELLOW, TFT_BLACK, "Soll %4d %6.1f", target, ticksToDeg(target));

  if (online && st.servoOk)
    drawField(fIst, TFT_LIGHTGREY, TFT_BLACK, "Ist  %4u %6.1f  %uC",
              st.servoPos, ticksToDeg(st.servoPos), st.tempC);
  else
    drawField(fIst, TFT_LIGHTGREY, TFT_BLACK, online ? "Servo antwortet nicht"
                                                     : "warte auf PowerActor2");

  if (!online || st.lidarErr == LIDAR_NONE) {
    drawField(fLidar, TFT_DARKGREY, TFT_BLACK, "--.--- m");
    drawField(fLidInfo, TFT_DARKGREY, TFT_BLACK, online ? "Lidar: keine Messung" : "");
  } else if (st.lidarErr != 0) {
    drawField(fLidar, TFT_ORANGE, TFT_BLACK, "--.--- m");
    drawField(fLidInfo, TFT_ORANGE, TFT_BLACK, "Lidar: %s", lidarErrText(st.lidarErr));
  } else {
    drawField(fLidar, st.lidarMm < 500 ? TFT_RED : TFT_WHITE, TFT_BLACK,
              "%2lu.%03lu m", (unsigned long)(st.lidarMm / 1000), (unsigned long)(st.lidarMm % 1000));
    drawField(fLidInfo, TFT_DARKGREY, TFT_BLACK, "Lidar ok  -  Laser EIN unter 0,5 m");
  }

  bool laser = online && st.laserOn;
  drawField(fLaser, TFT_BLACK, laser ? TFT_RED : TFT_DARKGREY, laser ? " LASER " : " Laser ");
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
  udp.begin(STEP2_STATE_PORT);
  Serial.printf("Step4_Display ready, IP %s\n", WiFi.localIP().toString().c_str());
  sendCmd(S2_POLL, 0);
  drawStatic();
  drawValues();
}

void loop() {
  static uint32_t tPoll = 0, tSend = 0, tDraw = 0;
  static bool redraw = false;

  int v = target;
  if (readEncoder(v, ENC_STEP)) {
    target = constrain(v, SERVO_MIN, SERVO_MAX);
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
          target = BTNS[i].pos;
          targetDirty = false;
          sendCmd(S2_FAST, target);
          tSend = tPoll = millis();
          flashBtn = i;
          flashUntil = millis() + 300;
          drawButton(i);
          redraw = true;
          break;
        }
      }
    }
  } else {
    touchDown = false;
  }
  if (flashBtn >= 0 && millis() > flashUntil) {
    int i = flashBtn;
    flashBtn = -1;
    drawButton(i);
  }

  if (targetDirty && millis() - tSend > SEND_GAP) {
    sendCmd(S2_SERVO, target);
    targetDirty = false;
    tSend = tPoll = millis();
  }
  if (millis() - tPoll > POLL_MS) {
    sendCmd(S2_POLL, 0);
    tPoll = millis();
  }
  while (receiveState()) redraw = true;

  // Werte hoechstens alle 100 ms auffrischen, sonst verpasst readEncoder Rasten;
  // einmal je Sekunde auch ohne Paket (Online-Anzeige faellt auf "--")
  if ((redraw && millis() - tDraw > 100) || millis() - tDraw > 1000) {
    drawValues();
    tDraw = millis();
    redraw = false;
  }
}
