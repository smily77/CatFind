// hwDef.h -- LD06_6_3_0 (LDROBOT LD06 auf dem PowerActor2-Traeger, welt-faehiger Sensor)

// --- allg. HW (Ausgaenge werden nur sicher AUS gehalten) ---
#define extRelais 18
#define lidarPwr  27              // schaltet die LD06-Versorgung
#define laser     4

// --- Lidar ---
#define dataInPin 32              // LD06 TX -> Serial2 RX, 230400 Baud
#define LIDAR_BAUD 230400
#define dataPointPerSentence 12
#define sentenceLength 47         // 0x54 0x2C, Drehzahl, Startwinkel, 12x3, Endwinkel, Zeit, CRC
struct messDaten {
  int   quality;
  int   dist;                     // mm
  float winkel;                   // Grad, GEGEN den Uhrzeigersinn (360 - LD06-Rohwinkel)
};

// --- Pixelring (29 x WS2811) ---
#define containLed
#define COLOR_ORDER GRB
#define LED_TYPE    WS2811
#define pixelNum    29
#define pixelPin    23
#define minPix      0
#define maxPix      30            // > pixelNum: setUpWifi blinkt dann den ganzen Ring
#define BRIGHTNESS  40
// Ring-Ausrichtung: auf welchen LD06-Winkel (Grad, gegen den Uhrzeigersinn) zeigt
// Pixel 0, und laufen die Pixelnummern mit (+1) oder gegen (-1) den Winkel?
// Bestimmt 2026-10-29: der Drehturm liegt bei ~90 Grad (15-17 cm), Pixel 0 zeigt vom
// Turm weg (~270 Grad), Pixel 1 liegt von oben gesehen gegen den Uhrzeigersinn daneben
// = Pixelnummern laufen mit dem Winkel. Zum Nachpruefen RING_TEST definieren: beim
// Lernen leuchtet dann Pixel 0 weiss und Pixel 1 gruen.
#define RING_PIX0_DEG 270.0f
#define RING_DIR      1

// --- Hintergrund-/Detektionsmodell (wie LidarC1) ---
#define NUM_BINS          360     // 1 Grad
#define MAX_RANGE_MM      12000.0f
#define CAL_TIME_MS       20000UL // 20 s Hintergrund lernen
#define MIN_CAL_SAMPLES   5
#define PERIM_MARGIN_MM   300.0f  // Ziel muss >= 0,3 m naeher als der Hintergrund sein
#define PERIM_MAX_GAP_DEG 25
#define NEAR_LIMIT_MM     400.0f  // Drehturm (15-17 cm hinter dem LD06) und Traeger ausblenden
#define MIN_QUALITY       150     // schwaechere Echos verwerfen (Sonne/Stoerungen; indoor alle >= 200)
#define MIN_DETECT_POINTS 3       // so viele markierte Punkte je Umdrehung noetig
#define DETECT_HOLD_MS    800UL   // Richtungsanzeige so lange halten

// --- VPS-Lokalisierung ---
#define VPS_LOC_PATH       "/localize"
#define VPS_LOC_TIMEOUT_MS 20000
#define LOC_MIN_INLIER     0.60f   // neue Pose nur bei conf=HOCH und inlier >= diesem Wert
#define LOC_RETRY_MS       300000UL // ohne gueltige Pose: alle 5 min neu lernen + lokalisieren

// --- No-Shot-Karte ---
#define NOSHOT_PATH          "/noshot.csv"
#define MAP_WAIT_MS          4000
#define MAP_RETRY_MS         60000
#define MAP_RECHECK_MS       3600000UL
#define MAP_ANNOUNCE_SLOT_MS 300

// --- Kommunikation ---
#define periodeForHB 10000
#define HB_BLINK_MS  150

// --- Einstellungen & Steuerung ---------------------------------------------------------
// Lidare dienen vor allem der Pose-Bestimmung (Sonne/Stoerungen, nachts schwach fuer
// Katzen): nach dem Boot lernt der LD06 den Hintergrund, lokalisiert sich und geht dann
// in Ruhe (stgActive AUS = Motor aus, nur HB). Zielerkennung bei Bedarf einschalten
// (stgActive AN: Hintergrund neu lernen, neu lokalisieren, dann catObserved).
// stgActive startet hier AUS und ist nicht persistiert: nach jedem Boot nur Pose.
// Aktion "Kalibrieren" (cmdCalibrate) = Hintergrund neu lernen + Pose neu bestimmen.
#define STG_SUPPORTED ((1u<<stgHbLed)|(1u<<stgCatLed)|(1u<<stgActive))
#define STG_DEFAULT   ((1u<<stgHbLed)|(1u<<stgCatLed))
#define STG_PERSIST   ((1u<<stgHbLed)|(1u<<stgCatLed))
#define STG_ACTIONS   (1u<<actCalibrate)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (float)(M_PI/180.0)
#define RAD2DEG (float)(180.0/M_PI)
