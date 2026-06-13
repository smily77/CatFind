#include <FastLED.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Streaming.h>
#include <Wire.h>
#include <PCF8574.h>


#define DEBUG true

const char* ssid = "xxx";
const char* password = "yyyy";
#define hostName "PA1b"

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

#define fullStep 1
#define halfStep 2
#define quaterStep 4
#define eighthStep 8
#define sixteenthStep 16

#define DIR 0
#define STEP 1  
#define SLP 2
#define RST 3  
#define MS3 4
#define MS2 5  
#define MS1 6
#define EN 7

#define sda 21
#define scl 19 
#define motorAdr 0x21
TwoWire I2Cone = TwoWire(0);
PCF8574 rotor(&I2Cone, motorAdr);

void setup() {
  Serial.begin(115200);
  setUpWifi();
  setUpOTA();
  pinMode(water,OUTPUT);
  digitalWrite(water,LOW);
  pinMode(extPower,OUTPUT);
  digitalWrite(extPower,LOW);
  pinMode(wirelessPower,OUTPUT);
  digitalWrite(wirelessPower,LOW);
  pinMode(taster,INPUT_PULLUP);
  pinMode(reed1,INPUT_PULLUP);
  pinMode(reed2,INPUT_PULLUP);
  pinMode(laser,OUTPUT);
  
  I2Cone.begin(sda,scl,100000U);

  rotorInit();
  FastLED.addLeds<WS2812, pixelPin, RGB>(leds, pixelCount);  // GRB ordering is typical
  FastLED.setBrightness(90);


}

void loop() {
  if (digitalRead(taster)) {
    leds[0] = 0xFF0000;
    leds[1] = 0xFF0000;
    digitalWrite(laser,HIGH);
    rotor.digitalWrite(STEP, LOW);
  }
  else {
    leds[0] = 0x00FF00;
    leds[1] = 0x00FF00;
    digitalWrite(laser,LOW);
    rotor.digitalWrite(STEP, HIGH);    
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
  ArduinoOTA.handle();
}
