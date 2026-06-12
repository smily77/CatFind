#if defined DomeDevice
  #define ID Dome

  #define containLed
  #define COLOR_ORDER RGB
  #define pixelNum 2
  #define pixelPin 32
  #define maxPix 1
  #define minPix 0
  uint8_t BRIGHTNESS = 5;
  //HW
  #define laser 25
  #define radar 33
  //Serial2
  #define S2_TX 12
  #define S2_RX radar
  #define S2_baud 256000

  #define periodeForHB 5000
  #define statusLightDuration 10
  #define deadZone 500

#elif defined MiniDomeDevice
  #define ID MiniDome
  
  //Pixel sim
  #define containLed
  #define COLOR_ORDER RGB
  #define pixelNum 2
  #define pixelPin 32
  #define maxPix 1
  #define minPix 0
  uint8_t BRIGHTNESS = 5;
  //HW
  #define radar 34
  //Serial2
  #define S2_TX 12
  #define S2_RX radar
  #define S2_baud 256000

  #define periodeForHB 500
  #define statusLightDuration 10
  #define deadZone 500

#elif defined CompactDomeDevice
  #define ID CompactDome

  #define containLed
  #define COLOR_ORDER GRB
  #define pixelNum 1
  #define pixelPin 27
  #define maxPix 0
  #define minPix 0
  uint8_t BRIGHTNESS = 100;
  //HW
  #define radar 19
  //Serial2
  #define S2_TX 12
  #define S2_RX radar
  #define S2_baud 256000

  #define periodeForHB 5000
  #define statusLightDuration 10
  #define deadZone 1000

#endif

#define LED_TYPE WS2811

byte radarBuffer[30];
int gi = 0; // globale laufvariabele für Radasignal
struct targetData {
  boolean active;
  int x;
  int y;
  float l;
  float phi;
  int geschw;
  int res;
};
targetData target[3];
boolean newDataReady = false;
