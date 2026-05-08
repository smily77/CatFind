/*
 * RotaryLine8 — M5Stack Tab5 + Unit 8Encoder
 *
 * Zeigt eine rote Linie von der Bildschirmmitte,
 * die sich per Encoder (Knopf 0) drehen lässt.
 *
 * Hardware:
 *   Board   : M5Stack Tab5 (ESP32-P4)
 *   FQBN    : m5stack:esp32:m5stack_tab5
 *   Display : DSI 720x1280 (Querformat: 1280x720)
 *   Encoder : M5Stack Unit 8Encoder an Port A
 *
 * Wichtige Erkenntnisse für künftige Projekte:
 *   - Display ist ein Hardware-Framebuffer (DSI/DMA2D) → kein Sprite nötig,
 *     direkt zeichnen reicht und flimmert nicht.
 *   - LGFX_Sprite schlägt auf Tab5 fehl (zu wenig interner RAM für 1280x720),
 *     falls doch ein Sprite benötigt wird: in PSRAM allokieren.
 *   - Auf ESP32-P4 gibt es nur Wire (kein Wire1 im m5stack-Paket).
 *   - Wire muss NACH display.init() gestartet werden, sonst Konflikt
 *     mit dem internen I2C des Displays (GPIO 31/32).
 *   - Port A I2C-Pins: SDA=53, SCL=54 (Standard-Wire des Tab5).
 *   - Unit 8Encoder I2C-Adresse: 0x41 (Unit Encoder = 0x40).
 *   - getEncoderValue(index) gibt uint32_t zurück → als int32_t casten.
 *   - Encoder-Index 0–7, Knopf 0 = index 0.
 *   - M5GFX >= 0.2.20 erforderlich (DSI-Bugfix für Tab5).
 *   - Bibliotheken: M5GFX, M5UNIT_8Encoder
 */

#include <Wire.h>
#include <M5GFX.h>
#include <UNIT_8ENCODER.h>

M5GFX       display;
UNIT_8ENCODER encoder;

float    angle_deg = 90.0f;   // Aktuelle Linienposition in Grad
int32_t  prev_enc  = 0;       // Letzter Encoder-Wert (für Delta-Berechnung)
int      old_x2, old_y2;      // Endpunkt der zuletzt gezeichneten Linie
int      cx, cy, len;         // Bildschirmmitte und Linienlänge

// Grad pro Encoder-Schritt (erhöhen = gröbere Schritte)
static const float DEG_PER_STEP = 3.0f;

// Welcher der 8 Encoder-Knöpfe gesteuert wird (0–7)
static const uint8_t ENCODER_INDEX = 0;

void setup() {
  // Display initialisieren (muss vor Wire.begin() kommen)
  display.init();
  if (display.width() < display.height()) {
    display.setRotation(display.getRotation() ^ 1); // Querformat erzwingen
  }
  display.setBrightness(200); // 0–255
  display.fillScreen(TFT_BLACK);

  cx  = display.width()  / 2;
  cy  = display.height() / 2;
  len = min(cx, cy) - 40; // Linie etwas kürzer als Hälfte des kleineren Maßes

  // Startposition der Linie berechnen
  float rad = angle_deg * (M_PI / 180.0f);
  old_x2 = cx + (int)(len * cosf(rad));
  old_y2 = cy - (int)(len * sinf(rad)); // y-Achse am Bildschirm ist invertiert

  // I2C für Port A starten (nach display.init()!)
  Wire.begin(53, 54, 100000); // SDA=53, SCL=54, 100 kHz
  encoder.begin(&Wire, ENCODER_ADDR, 53, 54, 100000);

  // Startwert merken → kein Sprung beim ersten Frame
  prev_enc = (int32_t)encoder.getEncoderValue(ENCODER_INDEX);
}

void loop() {
  int32_t enc   = (int32_t)encoder.getEncoderValue(ENCODER_INDEX);
  int32_t delta = enc - prev_enc;

  if (delta != 0) {
    angle_deg -= delta * DEG_PER_STEP; // Minus = Drehrichtung korrigiert
    prev_enc   = enc;
    // Winkel im Bereich [0, 360) halten
    while (angle_deg >= 360.0f) angle_deg -= 360.0f;
    while (angle_deg <    0.0f) angle_deg += 360.0f;
  }

  // Neuen Endpunkt berechnen
  float rad = angle_deg * (M_PI / 180.0f);
  int x2 = cx + (int)(len * cosf(rad));
  int y2 = cy - (int)(len * sinf(rad));

  // Alte Linie schwarz überschreiben (nur geänderte Pixel → kein Flimmern)
  display.drawLine(cx,   cy,   old_x2,   old_y2,   TFT_BLACK);
  display.drawLine(cx+1, cy,   old_x2+1, old_y2,   TFT_BLACK);
  display.drawLine(cx,   cy+1, old_x2,   old_y2+1, TFT_BLACK);

  // Neue rote Linie (3 px breit)
  display.drawLine(cx,   cy,   x2,   y2,   TFT_RED);
  display.drawLine(cx+1, cy,   x2+1, y2,   TFT_RED);
  display.drawLine(cx,   cy+1, x2,   y2+1, TFT_RED);

  // Mittelpunkt als Kreis
  display.fillCircle(cx, cy, 8, TFT_RED);

  // Kontrolltext (Hintergrund zuerst löschen)
  display.fillRect(0, 0, 420, 60, TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.printf("Winkel: %.1f Grad", angle_deg);
  display.setCursor(10, 35);
  display.printf("Encoder[%d]: %d", ENCODER_INDEX, enc);

  old_x2 = x2;
  old_y2 = y2;

  delay(10);
}
