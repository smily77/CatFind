//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      Beobachtungen gehen als catObserved mit posPayload raus, sender/timeStamp füllt der Header
#include <xComDef6_3.h>
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

posPayload obsData;

#define minAngleC 250
#define maxAngleC 330
#define nearEndC 500
#define farEndC 6500

#include <xComProc6_3.h>

void setup(){
  initHw();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  setUpOTA();
  setUpTime();
  lidarMotor(HIGH);
  initLidar();
}

void loop() {
  ArduinoOTA.handle();
  if (Serial2.available()) newLidarData = readLidar();
  if (newLidarData) {
    newLidarData = false;
    for (int i=0; i<dataPointPerSentence;i++) {
      if (isDataInRange(mData[i],minAngleC,maxAngleC,nearEndC,farEndC)) {
        obsData.angle = (450 - mData[i].winkel)*2048/180;
        obsData.radius = mData[i].dist;
        int px, py;
        toPaKart(px,py,obsData.angle,obsData.radius);
        obsData.x = px;
        obsData.y = py;
        broadcastMsg(catObserved,obsData);
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
