#include <xComDef6_2.h>
#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true
byte ID = Manager;

#define containLed
#ifdef containLed
  CRGB leds[pixelNum];
#endif

unsigned long timer;
bool blinkOn = false;
bool targetAlarm = false;

#include <xComProc6_2.h>

void setup() {
  Serial.begin(115200);
  initPixel();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
//  setUpTime();
  Serial << "ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  if (udpTextReceived) {
    udpTextReceived = false;
    Serial.print("received:" );
    Serial.println(udpTextMsg);
  }
  if (mcDataReceived) {
    mcDataStruct mcMsg;
    mcMsg=lastMcMsg;
    printSensorData(mcMsg);

    if (mcMsg.msgCode == HB) {
      allPixel(0x00FF00);
      blinkOn = true;
      timer = millis()+ HB_blinkPeriode;
    }
    else if (mcMsg.msgCode == catObserved) {
      allPixel(0x0000FF);
      targetAlarm = true;
      blinkOn = false;
      timer = millis()+ Alarm_blinkPeriode;
    }
    mcDataReceived = false;
  }
  if (blinkOn && (millis()> timer)) {
    allPixel(0x000000);
    blinkOn = false;
  }
  if (targetAlarm && (millis()> timer)) {
    allPixel(0x000000);
    targetAlarm = false;
  }

}
