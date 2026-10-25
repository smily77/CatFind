// =============================================================================
//  Step5_Display - Display-/Encoder-Seite zu Step5_RelaysIR (PowerActor2)
//  Basis: Step4_Display (Servo-Bedienung, Lidar-/Laser-Anzeige unveraendert)
//         + Relais-Knoepfe + Bild der IR-Positionskamera (bis 4 Punkte).
//
//  Bedienung:
//    Drehknopf          : faehrt den Servo direkt (ENC_STEP Ticks je Raste)
//    Touch << LINKS     : so schnell wie moeglich an den linken Rand (0 = -180 Grad)
//    Touch MITTE        : so schnell wie moeglich in die Mitte (2048 = 0 Grad)
//    Touch RECHTS >>    : so schnell wie moeglich an den rechten Rand (4095 = +180 Grad)
//    Touch Ventil/LD06/Radar/Spot : Relais umschalten (gruen = EIN)
//  Anzeige: Servo Soll/Ist/Temperatur, Lidar-Distanz (bzw. Fehler), Laser EIN/AUS,
//  rechts oben das IR-Kamerabild (1024 x 768 auf 128 x 96 verkleinert) mit den
//  Punkten 1..4 (rot/gruen/cyan/gelb), unten deren Koordinaten.
//  Der Laser schaltet der PowerActor2 selbst (< 0,5 m), das Display zeigt ihn nur.
//  Die Relais gehen am PowerActor2 aus, wenn das Display 3 s nichts sendet.
//
//  Kein Flackern: der feste Teil (Titel, Knoepfe, Rahmen) wird EINMAL
//  gezeichnet; danach werden nur geaenderte Werte ueberschrieben - Text mit
//  Hintergrundfarbe auf fester Breite, IR-Punkte nur bei Bewegung neu.
//
//  Touch: XPT2046 des CYD28, von LGFX_AUTODETECT eingerichtet (wie
//  Displays/Bedienung_Einstellungen), nur die Druck-Flanke loest aus.
//  Protokoll: eigenes Test-Protokoll, identisch in Step5_RelaysIR (kein xCom).
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

// ── Protokoll (identisch in Step5_RelaysIR) ──────────────────────────────────

#define PA2_IP            IPAddress(192, 168, 0, 181)
#define STEP2_CMD_PORT    4310
#define STEP2_STATE_PORT  4311
#define STEP5_MAGIC       0x55
#define SERVO_MIN         0
#define SERVO_MAX         4095
#define SERVO_CENTER      2048
#define LIDAR_NONE        0xFF
#define IR_NONE           1023

enum : uint8_t { S2_POLL = 0, S2_SERVO = 1, S2_RELAY = 2, S2_FAST = 3 };

struct __attribute__((packed)) Step2Cmd {
  uint8_t  magic;
  uint8_t  type;
  uint16_t value;
};

struct __attribute__((packed)) Step5State {
  uint8_t  magic;
  uint8_t  servoOk;
  uint16_t servoGoal;
  uint16_t servoPos;
  uint8_t  tempC;
  uint8_t  lidarErr;
  uint32_t lidarMm;
  uint8_t  laserOn;
  uint8_t  relays;
  uint8_t  irOk;
  uint16_t irX[4];
  uint16_t irY[4];
  uint8_t  irS[4];
};

WiFiUDP udp;
Step5State st = {};
bool     haveState = false;
uint32_t lastRx    = 0;

// ── Bedienzustand ────────────────────────────────────────────────────────────

int  target    = SERVO_CENTER; // Servo-Soll am Display
bool targetDirty = false;
bool touchDown = false;       // Entprellung: nur Druck-Flanke feuert
int  flashBtn  = -1;          // zuletzt gedrueckter Servo-Knopf (kurz hell)
uint32_t flashUntil = 0;

// Ein Anzeigefeld: schreibt nur, wenn sich Text oder Farbe geaendert haben,
// und fuellt immer auf die volle Feldbreite (alte, laengere Texte verschwinden
// ohne vorheriges Loeschen).
struct Field {
  int x, y, chars, size;
  char last[56];
  uint16_t lastFg, lastBg;
};

// Servo-Knoepfe: nebeneinander, gleiche Breite
struct FastBtn { const char *text; uint16_t pos; };
const FastBtn BTNS[3] = {{"<< LINKS", SERVO_MIN}, {"MITTE", SERVO_CENTER}, {"RECHTS >>", SERVO_MAX}};
#define BTN_Y   136
#define BTN_H   40
#define BTN_W   100
#define BTN_GAP 7
int btnX(int i) { return BTN_GAP + i * (BTN_W + BTN_GAP); }

// Relais-Knoepfe (Reihenfolge = Relais 1..4 am PowerActor2)
const char *RELAY_TEXT[4] = {"Ventil", "LD06", "Radar", "Spot"};
#define REL_Y   182
#define REL_H   40
#define REL_W   74
#define REL_GAP 5
int relX(int i) { return REL_GAP + i * (REL_W + REL_GAP); }
int8_t relDrawn[4] = {-1, -1, -1, -1};   // gezeichneter Zustand: -1 neu, 0 aus, 1 ein, 2 offline

