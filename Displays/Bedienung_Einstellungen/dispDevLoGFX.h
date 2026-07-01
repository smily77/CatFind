
#include <Arduino.h>

#if defined(CYD28)
  #include <LovyanGFX.hpp>
  #define LGFX_AUTODETECT
  #include <LGFX_AUTODETECT.hpp>

  LGFX gfx;   // Instanz für das erkannte Display
#elif defined(CYD35)

#include <LXFX_CYD35_UBS_C.h>

LGFX gfx;

#elif defined(Sunton7) //----------------------------------------------------------------------------------
#include <LXFX_Sunton_ESP32-8048S070.h>

LGFX gfx;
#elif defined(Sunton5) //----------------------------------------------------------------------------------
// Bedienung_Einstellungen ist ein Touch-Bediengeraet -> der 8-Encoder des Prototyps
// (M5_8EN) wird NICHT gebraucht und daher hier nicht eingebunden.
#include <LXFX_Sunton_ESP32-8048S050.h>

LGFX gfx;

#elif defined(M5Core2) //----------------------------------------------------------------
  #include <M5Unified.h>
  auto& gfx = M5.Display;
  
#elif defined(M5Tab5) //----------------------------------------------------------------
  #include <M5Unified.h>
  auto& gfx = M5.Display;

#elif defined(Wave7) //------------------------------------------------------------------------------------

#include <Waveshare_ESP32-S3-Touch-LCD-7.h>

LGFX gfx;
#endif
