// 1 - Auf Basis 3 -> Radar über wirelessPower
// 2 - BLE Radar einbau
// 3 -
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
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

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
//BLE Variables
static BLEAddress sensorAddress("ae:02:8b:83:68:78"); // Ersetzen Sie dies mit der MAC-Adresse Ihres Sensors
static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID("0000fff1-0000-1000-8000-00805f9b34fb");
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
bool connected = false;
bool doConnect = false;
bool newBleData = false;
targetData BLEtarget[3];

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
  while(abs(globalPosition) < 650) {
    rotorStep(right,sixteenthStep);
  }
  Serial << endl;
  Serial << globalPosition << endl;
  allLedOff();
  digitalWrite(wirelessPower,true);
  led("Y",1);
  sendPeriodicHB.enable();
  allLedOffAfter1000.enable();
  BLEDevice::init("");
  doConnect = true;
}



void loop() {
/*
  bool m = false;
  for(int la=0; la <= 10; la++) {
    for (int l=0; l <1300; l++) {
      rotorStep(m,fullStep);
    }
    m=!m;
    ArduinoOTA.handle();
    taskMgr.execute();
  }
*/
  if (doConnect) {
    doConnect = false;
    if (connectToSensor()) {
      Serial.println("Wieder verbunden");
    } else {
      Serial.println("Erneuter Verbindungsversuch in 5 Sekunden...");
      delay(5000); // Warte 5 Sekunden vor dem erneuten Verbindungsversuch
      doConnect = true;
    }
  }

  // Wenn die Verbindung unterbrochen wird
  if (connected && !pClient->isConnected()) {
    connected = false;
    Serial.println("Sensorverbindung verloren. Versuche, erneut zu verbinden...");
    doConnect = true;
  }
  if (newBleData) {
    newBleData = false;
    Serial << BLEtarget[0].active << " ";
    for (int k=0; k < 3;k++) {
      Serial.print(BLEtarget[k].phi+90);
      Serial.print(" ");
    }
    
    if (BLEtarget[0].active) {
      bool dir = !((BLEtarget[0].phi+90)> 0);
      int steps = (abs(BLEtarget[0].phi+90))/5;
      if (steps < 1) steps = 1;
      Serial << steps;
//      for (int z=0; z < steps ;z++) {
//        rotorStep(dir,sixteenthStep);
//      }
    }
    Serial.println();
  }
  if (!digitalRead(taster)) ESP.restart();
  ArduinoOTA.handle();
  taskMgr.execute();
}
