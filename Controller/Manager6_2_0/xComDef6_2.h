//1 - Wlan initialisierung und Multicast routinen
//2 - Unicast routinen
//3 - it definitions array
//4 - OTA
//5 - Time
//6 - Pixel & PrintOut
//7 - Koordinatenumrechnug
//8 - Measurement ammendet
//9 - LD06 Lidar
//10 - Button für ucData
//11 - Union fur mcDataStruct HB & HB proc & periodeForHB & aut eintragen der IP & armFire für uc und sendUc prüft ob IP = 0
//1.01 - Udp Text, schalter
//1.x - 2 HostName in device dB

#include <Streaming.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <ArduinoOTA.h>
#include "time.h"
#include <Credentials.h>

struct stationDefinitions {
  byte   type;
  byte   IP;
  byte   MAC;
  String Name;
};
//Device
#define Manager 0
#define Dome 1
#define MiniDome 2  // Ohne Pixel
#define CompactDome 3
#define PA2i 4
#define Disp7 5
#define CYD 6
#define LD06 7
#define Schalter 8
#define Disp5 9
#define Core2 10
#define Tab5 11
#define CYD35Z 12
#define Wave7z 13
//Types
#define MananagementDevice 1
#define HLK 2
#define PowerActor 3
#define Screen 4
#define Controller 5
#define Lidar 6
#define onOffSchalter 7
//  {MananagementDevice,180,0x01},
//const stationDefinitions device[14] = {
stationDefinitions device[14] = {
  {MananagementDevice,180,0x01,"Manager_Dev"},  //Manager
  {HLK,0,0,"Dome"},                             //Dome
  {HLK,0,0,"Mini_Dome"},                        //MiniDome
  {HLK,0,0,"Compact_Dome"},                     //CompactDome - auf PA M5PicoDome
  {PowerActor,181,0x02,"PowerActor1"},          //PA1
  {Screen,0,0,"Disp_7"},                        //Display 7 Inch
  {Controller,0,0,"CYD"},                       //CYD Controller
  {Lidar,0,0,"LD6"},                            //LD06
  {onOffSchalter,0,0,"Button"},                  //Schalter - achtung nicht Unique
  {Screen,0,0,"Disp_5"},                         //Display 5 Inch 
  {Screen,0,0,"Core2"},                          //Core2 
  {Screen,0,0,"Tab5"},                           //Tab5 
  {Screen,0,0,"CYD35Zoll"},                      //CYD35Zoll 
  {Screen,0,0,"Wavetec_7inch"}                   //Wavetec
};


// call -> device[ident].type
//action - msgCode
#define HB          1
#define catObserved 2
#define measurement 3
#define catHit      4

struct mcDataStruct {
  byte sender;
  byte msgCode;
  time_t timeStamp;
  union {
    struct {
      int x;
      int y;
      float radius;
      float angle;
      int targetSpeed;
      int res;    
      byte sensor;
    };
    // data HB
    struct {
      byte ip;
      unsigned int HBperiode;
      union {
        // pa2HB
        struct {
           bool readyToFire;
           bool limitsActive;
           float leftLimit;
           float rightLimit;
           float farLimit;
           float nearLimit;
        } pa2HB;
        // radarHB
        struct {
          float deadZone;
        } radarHB;
      }; // Union
    } dataHB;
  };  // Union
};

IPAddress multiCastIP (239,0,0,57); 
constexpr uint16_t MC_PORT = 8266; 
AsyncUDP udpMc;
volatile bool mcDataReceived = false;
mcDataStruct lastMcMsg;

//------------------------------------------------------------------------------------
constexpr uint16_t MC_Text_PORT = 8300; 
AsyncUDP udpText;
volatile bool udpTextReceived = false;
String udpTextMsg;
//------------------------------------------------------------------------------------

#define richtung                1
#define laser                   2
#define adjustAngle             3
#define taste                   4
#define armFire                 5
#define setLeftLimit            6
#define setRightLimit           7
#define setFarLimit             8
#define setNearLimit            9
#define changeLimitActivation  10

struct ucDataStruct {
  byte sender;
  byte cmd;
  int  info;
};

static const uint16_t UC_PORT = 23456;
AsyncUDP udpUc;
volatile bool ucDataReceived = false;
ucDataStruct lastUcMsg;

struct tm timeinfo;
time_t now;
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
const char* time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
