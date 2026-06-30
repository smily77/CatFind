// under development
//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      HB geht als radarHbPayload (inkl. Totzone) raus, Beobachtungen als posPayload
//#define  CompactDomeDevice
//#define  MiniDomeDevice
#define  DomeDevice

#include <xComDef6_3.h>

#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true

#ifdef containLed
  CRGB leds[pixelNum];
#endif

unsigned long timer;
boolean statusLightOn = false;

posPayload obsData;

#include <xComProc6_3.h>

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
        obsData.x = target[i].x;
        obsData.y = target[i].y;
        obsData.radius = target[i].l;
        obsData.angle = target[i].phi;
        obsData.sensor = i;
        obsData.targetSpeed = target[i].geschw;
        obsData.res = target[i].res;
        broadcastMsg(catObserved,obsData);
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
