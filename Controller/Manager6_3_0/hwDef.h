#define hostName "Manager_Dev"
#define periodeForHB 10000

#define pixelPin 16 
#define pixelNum 24
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB

uint8_t BRIGHTNESS = 5;
#define minPix 0
#define maxPix 230

#define HB_blinkPeriode 1
#define Alarm_blinkPeriode 500

// --- Einstellungen & Steuerung (settingsPayload / xComProc initSettings) ---------------
// Spezialfall Manager: Anzeige beim EMPFANG. HB-Empfang (gruen), catObserved-Empfang
// (blau) und catDetected-Empfang (ROT, vom Cat Identifier) je ein-/ausschaltbar,
// im NVS gespeichert. Keine Automatik/Aktionen.
#define STG_SUPPORTED ((1u<<stgHbLed)|(1u<<stgCatLed)|(1u<<stgCatDetLed))
#define STG_DEFAULT   STG_SUPPORTED
#define STG_PERSIST   STG_SUPPORTED
#define STG_ACTIONS   0u
