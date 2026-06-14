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
//6_3 - Fixer Header + variabler Payload:
//      * msgHeader (version, sender, msgCode, payloadLen, timeStamp) wird beim Senden automatisch gefüllt
//      * pro Nachrichtenart ein eigener Payload-Struct (posPayload, hbPayload, pa2HbPayload, radarHbPayload, markerHbPayload, cmdPayload)
//      * der alte Union mcDataStruct und ucDataStruct entfallen - Kommandos laufen als commandMsg im selben Format
//      * Broadcast (Multicast) und Unicast verwenden dasselbe Nachrichtenformat (broadcastMsg / unicastMsg)
//      * Pakete werden über Version + payloadLen validiert, getPayload() liest typsicher aus
//      * Simulator in der device dB (Sim)

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
#define Sim 14
#define LaserMarker 15
//Types
#define MananagementDevice 1
#define HLK 2
#define PowerActor 3
#define Screen 4
#define Controller 5
#define Lidar 6
#define onOffSchalter 7
#define Marker 8
//  {MananagementDevice,180,0x01},
stationDefinitions device[16] = {
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
  {Screen,0,0,"Wavetec_7inch"},                  //Wavetec
  {MananagementDevice,0,0,"Simulator"},          //Simulator (Cardputer)
  {Marker,182,0x03,"Laser_Marker"}               //LaserMarker (ESP32-C3, feste IP .182)
};

// call -> device[ident].type

//---------------------------------------------------------------------------------------
// Protokoll 6.3: jede Nachricht = msgHeader + payloadLen Bytes Payload
//---------------------------------------------------------------------------------------
#define XCOM_VERSION 0x63

//action - msgCode
#define HB          1
#define catObserved 2
#define measurement 3
#define catHit      4
#define commandMsg  5

struct __attribute__((packed)) msgHeader {
  uint8_t version;     // XCOM_VERSION - Empfänger verwirft fremde Versionen
  uint8_t sender;      // Geräte-ID (device dB Index)
  uint8_t msgCode;
  uint8_t payloadLen;  // Länge des Payloads in Bytes (0 erlaubt)
  int64_t timeStamp;   // time_t des Senders
};

constexpr uint8_t maxPayloadLen = 64;

// Empfangs-/Sende-Container: Header + Payload-Puffer (auf der Leitung nur payloadLen Bytes)
struct xMsg {
  msgHeader header;
  uint8_t   payload[maxPayloadLen];
};

//----------------------------------- Payloads -------------------------------------------
// catObserved / measurement / catHit
struct __attribute__((packed)) posPayload {
  int32_t x;
  int32_t y;
  float   radius;
  float   angle;
  int32_t targetSpeed;
  int32_t res;
  uint8_t sensor;
};

// HB Basisdaten - stehen bei allen HB-Varianten am Anfang
struct __attribute__((packed)) hbPayload {
  uint8_t  ip;          // letztes Oktett
  uint32_t HBperiode;
};

// HB des PowerActors (Basis + Limits)
struct __attribute__((packed)) pa2HbPayload {
  hbPayload hb;
  uint8_t   readyToFire;
  uint8_t   limitsActive;
  float     leftLimit;
  float     rightLimit;
  float     farLimit;
  float     nearLimit;
};

// HB der Radarsensoren (Basis + Totzone)
struct __attribute__((packed)) radarHbPayload {
  hbPayload hb;
  float     deadZoneDist;   // nicht "deadZone": kollidiert mit #define in hwDef
};

// HB des LaserMarkers (Basis + aktueller Zustand aller Ausgaenge)
// Wird zyklisch gebroadcastet und nach jedem Kommando sofort -> der aktuelle
// Geraetezustand ist damit jederzeit von aussen ablesbar.
struct __attribute__((packed)) markerHbPayload {
  hbPayload hb;
  uint8_t   mainLaser;   // 0 = aus, 1 = an
  uint8_t   subLaser;    // 0 = aus, 1 = an
  uint8_t   aux;         // 0 = aus, 1 = an
  uint8_t   r;           // Pixel-Rot   0..255
  uint8_t   g;           // Pixel-Gruen 0..255
  uint8_t   b;           // Pixel-Blau  0..255
};

// commandMsg (ersetzt ucDataStruct) - typischerweise per Unicast
struct __attribute__((packed)) cmdPayload {
  uint8_t cmd;
  int32_t info;
};

//cmd Codes für cmdPayload
#define cmdRichtung                1
#define cmdLaser                   2
#define cmdAdjustAngle             3
#define cmdTaste                   4
#define cmdArmFire                 5
#define cmdSetLeftLimit            6
#define cmdSetRightLimit           7
#define cmdSetFarLimit             8
#define cmdSetNearLimit            9
#define cmdChangeLimitActivation  10
// LaserMarker-Kommandos
#define cmdMainLaser              11   // info: 0 = aus, 1 = an
#define cmdSubLaser               12   // info: 0 = aus, 1 = an
#define cmdAux                    13   // info: 0 = aus, 1 = an
#define cmdPixelColor             14   // info: 0x00RRGGBB (24-Bit Farbe)
#define cmdMarkerState            15   // info: ignoriert - erzwingt sofortigen HB

//---------------------------------------------------------------------------------------
IPAddress multiCastIP (239,0,0,57);
constexpr uint16_t MC_PORT = 8266;
AsyncUDP udpMc;
volatile bool mcDataReceived = false;
xMsg lastMcMsg;

//------------------------------------------------------------------------------------
constexpr uint16_t MC_Text_PORT = 8300;
AsyncUDP udpText;
volatile bool udpTextReceived = false;
String udpTextMsg;
//------------------------------------------------------------------------------------

static const uint16_t UC_PORT = 23456;
AsyncUDP udpUc;
volatile bool ucDataReceived = false;
xMsg lastUcMsg;

struct tm timeinfo;
time_t now;
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
const char* time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
