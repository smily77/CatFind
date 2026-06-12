#include <xComDef6_2.h>
#include "hwDef.h"
#include <FastLED.h>

#define DEBUG false
#define containLed
byte ID = LD06;

#ifdef containLed
  CRGB leds[pixelNum];
#endif

// lidar var
char rawData[maxSentenceLength + 10]; // eigentlich 46
bool newLidarData = false;
messDaten mData[dataPointPerSentence];

unsigned long fireTimer;
bool fireOn = false;

mcDataStruct comTXMsg;

#define minAngleC 250
#define maxAngleC 330
#define nearEndC 500
#define farEndC 6500

#include <xComProc6_2.h>

void setup(){
  initHw();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  setUpOTA();
  setUpTime();
  lidarMotor(HIGH);
  initLidar();
  comTXMsg.sender = ID;
}

void loop() {
  ArduinoOTA.handle();
  if (Serial2.available()) newLidarData = readLidar();
  if (newLidarData) {
    newLidarData = false;
    for (int i=0; i<dataPointPerSentence;i++) {
      if (isDataInRange(mData[i],minAngleC,maxAngleC,nearEndC,farEndC)) {
        comTXMsg.msgCode = catObserved;
        comTXMsg.angle = (450 - mData[i].winkel)*2048/180;
        comTXMsg.radius = mData[i].dist;
        toPaKart(comTXMsg.x,comTXMsg.y,comTXMsg.angle,comTXMsg.radius);
        sendMcData(comTXMsg);
        /*
        allPixel(0xFF0000); 
        fireOn = true;
        fireTimer = millis()+fireDuration;  
        */
      }
    }
  }
  if (fireOn) {
    if (millis() > fireTimer) {
      fireOn = false;
      allPixel(0x000000);  
    }
  }
}
