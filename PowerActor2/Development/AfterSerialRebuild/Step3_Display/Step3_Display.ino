// =============================================================================
//  Step3_Display - Display-/Encoder-Seite zu Step3_ServoFull (PowerActor2)
//  Basis: Step2_Display, Relais-Zeilen ersetzt durch zwei Schnellfahrt-Knoepfe.
//
//  Bedienung (Drehknopf + Taste):
//    Drehen             : Auswahl  Servo / << LINKS / RECHTS >>
//    Taste auf Servo    : Servo-Modus EIN -> Drehen faehrt den Servo
//                         (ENC_STEP Ticks je Raste), Taste nochmals = zurueck
//    Taste auf LINKS    : so schnell wie moeglich an den linken Rand (0 = -180 Grad)
//    Taste auf RECHTS   : so schnell wie moeglich an den rechten Rand (4095 = +180 Grad)
//  Das Display schickt die Kommandos per UDP an den PowerActor2 und fragt alle
//  POLL_MS den Zustand ab (Soll/Ist-Position, Temperatur).
//
//  Protokoll: eigenes Test-Protokoll, identisch in Step3_ServoFull (kein xCom).
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
#define taste 35

#define ENC_STEP  16          // Servo-Ticks je Raste (~1,4 Grad)
#define POLL_MS   300
#define SEND_GAP  60          // ms: Servo-Ziel hoechstens so oft senden

// ── Protokoll (identisch in Step3_ServoFull) ─────────────────────────────────

#define PA2_IP            IPAddress(192, 168, 0, 181)
#define STEP2_CMD_PORT    4310
#define STEP2_STATE_PORT  4311
#define STEP2_MAGIC       0x53
#define SERVO_MIN         0
#define SERVO_MAX         4095

enum : uint8_t { S2_POLL = 0, S2_SERVO = 1, S2_FAST = 3 };

struct __attribute__((packed)) Step2Cmd {
  uint8_t  magic;
  uint8_t  type;
  uint16_t value;
};

struct __attribute__((packed)) Step2State {
  uint8_t  magic;
  uint8_t  relays;
  uint16_t servoGoal;
  uint16_t servoPos;
  uint8_t  servoOk;
  uint8_t  tempC;
};

WiFiUDP udp;
Step2State st = {};
bool     haveState = false;
uint32_t lastRx    = 0;

// ── Bedienzustand ────────────────────────────────────────────────────────────

enum { SEL_SERVO = 0, SEL_LEFT = 1, SEL_RIGHT = 2, SEL_COUNT = 3 };
int  sel       = SEL_SERVO;
bool servoEdit = false;       // Drehknopf faehrt den Servo
int  target    = 2048;        // Servo-Soll am Display
bool targetDirty = false;

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

// Taste gedrueckt? (entprellt, aktiv LOW)
bool tastePressed() {
  static uint32_t tLast = 0;
  if (!digitalRead(taste) && millis() - tLast > 250) {
    while (!digitalRead(taste)) delay(5);
    tLast = millis();
    return true;
  }
  return false;
}

// ── Senden / Empfangen ───────────────────────────────────────────────────────

void sendCmd(uint8_t type, uint16_t value) {
  Step2Cmd c = {STEP2_MAGIC, type, value};
  udp.beginPacket(PA2_IP, STEP2_CMD_PORT);
  udp.write((const uint8_t *)&c, sizeof(c));
  udp.endPacket();
}

bool receiveState() {
  int len = udp.parsePacket();
  if (len <= 0) return false;
  Step2State s;
  if (len != sizeof(s)) { udp.flush(); return false; }
  udp.read((uint8_t *)&s, sizeof(s));
  if (s.magic != STEP2_MAGIC) return false;
  if (!haveState) target = s.servoGoal;   // beim ersten Kontakt Soll uebernehmen
  st = s;
  haveState = true;
  lastRx = millis();
  return true;
}

