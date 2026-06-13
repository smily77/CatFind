#include <FastLED.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Streaming.h>
#include <Wire.h>
#include <PCF8574.h>
#include "hardware.h"

#define DEBUG true
#define defaultStepSize fullStep
#define helligkeit 10

CRGB leds[pixelCount];
TwoWire I2Cone = TwoWire(0);
PCF8574 rotor(&I2Cone, motorAdr);

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
  led("",0);
  led("",1);
}

void loop() {
  if (digitalRead(taster)) {
    led("",0);
    rotor.digitalWrite(STEP, LOW);
  }
  else {
    led("G",0);
    rotor.digitalWrite(STEP, HIGH);    
  }
   
  ArduinoOTA.handle();
}
