// 2 - winkelgeber, rotor initialPos
#include <FastLED.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Streaming.h>
#include <Wire.h>
#include <PCF8574.h>
#include "hardware.h"
#include "AS5600.h"

#define DEBUG true
#define defaultStepSize sixteenthStep // fullStep
#define helligkeit 10

CRGB leds[pixelCount];
TwoWire I2Cone = TwoWire(0);
PCF8574 rotor(&I2Cone, motorAdr);
AS5600 winkel(&I2Cone);

int globalPosition = 0;

void setup() {
  if (DEBUG) Serial.begin(115200);
  initPixels();
  led("B",1);
  led("R",0);
  setUpWifi();
  led("Y",0);
  setUpOTA();
  initPaControls();  
  I2Cone.begin(sda,scl,100000U);
  rotorInit();
  adjustStepSize(defaultStepSize);
  initWinkelMesser();
  rotorZeroPos();
  led("",0);
  led("",1);
}



void loop() {
  if (!digitalRead(taster)) ESP.restart();
  delay(10);
  ArduinoOTA.handle();
}
