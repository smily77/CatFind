

#define hostName "Button1"
#define periodeForHB 10000

#ifdef Atom3S
  #include <M5Unified.h>
#else
  #include <M5Atom.h>
  #define containLed
  #define pixelPin 27 
  #define pixelNum 25
  #define LED_TYPE    WS2811
  #define COLOR_ORDER GRB
  CRGB leds[pixelNum];
#endif
uint8_t BRIGHTNESS = 5;
#define minPix 0
#define maxPix 26

#define HB_blinkPeriode 1
#define Alarm_blinkPeriode 500
