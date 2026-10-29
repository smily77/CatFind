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

  // Nur beim Dome strahlen die beiden Pixel nach vorne raus -> als Leuchte nutzbar
  #define hasFrontLight

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

  #define periodeForHB 5000
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
  #define deadZone 500

#elif defined DomeC1Device
  // Aufbau wie CompactDome (ESP32, HLK-2450 an GPIO19, ein Pixel an GPIO27),
  // zusaetzlich BMP180 (I2C) und IR-Scheinwerfer-Sperre an GPIO18.
  #define ID DomeC1

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
  #define deadZone 500

  // BMP180 Temperatur/Luftdruck (I2C-Adresse 0x77) -> envReport
  #define hasBMP180
  #define BMP_SDA 21
  #define BMP_SCL 22
  // IR-Scheinwerfer (Pi-Cam-Typ): GPIO18 liegt parallel zum LDR des Scheinwerfers und
  // arbeitet als Open-Drain-Ausgang. Leitet er (LOW), sieht der Scheinwerfer "hell" und
  // bleibt aus; hochohmig (HIGH) entscheidet der LDR wie ohne Steuerung (an bei Nacht).
  #define hasIrLight
  #define IR_PIN 18

#endif

// Versatz-Slot nach einem mapInfo-Announce (x MAP_ANNOUNCE_SLOT_MS, siehe Radar6_3_0.ino).
// Fest je Variante statt ID % 10: DomeC1 (21) fiele sonst auf denselben Slot wie Dome (1).
// Belegt: 1-3 + 4 hier, LidarC1 7, CatIdent 8.
#if defined DomeDevice
  #define MAP_SLOT 1
#elif defined MiniDomeDevice
  #define MAP_SLOT 2
#elif defined CompactDomeDevice
  #define MAP_SLOT 3
#elif defined DomeC1Device
  #define MAP_SLOT 4
#endif

#define LED_TYPE WS2811

// --- Einstellungen & Steuerung (settingsPayload / xComProc initSettings) ---------------
// Anzeige: HB-Blitz und Target-LED ein-/ausschaltbar. Automatik: Welt-Pose aus der Gruppe
// uebernehmen und (Radar-Spezialfall) automatische Co-Observation-Kalibrierung.
// Aktionen: Pose jetzt kopieren, Kalibrierung jetzt ausloesen.
// stgRadarFullRasen NICHT in STG_DEFAULT/STG_PERSIST: Default AUS (NoShot-gefiltert,
// ruhiger Bus) und nicht persistiert - nach jedem Reboot wieder der ruhige Normalbetrieb,
// unabhaengig davon, was vor dem letzten Neustart eingestellt war (s. Radar6_3_0.ino).
// stgActive (Ruhemodus) unterstuetzt, Default AN, NICHT persistiert: nach Reboot aktiv.
// IR-Scheinwerfer (nur Varianten mit hasIrLight): stgIrLight + stgIrAuto bilden den
// Schalter AUS / EIN / AUTO (siehe xComDef6_3.h), beide persistiert, Default AUTO.
#ifdef hasIrLight
  #define STG_IR ((1u<<stgIrLight)|(1u<<stgIrAuto))
#else
  #define STG_IR 0u
#endif
// Front-Licht (nur Dome): stgLight + stgLightAuto bilden den Schalter AUS / EIN / AUTO,
// beide persistiert, Default AUS (nicht im STG_DEFAULT).
#ifdef hasFrontLight
  #define STG_LIGHT ((1u<<stgLight)|(1u<<stgLightAuto))
#else
  #define STG_LIGHT 0u
#endif
#define STG_SUPPORTED ((1u<<stgHbLed)|(1u<<stgCatLed)|(1u<<stgAutoCopyPose)|(1u<<stgAutoCalib)|(1u<<stgRadarFullRasen)|(1u<<stgActive)|STG_IR|STG_LIGHT)
#define STG_DEFAULT   ((1u<<stgHbLed)|(1u<<stgCatLed)|(1u<<stgAutoCalib)|(1u<<stgActive)|STG_IR)   // Auto-Kalib + aktiv default an
#define STG_PERSIST   ((1u<<stgHbLed)|(1u<<stgCatLed)|(1u<<stgAutoCopyPose)|(1u<<stgAutoCalib)|STG_IR|STG_LIGHT)
#define STG_ACTIONS   ((1u<<actCopyPose)|(1u<<actCalibrate)|(1u<<actClearPose))

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

// Verwaltungszustand je Filterkarte (NoShot/Rasen, siehe serviceFilterMaps in
// Radar6_3_0.ino). MUSS hier im Header stehen, nicht im .ino: der Arduino-Builder
// setzt Auto-Prototypen (serviceMapSlot nimmt MapSlot&) VOR den Sketch-Code -
// ein im .ino definierter Struct waere dort noch unbekannt (gleiches Muster wie
// catTrackDef.h beim Cat Identifier).
struct MapSlot {
  bool          loaded    = false;   // dieser Slot beim letzten Versuch erfolgreich geladen?
  bool          synced    = false;   // ... UND dabei mit dem Manager abgeglichen (mapLastSynced).
                                     // Nur dann darf bis zum stuendlichen Fangnetz gewartet werden -
                                     // sonst faellt eine verlorene Anfrage erst nach 1 h auf.
  unsigned long lastTry   = 0;       // letzter Beschaffungsversuch (0 = noch keiner)
  bool          recheckNow = false;  // per Announce (mapInfo) sofortiger Re-Check angefordert
  unsigned long recheckAt  = 0;      // fruehester Zeitpunkt dafuer (Announce-Versatz je Geraet)
};
