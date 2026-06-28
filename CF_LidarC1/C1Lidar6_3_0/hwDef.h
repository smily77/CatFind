// hwDef.h -- C1Lidar6_3_0 (RPLidar C1, welt-faehiger CatFinder-Sensor)

// --- Hardware ---
#define LED_PIN        9          // 2x WS2812 Status-Pixel
#define pixelPin       LED_PIN
#define pixelNum       2
#define LED_TYPE       WS2812
#define COLOR_ORDER    RGB
#define BRIGHTNESS     80
#define minPix         0
#define maxPix         1

#define LIDAR_RX_PIN   7          // Lidar TX (gelb) -> GPIO7
#define LIDAR_TX_PIN   8          // Lidar RX (gruen) -> GPIO8
#define LIDAR_BAUD     RPLIDAR_BAUD_C1   // 460800

// --- Hintergrund-/Detektionsmodell ---
#define NUM_BINS       360        // 1 Grad Aufloesung
#define MAX_RANGE_MM   12000.0f   // C1 Reichweite; auch "open/out of range"
#define CAL_TIME_MS    20000UL    // 20 s Hintergrund lernen
#define MIN_CAL_SAMPLES 5         // Mindest-Treffer je Bin
#define PERIM_MARGIN_MM 300.0f    // Ziel muss >= 0.3 m naeher als der Hintergrund sein
#define PERIM_MAX_GAP_DEG 25      // Luecken bis hierher interpolieren, sonst "open"
#define NEAR_LIMIT_MM  120.0f     // naechstes Sensorrauschen verwerfen
#define MIN_DETECT_POINTS 3       // so viele markierte Punkte je Umdrehung noetig
#define DETECT_HOLD_MS 800UL      // "detektiert" so lange halten

// --- VPS-Lokalisierung ---
#define VPS_PORT       8080
#define VPS_PATH       "/localize"
#define VPS_TIMEOUT_MS 20000      // HTTP-Timeout fuer die Pose-Anfrage
#define LOC_PLAUSIBLE_MM 600.0f   // NVS-Pose gilt als bestaetigt, wenn so nah an der VPS-Pose
#define LOC_PLAUSIBLE_DEG 8.0f    // ... und Heading so nah

// --- No-Shot-Karte ---
#define NOSHOT_PATH    "/noshot.csv"
#define MAP_WAIT_MS    4000        // so lange auf mapInfo/Chunks vom Manager warten

// --- Kommunikation ---
#define periodeForHB   10000       // HB-Periode (ms)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (float)(M_PI/180.0)
#define RAD2DEG (float)(180.0/M_PI)
