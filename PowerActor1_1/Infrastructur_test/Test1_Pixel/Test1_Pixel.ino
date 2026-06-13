#include <FastLED.h>

#define pixelPin 25
#define pixelCount 2
CRGB leds[pixelCount];

#define water 12
#define extPower
void setup() {
  FastLED.addLeds<WS2812, pixelPin, RGB>(leds, pixelCount);  // GRB ordering is typical
  FastLED.setBrightness(90);
  for (int i=0; i < pixelCount; i++) {
    leds[i] = 0xFF0000;
    FastLED.show();
  }


}

void loop() {
  for (int i=0; i < pixelCount; i++) {
    leds[i] = 0xFFFFFF;
    FastLED.show();
    delay(1000);
    leds[i] = 0x000000;
    FastLED.show();
    delay(1000);
  }
}
