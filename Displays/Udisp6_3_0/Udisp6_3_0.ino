//Basieren auf Dev4
//x_2 Pivot als 0/0 Punkt
//x_3 Neue Draw Ray Funktion
//x_4 Suntoon5 Encoder
//x_5 relocated disp driver
//6_3 umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//#define CYD28
#define CYD35
//#define Sunton7
//#define Sunton5
//#define M5Core2
//#define M5Tab5
//#define Wave7

// Flash Size 16MB
// PSRAM "OPI PSRAM"

#include <xComDef6_3.h>

#define DEBUG true

#include "dispDevLoGFX.h"
#include "dispDef.h"

int deadZone = initDeadZone;
int xScale,yScale;

int encoder;
LGFX_Sprite layer[noSprites];
uint8_t menuPage = 0;
int dispAngel = 0;
int dispShift = 0;
bool actionsActive = false;
int spritFactor;
bool hasPSRAM;
static int16_t g_px[noSprites];
static int16_t g_py[noSprites];
cmdPayload cmdToSend;

int encoderPos[pages] = {
0,
2048 - (maxAngle * 4096 / 360),
2048 + (maxAngle * 4096 / 360),
maxDist,
deadZone,
dispShift,
dispAngel};

int encoderStep[pages] = {
1,
10,
10,
33,
33,
2,
1};



#include <xComProc6_3.h>

void setup(){
  Serial.begin(115200);
  Serial << " init disp " << endl;
  initDisp();
#if defined(M5Tab5)
  WiFi.setPins(12, 13, 11, 10, 9, 8, 15);
#endif
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  initText2Udp();
  //setUpTime();
  //setUpOTA();
  calcScale(maxAngle, maxDist);
  encoderStep[farLimitPage] = yScale;
  encoderStep[nearLimitPage] = yScale;
  drawGrid();
  pushScreens(dispAngel,dispShift,true);
}

void loop(){
 /*
#if defined(Sunton5) || defined(M5Core2)
  for (int i=0;i < 8;i++) {
    if (terminal.getButtonStatus(i)) terminal.setLEDColor(i,0x000000);
      else  terminal.setLEDColor(i,0x00FF00);
  }
#endif
*/
  //ArduinoOTA.handle();
  if (checkSwitch()) {
    if (actionsActive) {
      switchActions();
    }
    else {
      actionsActive = true;
      menuPage = 0;
      encoderPos[menuPage]= 0;
      drawMenue(menuPage);
    }
  }
  if (actionsActive) checkAction();

  if (mcDataReceived) {
    xMsg receivedMsg = lastMcMsg;
    mcDataReceived = false;
    if ((receivedMsg.header.msgCode == HB) &&  (receivedMsg.header.sender == targetSystem )){
      //printSensorData(receivedMsg);
      pa2HbPayload pa2;
      if (getPayload(receivedMsg, pa2)) {
        layer[limitLayer].fillScreen(0);
        if (pa2.limitsActive) {
          drawVectorLine(layer[limitLayer],PX(limitLayer,0),PY(limitLayer,0),pa2.leftLimit,1);
          drawVectorLine(layer[limitLayer],PX(limitLayer,0),PY(limitLayer,0),pa2.rightLimit,1);
          int leftWinkel = ((pa2.leftLimit*360)/4096)+90;
          int rightWinkel = ((pa2.rightLimit*360)/4096)+90;
          layer[limitLayer].drawArc(PX(limitLayer,0), PY(limitLayer,0), int(pa2.farLimit/xScale)-1, int(pa2.farLimit/xScale), leftWinkel, rightWinkel, 1);
          layer[limitLayer].drawArc(PX(limitLayer,0), PY(limitLayer,0), int(pa2.nearLimit/xScale)-1, int(pa2.nearLimit/xScale)+1, leftWinkel, rightWinkel, 1);
        }
        if (!actionsActive) pushScreens(dispAngel,dispShift,true);
      }
    }
    else if (receivedMsg.header.msgCode == catObserved) {
      posPayload pos;
      if (getPayload(receivedMsg, pos)) {
        layer[detectLayer].drawPixel(PX(detectLayer,int(pos.x / xScale)),PY(detectLayer,int(pos.y / yScale)),2);
        if (!actionsActive) pushScreens(dispAngel,dispShift,false);
      }
    }
  }

}
