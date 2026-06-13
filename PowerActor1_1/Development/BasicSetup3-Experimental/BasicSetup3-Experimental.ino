// 2 - winkelgeber, rotor initialPos
// 3 - Time UDP taskSheduler
#include <FastLED.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Streaming.h>
#include <Wire.h>
#include <PCF8574.h>
#include "hardware.h"
#include "AS5600.h"
#include "time.h"
#include "sntp.h"
#include <AsyncUDP.h>
#include <math.h>
#include <TaskScheduler.h>

#define DEBUG true
#define defaultStepSize sixteenthStep // fullStep
#define helligkeit 10

CRGB leds[pixelCount];
TwoWire I2Cone = TwoWire(0);
PCF8574 rotor(&I2Cone, motorAdr);
AS5600 winkel(&I2Cone);
struct tm timeinfo;
AsyncUDP udp;
Scheduler taskMgr;
void oneTimeLedOffHelper();
Task allLedOffAfter1000(1000,2,&oneTimeLedOffHelper);
void sendHB();
Task sendPeriodicHB(5000, TASK_FOREVER, &sendHB); 

int globalPosition = 0;

void setup() {
  if (DEBUG) Serial.begin(115200);
  taskMgr.init();
  taskMgr.addTask(allLedOffAfter1000);
  taskMgr.addTask(sendPeriodicHB);
  initPixels();
  led("B",1);
  led("M",0);
  setUpWifi(); 
  setUpOTA();
  led("Y",0);
  configTzTime(time_zone, ntpServer1, ntpServer2);
  while(!getLocalTime(&timeinfo));
  led("C",0);
  initPaControls();  
  I2Cone.begin(sda,scl,100000U);
  rotorInit();
  adjustStepSize(defaultStepSize);
  initWinkelMesser();
  rotorZeroPos();
  //digitalWrite(wirelessPower,HIGH);
  led("G",0);
  //sendPeriodicHB.enable();
  //allLedOffAfter1000.enable();
  rotorStep(right,sixteenthStep);
  Serial << globalPosition << "  " << winkel.rawAngle() << endl;
}



void loop() {
  while ((globalPosition % 1300) !=0) rotorStep(right,sixteenthStep);
  rotorStep(right,sixteenthStep);
  Serial << globalPosition << "  " << winkel.rawAngle() << "  " << winkel.readAngle() << endl;
  delay(3000);
  
  if (!digitalRead(taster)) ESP.restart();
  delay(10);
  ArduinoOTA.handle();
  taskMgr.execute();
}