// ── Anzeige ──────────────────────────────────────────────────────────────────

float ticksToDeg(int t) { return (t - 2048) * 360.0f / 4096.0f; }

void drawButton(int x, int y, int w, const char *text, bool selected) {
  uint16_t bg = selected ? TFT_YELLOW : TFT_DARKGREY;
  gfx.fillRoundRect(x, y, w, 50, 8, bg);
  gfx.setTextColor(TFT_BLACK, bg);
  gfx.setCursor(x + (w - (int)strlen(text) * 12) / 2, y + 17);
  gfx.print(text);
}

void drawScreen() {
  gfx.startWrite();
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextSize(2);
  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setCursor(6, 6);
  gfx.print("PowerActor2 Step3");

  bool online = haveState && millis() - lastRx < 2000;
  gfx.setCursor(250, 6);
  gfx.setTextColor(online ? TFT_GREEN : TFT_RED, TFT_BLACK);
  gfx.print(online ? "ok" : "--");

  // Zeile Servo
  int y = 40;
  gfx.setTextColor(sel == SEL_SERVO ? (servoEdit ? TFT_BLACK : TFT_YELLOW) : TFT_WHITE,
                   sel == SEL_SERVO && servoEdit ? TFT_YELLOW : TFT_BLACK);
  if (sel == SEL_SERVO && servoEdit) gfx.fillRect(0, y - 4, 320, 24, TFT_YELLOW);
  gfx.setCursor(6, y);
  gfx.printf("%sServo %4d %6.1f", sel == SEL_SERVO ? ">" : " ", target, ticksToDeg(target));

  gfx.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  gfx.setCursor(6, y + 26);
  if (online && st.servoOk)
    gfx.printf("  Ist  %4u %6.1f  %uC", st.servoPos, ticksToDeg(st.servoPos), st.tempC);
  else if (online)
    gfx.print("  Servo antwortet nicht");
  else
    gfx.print("  warte auf PowerActor2");

  // Schnellfahrt-Knoepfe
  drawButton(6,   120, 148, "<< LINKS",  sel == SEL_LEFT);
  drawButton(166, 120, 148, "RECHTS >>", sel == SEL_RIGHT);
  gfx.setTextSize(1);
  gfx.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  gfx.setCursor(6, 178);
  gfx.print("so schnell wie moeglich an den Rand (-180 / +180 Grad)");

  gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
  gfx.setCursor(6, 228);
  gfx.print(servoEdit ? "Drehen = Servo fahren, Taste = fertig"
                      : "Drehen = Auswahl, Taste = ausloesen / Servo-Modus");
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
  pinMode(taste, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  udp.begin(STEP2_STATE_PORT);
  Serial.printf("Step3_Display ready, IP %s\n", WiFi.localIP().toString().c_str());
  sendCmd(S2_POLL, 0);
  drawScreen();
}

void loop() {
  static uint32_t tPoll = 0, tSend = 0, tDraw = 0;
  static bool redraw = false;

  int v = servoEdit ? target : sel;
  if (readEncoder(v, servoEdit ? ENC_STEP : 1)) {
    if (servoEdit) {
      target = constrain(v, SERVO_MIN, SERVO_MAX);
      targetDirty = true;
    } else {
      sel = (v % SEL_COUNT + SEL_COUNT) % SEL_COUNT;
    }
    redraw = true;
  }

  if (tastePressed()) {
    if (sel == SEL_SERVO) {
      servoEdit = !servoEdit;
    } else {
      target = sel == SEL_LEFT ? SERVO_MIN : SERVO_MAX;
      targetDirty = false;
      sendCmd(S2_FAST, target);
      tSend = tPoll = millis();
    }
    redraw = true;
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

  // Neuzeichnen hoechstens alle 100 ms, sonst verpasst readEncoder Rasten
  if (redraw && millis() - tDraw > 100) {
    drawScreen();
    tDraw = millis();
    redraw = false;
  }
}
