/*
 * ServoTest - SA-L Test 2 (CYD + Encoder + Servo)
 *
 * Wie EncoderZeiger, der Winkel steuert zusaetzlich den Pan-Servo an SERVO_PIN.
 * Puls = SERVO_MID_US + Winkel * US_PER_DEG (Annahme 300-Grad-Servo:
 * 500..2500 us = 300 Grad -> 6,67 us/Grad). Zuerst nur +/-90 Grad (900..2100 us),
 * das vertraegt jeder Servo; erweitern, wenn der Drehbereich geklaert ist.
 *
 * Servo separat mit 5 V speisen, GND mit dem Display verbinden.
 *
 * Encoder-Anschluss wird beim Start erkannt:
 *  - PCF8574 am I2C (extSDA/extSCL aus CYD_Display_Config.h, Adresse 0x20..0x27
 *    oder 0x38..0x3F): Die ersten zwei Bits, die sich bewegen, werden A/B.
 *  - sonst direkt an GPIO extSCL/extSDA (wie EPF1_Stell: 22/27 am CYD28).
 *
 * Rasten langsam = 1 Grad, schnell gedreht = 5 Grad.
 * Serielle Ausgabe 115200 Baud fuer die Diagnose.
 */

#include <CYD_Display_Config.h>
#include <Wire.h>

LGFX display;
LGFX_Sprite dial(&display);

// ── Einstellungen ────────────────────────────────────────────────────────────
#define ANG_MIN    -90
#define ANG_MAX     90
#define SERVO_PIN    7     // extTxD am Gen24Inch-Housing
#define SERVO_MID_US 1500
#define US_PER_DEG   (2000.0f / 300.0f)

static uint32_t servoUs(int ang) { return SERVO_MID_US + lroundf(ang * US_PER_DEG); }
static void servoWrite(int ang) {
  // 50 Hz, 14 Bit: 20000 us = 16384 Schritte
  ledcWrite(SERVO_PIN, (uint32_t)((uint64_t)servoUs(ang) * 16384 / 20000));
}
#define STEP_FINE    1
#define STEP_FAST    5
#define FAST_MS     60     // Rasten dichter als das = schnell

// ── Encoder-Zustand (Task auf Kern 0) ────────────────────────────────────────
enum EncMode { ENC_GPIO, ENC_PCF };
static EncMode encMode = ENC_GPIO;
static uint8_t pcfAddr = 0;
static int pinA = extSCL, pinB = extSDA;
static int bitA = -1, bitB = -1;            // PCF-Bits, werden gelernt
static volatile uint8_t lastRaw = 0xFF;     // PCF-Rohwert fuer die Anzeige
static volatile int detents = 0;            // gezaehlte Rasten (vorzeichenbehaftet)
static volatile uint32_t encErrors = 0;

static bool pcfRead(uint8_t &v) {
  if (Wire.requestFrom(pcfAddr, (uint8_t)1) != 1) { encErrors++; return false; }
  v = Wire.read();
  return true;
}

static void encoderTask(void *) {
  static const int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  uint8_t oldAB = 3;
  int8_t encval = 0;
  uint8_t startRaw = 0xFF;
  if (encMode == ENC_PCF) { pcfRead(startRaw); lastRaw = startRaw; }

  for (;;) {
    uint8_t a = 1, b = 1;
    if (encMode == ENC_GPIO) {
      a = digitalRead(pinA);
      b = digitalRead(pinB);
    } else {
      uint8_t v;
      if (!pcfRead(v)) { vTaskDelay(5); continue; }
      lastRaw = v;
      // A/B lernen: die ersten zwei Bits, die vom Startwert abweichen
      if (bitB < 0) {
        uint8_t diff = v ^ startRaw;
        for (int i = 0; i < 8 && diff; i++) {
          if (!(diff & (1 << i)) || i == bitA) continue;
          if (bitA < 0) bitA = i; else if (bitB < 0) bitB = i;
        }
        if (bitB < 0) { vTaskDelay(1); continue; }
        if (bitA > bitB) { int t = bitA; bitA = bitB; bitB = t; }
        Serial.printf("PCF: Encoder A=P%d B=P%d\n", bitA, bitB);
        oldAB = (((v >> bitA) & 1) << 1) | ((v >> bitB) & 1);
        continue;
      }
      a = (v >> bitA) & 1;
      b = (v >> bitB) & 1;
    }
    oldAB = (oldAB << 2) | (a << 1) | b;
    encval += enc_states[oldAB & 0x0f];
    if (encval > 3)       { detents++; encval = 0; }
    else if (encval < -3) { detents--; encval = 0; }
    vTaskDelay(1);   // 1-kHz-Abtastung reicht fuer einen Handencoder
  }
}

