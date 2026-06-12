//0_1 - Korrektur Winkel aut. Folgen
//0_2 - Neue Funktionen und strukturiert
//1_0
//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      HB-Zustand liegt jetzt direkt im pa2HbPayload hbState statt im Union msgHBStorage,
//      Kommandos kommen als commandMsg mit cmdPayload
#include <xComDef6_3.h>
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

posPayload txPos;
pa2HbPayload hbState;

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

#include <xComProc6_3.h>

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
  Serial << "Ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();
  if (ucDataReceived) {
    ucDataReceived =false;
    cmdPayload cmd;
    if (getPayload(lastUcMsg, cmd)) {
      switch (cmd.cmd) {
        case cmdArmFire:
          hbState.readyToFire = !hbState.readyToFire;
        break;
        case cmdSetLeftLimit:
          hbState.leftLimit = cmd.info;
          hbState.limitsActive = true;
          saveHBToVault();
        break;
        case cmdSetRightLimit:
          hbState.rightLimit = cmd.info;
          hbState.limitsActive = true;
          saveHBToVault();
        break;
        case cmdSetFarLimit:
          hbState.farLimit = cmd.info;
          hbState.limitsActive = true;
          saveHBToVault();
        break;
        case cmdSetNearLimit:
          hbState.nearLimit = cmd.info;
          hbState.limitsActive = true;
          saveHBToVault();
        break;
        case cmdChangeLimitActivation:
         hbState.limitsActive = !hbState.limitsActive;
         saveHBToVault();
        break;
      } //switch
      sendHBmsg();
      sendHBmsg();
    } //getPayload
  } //ucDataReceived
 /*
    if (cmd.cmd == cmdLaser) {
    }
    else if (cmd.cmd == cmdRichtung) {
    }
    else if (cmd.cmd == cmdAdjustAngle) {
    }
    else if (cmd.cmd == cmdTaste) {
    // scanWithLidar(); //switchLaser();//
    }
*/
  if (mcDataReceived) {
    mcDataReceived = false;

    /*
    if (!adjustmentMode) {
      if (lastMcMsg.header.msgCode == catObserved)  {
        posPayload obs;
        if (getPayload(lastMcMsg, obs)) {
          servoGoTo(int(obs.angle+angleAdjust));
          int abstand;
          if (!st.ReadMove(1)) abstand = int(laserDist()); else abstand = 0;
          if (abs(obs.radius - abstand) < 150) {
            digitalWrite(laserPwr,HIGH);
            fireOn = true;
            fireTimer = millis()+fireDuration;
            broadcastMsg(catHit, obs);
          }
          if (DEBUG) {
            txPos = obs;
            txPos.radius = abstand;
            txPos.angle = servoPos;
            broadcastMsg(measurement, txPos);
          }
          if (DEBUG) Serial << int(obs.radius) << "  " << abstand << endl;
        }
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
