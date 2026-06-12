//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      Kommandos laufen als commandMsg mit cmdPayload, sender steht im Header
//#define Atom3S
#include <xComDef6_3.h>
#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true
byte ID = Schalter;

unsigned long timer;
bool blinkOn = false;
bool targetAlarm = false;
cmdPayload cmdToSend;

#include <xComProc6_3.h>

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
    cmdToSend.cmd = cmdArmFire;
    cmdToSend.info = 0;
    unicastMsg(commandMsg, cmdToSend, device[PA2i].IP);
    Serial << "Die Taste wurde gedrückt" << endl;
//    sendUdpTextln("Die Taste wurde gedrückt");
  }
  if (mcDataReceived) {
    mcDataReceived = false;
    if (lastMcMsg.header.msgCode == HB) {
      if (device[lastMcMsg.header.sender].type == PowerActor) {
        pa2HbPayload pa2;
        if (getPayload(lastMcMsg, pa2)) {
          if (pa2.readyToFire) fireReady();
            else fireNotReady();
        }
      }
    }
  }

}