// IR-Bild: 1024 x 768 -> 128 x 96 (Faktor 8)
#define IRV_X  186
#define IRV_Y  26
#define IRV_W  128
#define IRV_H  96
const uint16_t IR_COL[4] = {TFT_RED, TFT_GREEN, TFT_CYAN, TFT_YELLOW};
int irPx[4] = {-1, -1, -1, -1}, irPy[4], irPr[4];   // gezeichnete Punkte (-1 = keiner)


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
  Step2Cmd c = {STEP5_MAGIC, type, value};
  udp.beginPacket(PA2_IP, STEP2_CMD_PORT);
  udp.write((const uint8_t *)&c, sizeof(c));
  udp.endPacket();
}

bool receiveState() {
  int len = udp.parsePacket();
  if (len <= 0) return false;
  Step5State s;
  if (len != sizeof(s)) { udp.flush(); return false; }
  udp.read((uint8_t *)&s, sizeof(s));
  if (s.magic != STEP5_MAGIC) return false;
  if (!haveState) target = s.servoGoal;   // beim ersten Kontakt Soll uebernehmen
  st = s;
  haveState = true;
  lastRx = millis();
  return true;
}

bool online() { return haveState && millis() - lastRx < 2000; }

// ── Anzeige ──────────────────────────────────────────────────────────────────

float ticksToDeg(int t) { return (t - 2048) * 360.0f / 4096.0f; }

void drawField(Field &f, uint16_t fg, uint16_t bg, const char *fmt, ...) {
  char buf[56];
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

Field fOnline = {290, 4,   2, 2, "", 0, 0};
Field fSoll   = {6,   28, 14, 2, "", 0, 0};
Field fIst    = {6,   48, 14, 2, "", 0, 0};
Field fLidar  = {6,   70,  9, 3, "", 0, 0};
Field fLaser  = {6,  100,  7, 2, "", 0, 0};
Field fTemp   = {110, 100, 5, 2, "", 0, 0};
Field fLidInfo= {6,  122, 29, 1, "", 0, 0};
Field fIrInfo = {IRV_X, IRV_Y + IRV_H + 3, 21, 1, "", 0, 0};
Field fIrPt[4] = {{2, 230, 13, 1, "", 0, 0}, {82, 230, 13, 1, "", 0, 0},
                  {162, 230, 13, 1, "", 0, 0}, {242, 230, 13, 1, "", 0, 0}};

void drawButton(int i) {
  uint16_t bg = (i == flashBtn) ? TFT_YELLOW : TFT_DARKGREY;
  int x = btnX(i);
  gfx.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 8, bg);
  gfx.setTextSize(2);
  gfx.setTextColor(TFT_BLACK, bg);
  gfx.setCursor(x + (BTN_W - (int)strlen(BTNS[i].text) * 12) / 2, BTN_Y + BTN_H / 2 - 8);
  gfx.print(BTNS[i].text);
}

// Relais-Knopf nach dem Zustand, den der PowerActor2 meldet
void drawRelay(int i) {
  int8_t s = !online() ? 2 : ((st.relays >> i) & 1);
  if (s == relDrawn[i]) return;
  relDrawn[i] = s;
  uint16_t bg = s == 1 ? TFT_GREEN : TFT_DARKGREY;
  uint16_t fg = s == 2 ? TFT_LIGHTGREY : TFT_BLACK;
  int x = relX(i);
  gfx.fillRoundRect(x, REL_Y, REL_W, REL_H, 8, bg);
  gfx.setTextSize(2);
  gfx.setTextColor(fg, bg);
  gfx.setCursor(x + (REL_W - (int)strlen(RELAY_TEXT[i]) * 12) / 2, REL_Y + REL_H / 2 - 8);
  gfx.print(RELAY_TEXT[i]);
}

void drawIrGrid() {   // Mittelkreuz als Orientierung
  gfx.drawFastHLine(IRV_X + IRV_W / 2 - 4, IRV_Y + IRV_H / 2, 9, TFT_DARKGREY);
  gfx.drawFastVLine(IRV_X + IRV_W / 2, IRV_Y + IRV_H / 2 - 4, 9, TFT_DARKGREY);
}

