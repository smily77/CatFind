//0_1 - Korrektur Winkel aut. Folgen
//0_2 - Neue Funktionen und strukturiert
//1_0
#include <xComDef6_2.h>
#include "hwDef.h"
#include <SCServo.h>
#include "LP40B_LiDAR.h"
#include <Preferences.h>

#define DEBUG false
//#define containLed
#define ID PA2i

SMS_STS st;
int servoPos;

LP40B_LiDAR lidar(Serial2);
bool laserOn =false;

mcDataStruct comTXMsg,msgHBStorage;

Preferences vault;

unsigned long HBtimer;
unsigned long fireTimer;
bool fireOn = false;
//bool fireReady = false;
const int systemAngle = maxAngle * 4096 / 360;
bool limitsActiveWert = true;
float leftLimitWert = 2048 - systemAngle;
float rightLimitWert = 2048 - systemAngle;
float farLimitWert = maxDist;
float nearLimitWert = initDeadZone;

#include <xComProc6_2.h>

void setup() {
  initHw();
  //digitalWrite(LD06Pwr,HIGH);
  setUpWifi(device[ID].IP);
  assembleHBmsg();
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  //setUpTime();
  initLidar();
  initServo();
  
  HBtimer = millis();  //heardBeat
  comTXMsg.sender = ID;
  Serial << "Ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();
  if (ucDataReceived) {
    ucDataReceived =false;
    switch (lastUcMsg.cmd) {
      case armFire:
        msgHBStorage.dataHB.pa2HB.readyToFire = !msgHBStorage.dataHB.pa2HB.readyToFire;    
      break;
      case setLeftLimit:
        msgHBStorage.dataHB.pa2HB.leftLimit = lastUcMsg.info;
        msgHBStorage.dataHB.pa2HB.limitsActive = true;
        saveHBToVault();
      break;
      case setRightLimit:
        msgHBStorage.dataHB.pa2HB.rightLimit = lastUcMsg.info;
        msgHBStorage.dataHB.pa2HB.limitsActive = true;
        saveHBToVault();
      break;
      case setFarLimit:
        msgHBStorage.dataHB.pa2HB.farLimit = lastUcMsg.info;
        msgHBStorage.dataHB.pa2HB.limitsActive = true;
        saveHBToVault();
      break;
      case setNearLimit:
        msgHBStorage.dataHB.pa2HB.nearLimit = lastUcMsg.info;
        msgHBStorage.dataHB.pa2HB.limitsActive = true;
        saveHBToVault();
      break;
      case changeLimitActivation:
       msgHBStorage.dataHB.pa2HB.limitsActive = !msgHBStorage.dataHB.pa2HB.limitsActive;
       saveHBToVault();
      break;
    } //switch
    sendHBmsg();
    sendHBmsg();    
  } //ucDataReceived
 /*
    if (lastUcMsg.cmd == laser) {      
    }
    else if (lastUcMsg.cmd == richtung) {
    }
    else if (lastUcMsg.cmd == adjustAngle) {
    }
    else if (lastUcMsg.cmd == button) {
    // scanWithLidar(); //switchLaser();//
    }
    
 //   if (lastUcMsg.cmd == armFire) {
 //     msgHBStorage.dataHB.pa2HB.readyToFire = !msgHBStorage.dataHB.pa2HB.readyToFire;    
//      sendHBmsg();
//      sendHBmsg();
//    }
//   }
*/
  if (mcDataReceived) {
    mcDataReceived = false;

    /*
    if (!adjustmentMode) {
      if (lastMcMsg.msgCode == catObserved)  {
        mcDataStorage = lastMcMsg;
        servoGoTo(int(mcDataStorage.angle+angleAdjust));
        int abstand;
        if (!st.ReadMove(1)) abstand = int(laserDist()); else abstand = 0;
        if (abs(mcDataStorage.radius - abstand) < 150) {
          digitalWrite(laserPwr,HIGH);
          fireOn = true;
          fireTimer = millis()+fireDuration;
          comTXMsg = mcDataStorage;
          comTXMsg.msgCode  = catHit;
          comTXMsg.sender = ID;
          sendMcData(comTXMsg);
        }
        if (DEBUG) {
          comTXMsg.msgCode = measurement;
          comTXMsg.radius = abstand;
          comTXMsg.angle = servoPos;  
          sendMcData(comTXMsg);    
        }
        if (DEBUG) Serial << int(mcDataStorage.radius) << "  " << abstand << endl;
      } // catObserved   
    } // !adjustmentMode  
    */
  } // mcDataReceived
  if (fireOn) {
    if (millis() > fireTimer) {
      fireOn = false;
 //     if (!adjustmentMode) digitalWrite(laserPwr,LOW);
    }
  }
}
