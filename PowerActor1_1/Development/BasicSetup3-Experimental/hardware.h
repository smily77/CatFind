//Pixel
#define pixelPin 25
#define pixelCount 2
//PA Control
#define water 12
#define extPower 13
#define wirelessPower 14
#define taster 33
#define reedLeft  26
#define reedRight  32
#define sda 21
#define scl 19 
//9Pin Connector
#define pin3 15
#define pin4 2
#define pin5 4
#define pin6 16
#define pin7 17
#define pin8 5
#define laser 18
//Stepper control
#define DIR 0
#define STEP 1  
#define SLP 2
#define RST 3  
#define MS3 4
#define MS2 5  
#define MS1 6
#define EN 7
//Stepper Steps
#define fullStep 1
#define halfStep 2
#define quaterStep 4
#define eighthStep 8
#define sixteenthStep 16
//I2C adresses
#define motorAdr 0x20
//Structures
struct udpMsgStruct {
  byte msgCode;
  float x;
  float y;
  float radius;
  float angle;
  int targetSpeed;
  time_t timeStamp;
  byte sender;
};
struct targetData {
  boolean active;
  int x;
  int y;
  float l;
  float phi;
  int geschw;
};
//constants
//Wlan
const char* ssid = "xxx";
const char* password = "yyyyy";
//Time
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
const char* time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
//OTA
#define hostName "PA1b"
//Udp
IPAddress multiCastIP (224,0,5,5); 
constexpr uint16_t multiCastPort = 8266;
#define broadcastId 8 //8 = PA1
//Udp activity codes
#define HB 1
#define catObserved 2
//BLE
//static BLEAddress sensorAddress("ae:02:8b:83:68:78"); // Ersetzen Sie dies mit der MAC-Adresse Ihres Sensors
//static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
//static BLEUUID charUUID("0000fff1-0000-1000-8000-00805f9b34fb");
//Operation
#define left HIGH
#define right LOW
