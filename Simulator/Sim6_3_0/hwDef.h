// hwDef.h -- Sim6_3_0 (M5 Cardputer: Rekorder/Player fuer catObserved-Szenen)

// --- SD-Karte (Cardputer-Pinout) ---
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

// --- Szenen-Dateien ---
#define NUM_SLOTS       9             // /scene1.bin .. /scene9.bin (Tasten 1-9)
#define LEGACY_PATH     "/data.bin"   // alte Einzeldatei -> wird beim Boot Szene 1

// --- Bedienung / Anzeige ---
#define PLAY_GAP_MS        20         // Abstand zwischen zwei Playback-Records
#define DISP_BRIGHT        140        // Display-Helligkeit im Betrieb
#define DISPLAY_TIMEOUT_MS 120000UL   // nach 2 min ohne Taste: Display dunkel
#define DELETE_CONFIRM_MS  3000UL     // Loeschen: 2. 'd'-Druck innerhalb dieser Zeit
#define NTP_TIMEOUT_MS     10000UL    // NTP-Sync max. so lange versuchen (mobil!)
#define WIFI_CHECK_MS      5000UL     // Intervall der WLAN-Ueberwachung (Langzeit)
