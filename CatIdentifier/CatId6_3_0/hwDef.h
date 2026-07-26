// Cat Identifier: ESP32-DevKit (classic), keine Pixel-Kette - nur die Onboard-LED.
// Konsumiert catObserved vom Bus, laesst das Erkennungsmodell in Echtzeit laufen
// und broadcastet catDetected (doppelt, UDP-Verlustschutz).
#define ID CatIdent

#define periodeForHB 10000

// Onboard-LED: kurzer Blitz beim HB-Senden (stgHbLed), laenger an bei CatDetected
// (stgCatLed). Hardware ist ein Seeed XIAO ESP32-S3: User-LED auf GPIO21,
// ACTIVE-LOW (LOW = leuchtet); WLAN NUR ueber die IPEX-Antenne (ohne angesteckte
// Antenne ist das Board praktisch taub!). Kein FastLED noetig.
#define CATID_LED     21
#define CATID_LED_ON  LOW
#define CATID_LED_OFF HIGH
#define HB_FLASH_MS 60
#define DET_FLASH_MS 1500

// --- Einstellungen & Steuerung (settingsPayload / xComProc initSettings) ---------------
// Anzeige: HB-Blitz + CatDetected-Blitz auf der Onboard-LED, je schaltbar (NVS).
// Aktion: Modell-Parameter neu vom VPS laden (cmdReloadParams) - fuer Modell-Iterationen
// ohne Neuflash.
// stgActive (Ruhemodus) unterstuetzt, Default AN, NICHT persistiert (nach Reboot aktiv).
#define STG_SUPPORTED ((1u<<stgHbLed)|(1u<<stgCatLed)|(1u<<stgActive))
#define STG_DEFAULT   STG_SUPPORTED
#define STG_PERSIST   ((1u<<stgHbLed)|(1u<<stgCatLed))   // stgActive bewusst nicht persistiert
#define STG_ACTIONS   (1u<<actReloadParams)
