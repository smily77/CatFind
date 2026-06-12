// under development
//#define  CompactDomeDevice 
#define  MiniDomeDevice 
//#define  DomeDevice 

#include <xComDef6_2.h>

#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true
//  byte ID = MiniDome;

#ifdef containLed
  CRGB leds[pixelNum];
#endif

unsigned long timer;
boolean statusLightOn = false;

mcDataStruct comTXMsg;

#include <xComProc6_2.h>

void setup() {
  Serial.begin(115200);
  initPixel();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  setUpOTA();
  setUpTime();
  Serial2.begin(S2_baud,SERIAL_8N1,S2_RX,S2_TX);
  timer = millis();  //heardBeat
  comTXMsg.sender = ID;
}

void loop() {
  ArduinoOTA.handle();
  heardBeat(); 
  if (Serial2.available()) readRadar();
  if (newDataReady) {
    boolean validTarget = false;
    for (int i=0; i < 3; i++) {
      if (target[i].active && (target[i].l > deadZone)) {
        validTarget = true;
        comTXMsg.msgCode = catObserved;
        comTXMsg.x = float(target[i].x);
        comTXMsg.y = float(target[i].y);
        comTXMsg.radius = target[i].l;
        comTXMsg.angle = target[i].phi;
        comTXMsg.sensor = i;
        comTXMsg.targetSpeed = target[i].geschw;
        comTXMsg.res = target[i].res;
        time(&now);
        comTXMsg.timeStamp = now;
        sendMcData(comTXMsg);
      }
    }
    if (validTarget){
      setPixel(maxPix,0x0000FF);
    } else {
        setPixel(maxPix,0x000000);
      }
    newDataReady = false;
  }
}
