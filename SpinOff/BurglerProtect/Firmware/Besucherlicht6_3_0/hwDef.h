// Besucherlicht (Geraet 22, Spin-off BurglerProtect): M5Stack AtomS3 (ESP32-S3,
// 0,85"-Display 128x128, Knopf unter dem Display). Konzept:
// SpinOff/BurglerProtect/Dokumentation/Konzept.md
#define ID Besucherlicht

#define periodeForHB 10000

// Der Dome-C1 meldet einen Besucher mit ~10 visitorObserved/s, dazu kommt der
// uebrige Busverkehr - Queue statt Einzelpuffer, damit nichts verdraengt wird.
#define MC_QUEUE_LEN 32

// --- Einstellungen & Steuerung (settingsPayload / xComProc initSettings) ---------------
// stgArmed = scharf: Knopf am Geraet schaltet um, VPS setzt absolut (cmdSetSetting).
// Persistiert: der Zustand ueberdauert einen Stromausfall (User-Entscheid 2026-10-28).
// Aktion "Parameter laden" holt Polygon + Regel + Hue-Gruppen neu vom VPS.
#define STG_SUPPORTED (1u<<stgArmed)
#define STG_DEFAULT   0u
#define STG_PERSIST   (1u<<stgArmed)
#define STG_ACTIONS   (1u<<actReloadParams)

// Standort fuer die Sonnenuntergangs-Rechnung (Herisau AR)
#define SITE_LAT 47.3857
#define SITE_LON 9.2794

#define CFG_PATH           "/visitor.cfg"     // Cache der VPS-Konfiguration (LittleFS)
#define CFG_FETCH_MS       900000UL           // alle 15 min beim VPS nachsehen
#define CFG_TIMEOUT_MS     6000

#define HUE_TIMEOUT_MS     3000
#define HUE_POLL_MS        5000               // solange das Licht "mir gehoert"
#define HUE_IDLE_CHECK_MS  300000UL           // sonst alle 5 min Bridge pruefen (Anzeige)
#define HUE_REF_DELAY_MS   3000               // nach dem Einschalten: Referenzwerte lesen
#define HUE_HOME_HOLD_MS   30000              // "jemand zuhause" so lange nicht neu fragen
#define HUE_OFF_RETRY_MS   1800000UL          // Ausschalten so lange wiederholen, dann aufgeben
#define QUIET_REARM_MS     30000              // nach Handabschaltung: Polygon so lange leer
#define TRACK_SLOTS        8
#define WIN_SLOTS          64                 // Obergrenze fuer min_events
#define MAX_GROUPS         8
#define MAX_POLY           32
