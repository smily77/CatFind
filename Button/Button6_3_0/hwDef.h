
#define periodeForHB 10000          // eigener HB -> Manager/VPS sehen den Button

#define MC_QUEUE_LEN 16              // HBs der Aktoren + fireInhibit nicht verlieren

#define PA_STALE_HBS   3             // so viele HB-Perioden ohne HB -> Aktor gilt als weg
#define PA_STALE_MIN   15000         // ... mindestens aber so lange (ms)
#define ARM_RETRY_MS   1000          // Kommando wiederholen, bis die HBs es bestaetigen
#define ARM_TRIES      4
#define NOTE_SHOW_MS   1500          // Fehler-Anzeige (magenta) so lange

#ifdef Atom3S
  #include <M5Unified.h>
#else
  #include <M5Atom.h>
  #define containLed
  #define pixelPin 27
  #define pixelNum 25                // Atom Matrix; beim Atom Lite leuchtet nur Pixel 0
  #define LED_TYPE    WS2811
  #define COLOR_ORDER GRB
  CRGB leds[pixelNum];
#endif
uint8_t BRIGHTNESS = 20;
#define minPix 0
#define maxPix 26

// Anzeige
#define colUnknown 0xFFFFFF          // weiss:   noch kein Aktor gesehen
#define colArmed   0xFF0000          // rot:     alle Aktoren scharf
#define colSafe    0x00FF00          // gruen:   alle gesichert
#define colMixed   0xFF6000          // orange:  gemischt
#define colInhibit 0xFFC000          // gelb:    Personensperre aktiv (Aktoren feuern nicht)
#define colError   0xFF00FF          // magenta: Kommando nicht bestaetigt / kein Aktor
#define WIFI_BLINK_COLOR 0x0000FF    // blau: WLAN-Suche beim Start (rot = scharf)