static bool detectPcf() {
  Wire.begin(extSDA, extSCL, 400000);
  const uint8_t ranges[2][2] = {{0x20, 0x27}, {0x38, 0x3F}};
  for (auto &r : ranges)
    for (uint8_t a = r[0]; a <= r[1]; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        pcfAddr = a;
        Wire.beginTransmission(a);  // alle Pins als Eingang (High schreiben)
        Wire.write(0xFF);
        Wire.endTransmission();
        return true;
      }
    }
  Wire.end();
  return false;
}

// ── Anzeige ──────────────────────────────────────────────────────────────────
static int W, H, D, R;
static int angle = 0;

static void drawDial() {
  dial.fillScreen(TFT_BLACK);
  int c = D / 2;
  dial.drawCircle(c, c, R, TFT_DARKGREY);
  // Skala -150..+150, 0 Grad zeigt nach oben
  for (int a = ANG_MIN; a <= ANG_MAX; a += 10) {
    float rad = (a - 90) * DEG_TO_RAD;
    int len = (a % 50 == 0) ? 14 : 6;
    int x1 = c + cosf(rad) * R,         y1 = c + sinf(rad) * R;
    int x2 = c + cosf(rad) * (R - len), y2 = c + sinf(rad) * (R - len);
    dial.drawLine(x1, y1, x2, y2, a == 0 ? TFT_GREEN : TFT_LIGHTGREY);
  }
  // Zeiger
  float rad = (angle - 90) * DEG_TO_RAD;
  int px = c + cosf(rad) * (R - 18), py = c + sinf(rad) * (R - 18);
  dial.drawWideLine(c, c, px, py, 4, TFT_ORANGE);
  dial.fillCircle(c, c, 7, TFT_ORANGE);
  // Winkel in der Mitte unten
  char buf[16];
  snprintf(buf, sizeof(buf), "%+d\xB0", angle);
  dial.setTextDatum(MC_DATUM);
  dial.setTextColor(TFT_WHITE);
  dial.setFont(&fonts::Font4);
  dial.drawString(buf, c, c + R / 2);
  snprintf(buf, sizeof(buf), "%lu us", (unsigned long)servoUs(angle));
  dial.setFont(&fonts::Font2);
  dial.setTextColor(TFT_LIGHTGREY);
  dial.drawString(buf, c, c + R / 2 + 22);
  dial.pushSprite((W - D) / 2, H - D);
}

static void drawHeader() {
  display.fillRect(0, 0, W, H - D, TFT_BLACK);
  display.setTextDatum(TL_DATUM);
  display.setFont(&fonts::Font2);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawString("SA-L Test: Servo (GPIO " + String(SERVO_PIN) + ")", 4, 2);
  char buf[64];
  if (encMode == ENC_GPIO)
    snprintf(buf, sizeof(buf), "Encoder GPIO A=%d B=%d   ", pinA, pinB);
  else if (bitB < 0)
    snprintf(buf, sizeof(buf), "PCF 0x%02X  roh %02X  drehen...   ", pcfAddr, lastRaw);
  else
    snprintf(buf, sizeof(buf), "PCF 0x%02X A=P%d B=P%d roh %02X   ", pcfAddr, bitA, bitB, lastRaw);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString(buf, 4, 20);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  display.init();
  display.setRotation(display.width() > display.height() ? 0 : 1);  // quer
  W = display.width();
  H = display.height();
  display.fillScreen(TFT_BLACK);

  if (detectPcf()) {
    encMode = ENC_PCF;
    Serial.printf("PCF8574 gefunden auf 0x%02X (SDA=%d SCL=%d)\n", pcfAddr, extSDA, extSCL);
  } else {
    encMode = ENC_GPIO;
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
    Serial.printf("Kein PCF8574, Encoder direkt GPIO A=%d B=%d\n", pinA, pinB);
  }

  ledcAttach(SERVO_PIN, 50, 14);
  servoWrite(0);
  Serial.printf("Servo an GPIO %d, Mitte %d us\n", SERVO_PIN, SERVO_MID_US);

  D = min(W, H - 40);
  R = D / 2 - 4;
  dial.setColorDepth(8);
  if (!dial.createSprite(D, D)) Serial.println("Sprite zu gross!");

  drawHeader();
  drawDial();
  xTaskCreatePinnedToCore(encoderTask, "enc", 4096, nullptr, 2, nullptr, 0);
}

void loop() {
  static int lastDet = 0;
  static uint32_t lastStepMs = 0, lastHdrMs = 0;
  int det = detents;
  if (det != lastDet) {
    uint32_t now = millis();
    int step = (now - lastStepMs < FAST_MS) ? STEP_FAST : STEP_FINE;
    lastStepMs = now;
    angle = constrain(angle + (det - lastDet) * step, ANG_MIN, ANG_MAX);
    lastDet = det;
    servoWrite(angle);
    Serial.printf("Rasten %d  Winkel %+d  %lu us\n", det, angle, (unsigned long)servoUs(angle));
    drawDial();
  }
  if (millis() - lastHdrMs > 300) {   // Kopfzeile: Modus + PCF-Rohwert
    lastHdrMs = millis();
    drawHeader();
  }
  delay(10);
}
