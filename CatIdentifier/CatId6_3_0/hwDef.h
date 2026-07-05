// Cat Identifier: ESP32-DevKit (classic), keine Pixel-Kette - nur die Onboard-LED.
// Konsumiert catObserved vom Bus, laesst das Erkennungsmodell in Echtzeit laufen
// und broadcastet catDetected (doppelt, UDP-Verlustschutz).
#define ID CatIdent

#define periodeForHB 10000

// Onboard-LED: kurzer Blitz beim HB-Senden (stgHbLed), laenger an bei CatDetected
// (stgCatLed). ESP32-S3-DevKit hat eine RGB-LED (RGB_BUILTIN, digitalWrite = weiss),
// klassisches DevKit eine einfache LED auf GPIO2. Kein FastLED noetig.
#ifdef RGB_BUILTIN
#define CATID_LED RGB_BUILTIN
#else
#define CATID_LED 2
#endif
#define HB_FLASH_MS 60
#define DET_FLASH_MS 1500

// --- Einstellungen & Steuerung (settingsPayload / xComProc initSettings) ---------------
// Anzeige: HB-Blitz + CatDetected-Blitz auf der Onboard-LED, je schaltbar (NVS).
// Aktion: Modell-Parameter neu vom VPS laden (cmdReloadParams) - fuer Modell-Iterationen
// ohne Neuflash.
#define STG_SUPPORTED ((1u<<stgHbLed)|(1u<<stgCatLed))
#define STG_DEFAULT   STG_SUPPORTED
#define STG_PERSIST   STG_SUPPORTED
#define STG_ACTIONS   (1u<<actReloadParams)
