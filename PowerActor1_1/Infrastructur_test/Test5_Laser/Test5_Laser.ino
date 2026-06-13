#include <FastLED.h>

#define pixelPin 25
#define pixelCount 2
CRGB leds[pixelCount];

#define water 12
#define extPower 13
#define wirelessPower 14
#define taster 33
#define reed1  26
#define reed2  32
#define laser 18
void setup() {
  pinMode(water,OUTPUT);
  digitalWrite(water,HIGH);
  pinMode(extPower,OUTPUT);
  digitalWrite(extPower,HIGH);
  pinMode(wirelessPower,OUTPUT);
  digitalWrite(wirelessPower,HIGH);
  pinMode(taster,INPUT_PULLUP);
  pinMode(reed1,INPUT_PULLUP);
  pinMode(reed2,INPUT_PULLUP);
  pinMode(laser,OUTPUT);
  FastLED.addLeds<WS2812, pixelPin, RGB>(leds, pixelCount);  // GRB ordering is typical
  FastLED.setBrightness(90);


}

void loop() {
  if (digitalRead(taster)) {
    leds[0] = 0xFF0000;
    leds[1] = 0xFF0000;
    digitalWrite(laser,HIGH);
  }
  else {
    leds[0] = 0x000000;
    leds[1] = 0x000000;
    digitalWrite(laser,LOW);    
  }
   
  FastLED.show();
  delay(100);
/*
  for (int i=0; i < pixelCount; i++) {
    leds[i] = 0xFFFFFF;
    digitalWrite(wirelessPower,HIGH);
    delay(100);
    FastLED.show();
    delay(1000);
    leds[i] = 0x000000;
    digitalWrite(wirelessPower,LOW);
    FastLED.show();
    delay(1000);
  }
  */
}
