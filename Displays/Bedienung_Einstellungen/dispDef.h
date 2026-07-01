
#define initDeadZone 500
#define maxAngle 60
#define maxDist 8000 
#define targetSystem PA2i

#define limitLayer 0
#define gridLayer 1
#define detectLayer 2
#define hitLayer 3
#define controlLayer 4
#define projectorLayer 5

#define TRANSPARENT 0xA145 // Braun
#define SCHWARZ     0x0000
#define WEISS       0xFFFFFF   //0xFFFF
#define ROT         0xFF0000  //0xF800
#define GRUEN       0x00FF00   //0x07E0
#define BLAU        0x0000FF  //0x001F
#define GELB        0xFFFF00   //0xFFE0
#define CYAN        0x00FFFF  //0x07FF
#define MAGENTA     0xFF00FF  //0xF81F
#define GRAU        0x010101  //0x8410
#define DUNKELGRAU  0x4208
#define ORANGE      0xFF8F00 //0xFD20
#define PINK        0xF9B6
#define HELLGRUEN   0x07F0
#define HELLBLAU    0x6B5F
//#define xxx
//#define xxx
//#define xxx

struct LayerCfg { int idx; uint8_t depth; };
const LayerCfg cfg2[] = {
  { limitLayer,     2 },
  { gridLayer,      1 },
  { detectLayer,    2 },
  { hitLayer,       1 },
  { controlLayer,   1 },
  { projectorLayer, 16}  
};
struct PalEntry {int layerIdx;  uint8_t colorIdx; uint32_t colorValue; };
const PalEntry palCfg[] = {
  { limitLayer,   1, GRUEN },
  { limitLayer,   2, ORANGE },
  { limitLayer,   3, BLAU },

  { gridLayer,    1, WEISS },

  { detectLayer,  1, ROT },
  { detectLayer,  2, ORANGE },
  { detectLayer,  3, BLAU },

  { hitLayer,     1, GELB },

  { controlLayer, 1, CYAN }
};
//Menue --------------------------------------------------------------
const char* menueItems[] ={
"left limit",
"right limit",
"far limit",
"near limit",
"display shift",
"display angle",
"erase limits",
"back"
};
const int pages = sizeof(menueItems) / sizeof(menueItems[0]);
#define mainMenue 0
#define leftLimitPage 1
#define rightLimitPage 2
#define farLimitPage 3
#define nearLimitPage 4
#define displayShiftPage 5
#define displayAnglePage 6
#define eraseLimitsPage 7
#define back 8
//Menue --------------------------------------------------------------

#if defined(CYD28)
  #define screenWidth  320
  #define screenHight  240
  #define ID CYD
  #define stEnable true
  #define noSprites 5
  #define scaling false
  //Encoder
  #define ENC_A 22
  #define ENC_B 27
  #define taste 35 
  #define counterInitialValue 2048
#elif defined(CYD35)
  #define screenWidth  480
  #define screenHight  320
  #define ID CYD35Z
  #define stEnable false
  #define noSprites 4
  #define scaling true
#elif defined(Sunton7)
  #define screenWidth  800
  #define screenHight  480
  #define ID Disp7
  #define stEnable true
  #define noSprites 6
  #define scaling true
#elif defined(Sunton5)
  #define screenWidth  800
  #define screenHight  480
  #define ID Disp5
  #define stEnable true
  #define noSprites 6
  #define scaling true
  // 8-Encoder des Prototyps hier nicht noetig (Touch-Bedienung)
#elif defined(M5Core2)
  #define screenWidth  320
  #define screenHight  240
  #define ID Core2
  #define stEnable true
  #define noSprites 6
  #define scaling true
  // 8-Encoder des Prototyps hier nicht noetig (Touch-Bedienung)
#elif defined(M5Tab5)
  #define screenWidth  1280
  #define screenHight  720
  #define ID Tab5
  #define stEnable true
  #define noSprites 6
  #define scaling true
#elif defined(Wave7)
  #define screenWidth  800
  #define screenHight  480
  #define ID Wave7z
  #define stEnable true
  #define noSprites 6
  #define scaling true
#endif
