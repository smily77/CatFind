// 1 - Auf Basis 3 -> Radar über wirelessPower
// 2 - BLE Radar einbau
// 3 - Div. Tests
// 4 - "Stand" Sender, Broadcast , telnet 
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
#define outputDevice Telnet // Serial

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
static BLEAddress sensorAddress("FF:BD:C0:1B:11:09"); 
//static BLEAddress sensorAddress("ae:02:8b:83:68:78"); 
static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID("0000fff1-0000-1000-8000-00805f9b34fb");
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
bool connected = false;
bool doConnect = false;
unsigned long reConnectTimer;
bool waitToReconnect = false;
bool newBleData = false;
targetData BLEtarget[3];
// TelnetServer
WiFiServer TelnetServer(23);
WiFiClient Telnet;

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
  TelnetServer.begin();
  TelnetServer.setNoDelay(true); 
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
  outputDevice << endl;
  outputDevice << globalPosition << endl;
  rotor.digitalWrite(SLP, LOW);
  allLedOff();
  digitalWrite(wirelessPower,true);
  led("Y",1);
  sendPeriodicHB.enable();
  allLedOffAfter1000.enable();
  BLEDevice::init("");
  doConnect = true;
  
}



void loop() {
  if (doConnect || !connected) reConnect();

  // Wenn die Verbindung unterbrochen wird
  if (connected && !pClient->isConnected()) {
    connected = false;
    outputDevice << "Sensorverbindung verloren. Versuche, erneut zu verbinden..." << endl;
    doConnect = true;
  }

  if (newBleData) {
    newBleData = false;
    if (BLEtarget[0].active) {
      led("R",0);
      broadcastCatAlarm(BLEtarget[0]);
    }
      else led("",0);
  }
   
  if (!digitalRead(taster)) ESP.restart();
  handleTelnet();
  ArduinoOTA.handle();
  taskMgr.execute();
}