void drawStatic() {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextSize(2);
  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setCursor(6, 4);
  gfx.print("PowerActor2 Step5");
  for (int i = 0; i < 3; i++) drawButton(i);
  for (int i = 0; i < 4; i++) { relDrawn[i] = -1; drawRelay(i); }
  gfx.drawRect(IRV_X - 1, IRV_Y - 1, IRV_W + 2, IRV_H + 2, TFT_DARKGREY);
  drawIrGrid();
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

// IR-Punkte: nur wenn sich einer bewegt hat, alle alten loeschen und alle
// neuen zeichnen (so bleibt bei Ueberlappung keiner angeknabbert)
void drawIr(bool on) {
  int nx[4], ny[4], nr[4];
  bool changed = false;
  for (int i = 0; i < 4; i++) {
    bool valid = on && st.irOk && st.irX[i] < IR_NONE && st.irY[i] < 768;
    nx[i] = valid ? IRV_X + st.irX[i] * IRV_W / 1024 : -1;
    ny[i] = valid ? IRV_Y + st.irY[i] * IRV_H / 768 : 0;
    nr[i] = valid ? 2 + min((int)st.irS[i], 12) / 3 : 0;
    if (nx[i] != irPx[i] || ny[i] != irPy[i] || nr[i] != irPr[i]) changed = true;
  }
  if (changed) {
    gfx.setClipRect(IRV_X, IRV_Y, IRV_W, IRV_H);   // Rahmen bleibt heil
    for (int i = 0; i < 4; i++)
      if (irPx[i] >= 0) gfx.fillCircle(irPx[i], irPy[i], irPr[i], TFT_BLACK);
    drawIrGrid();
    for (int i = 0; i < 4; i++) {
      irPx[i] = nx[i]; irPy[i] = ny[i]; irPr[i] = nr[i];
      if (nx[i] >= 0) gfx.fillCircle(nx[i], ny[i], nr[i], IR_COL[i]);
    }
    gfx.clearClipRect();
  }

  int n = 0;
  for (int i = 0; i < 4; i++) {
    if (nx[i] >= 0) {
      n++;
      drawField(fIrPt[i], IR_COL[i], TFT_BLACK, "%d:%u,%u s%u", i + 1, st.irX[i], st.irY[i], st.irS[i]);
    } else {
      drawField(fIrPt[i], TFT_DARKGREY, TFT_BLACK, "%d:--", i + 1);
    }
  }
  if (!on)            drawField(fIrInfo, TFT_DARKGREY, TFT_BLACK, "");
  else if (!st.irOk)  drawField(fIrInfo, TFT_ORANGE, TFT_BLACK, "IR-Kamera fehlt");
  else                drawField(fIrInfo, TFT_LIGHTGREY, TFT_BLACK, "IR: %d Punkt%s", n, n == 1 ? "" : "e");
}

void drawValues() {
  bool on = online();
  drawField(fOnline, on ? TFT_GREEN : TFT_RED, TFT_BLACK, on ? "ok" : "--");
  drawField(fSoll, TFT_YELLOW, TFT_BLACK, "S %4d %6.1f", target, ticksToDeg(target));

  if (on && st.servoOk) {
    drawField(fIst, TFT_LIGHTGREY, TFT_BLACK, "I %4u %6.1f", st.servoPos, ticksToDeg(st.servoPos));
    drawField(fTemp, TFT_LIGHTGREY, TFT_BLACK, "%uC", st.tempC);
  } else {
    drawField(fIst, TFT_LIGHTGREY, TFT_BLACK, on ? "Servo fehlt" : "warte auf PA2");
    drawField(fTemp, TFT_LIGHTGREY, TFT_BLACK, "");
  }

  if (!on || st.lidarErr == LIDAR_NONE) {
    drawField(fLidar, TFT_DARKGREY, TFT_BLACK, "--.--- m");
    drawField(fLidInfo, TFT_DARKGREY, TFT_BLACK, on ? "Lidar: keine Messung" : "");
  } else if (st.lidarErr != 0) {
    drawField(fLidar, TFT_ORANGE, TFT_BLACK, "--.--- m");
    drawField(fLidInfo, TFT_ORANGE, TFT_BLACK, "Lidar: %s", lidarErrText(st.lidarErr));
  } else {
    drawField(fLidar, st.lidarMm < 500 ? TFT_RED : TFT_WHITE, TFT_BLACK,
              "%2lu.%03lu m", (unsigned long)(st.lidarMm / 1000), (unsigned long)(st.lidarMm % 1000));
    drawField(fLidInfo, TFT_DARKGREY, TFT_BLACK, "Lidar ok - Laser EIN < 0,5 m");
  }

  bool laser = on && st.laserOn;
  drawField(fLaser, TFT_BLACK, laser ? TFT_RED : TFT_DARKGREY, laser ? " LASER " : " Laser ");

  for (int i = 0; i < 4; i++) drawRelay(i);
  drawIr(on);
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
  Serial.printf("Step5_Display ready, IP %s\n", WiFi.localIP().toString().c_str());
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
      for (int i = 0; i < 4; i++) {
        if (online() && (int)tx >= relX(i) && (int)tx < relX(i) + REL_W &&
            (int)ty >= REL_Y && (int)ty < REL_Y + REL_H) {
          bool on = !((st.relays >> i) & 1);         // umschalten
          sendCmd(S2_RELAY, (i << 1) | on);          // Antwort = neuer Zustand
          tPoll = millis();
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
