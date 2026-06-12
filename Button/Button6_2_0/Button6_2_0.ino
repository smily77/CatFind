//#define Atom3S
#include <xComDef6_2.h>
#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true
byte ID = Schalter;

unsigned long timer;
bool blinkOn = false;
bool targetAlarm = false;
ucDataStruct ucDataToSend;

#include <xComProc6_2.h>

void setup() {
  Serial.begin(115200);
  M5.begin();
#ifdef Atom3S
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_YELLOW);
  M5.Lcd.print("Setup WiFi");
#else
  initPixel();
#endif
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  initText2Udp();
//  setUpOTA();
//  setUpTime();
  ucDataToSend.sender = ID;
  undefFireState();
  Serial << "ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  M5.update();
#ifdef Atom3S
  if (M5.BtnA.wasPressed()) {
#else
  if (M5.Btn.wasPressed()) {
#endif
    undefFireState();
    ucDataToSend.cmd = armFire;
    sendUcData(ucDataToSend, device[PA2i].IP);
    Serial << "Die Taste wurde gedrückt" << endl;
//    sendUdpTextln("Die Taste wurde gedrückt");
  }
  if (mcDataReceived) {
    mcDataReceived = false;
    if (lastMcMsg.msgCode == HB) {
      if (device[lastMcMsg.sender].type == PowerActor) {
        if (lastMcMsg.dataHB.pa2HB.readyToFire) fireReady();
          else fireNotReady();
      }
    }
  }

}
