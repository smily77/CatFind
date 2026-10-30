// hwDef.h -- PA2i6_3_0, PowerActor2 nach dem Umbau (Hardware wie Development/
// AfterSerialRebuild/Step5_RelaysIR). ESP32 classic, feste IP .181, Hostname PowerActor2.

#define periodeForHB 5000
#define MC_QUEUE_LEN 16           // catDetected/catObserved/fireInhibit nicht verdraengen

// --- Servo-Bus (Feetech ST3020, ID 1, 1 Mbit/s) ---------------------------------------
// TX GPIO19 ueber 1 kOhm, RX GPIO18 direkt auf die Datenleitung.
// Position 0..4095 = -180..+180 Grad um 2048 = 0-Grad-Linie des Traegers (nach vorn),
// steigend = nach rechts. Das ist direkt der PA-Winkel (toPaPol: 2048 = geradeaus).
#define SERVO_RX_PIN   18
#define SERVO_TX_PIN   19
#define SERVO_BAUD     1000000
#define SERVO_ID       1
#define SERVO_MIN      0
#define SERVO_MAX      4095
#define SERVO_CENTER   2048
#define SERVO_FAST     0          // Geschwindigkeit 0 = Maximum
#define AIM_TOL        25         // so nah am Soll = ausgerichtet (Servo bleibt 10..20 davor stehen)
#define AIM_MAX_MS     1500       // spaetestens dann gilt er als ausgerichtet

// --- LP40B (DFRobot SEN0586) an Serial2 ----------------------------------------------
#define LIDAR_RX_PIN   16
#define LIDAR_TX_PIN   17
#define LIDAR_BAUD     115200
#define LIDAR_STALE_MS 300        // juengere Messung gilt als aktuell

// --- Ausgaenge (alle aktiv HIGH) ---
#define laserPin   25             // Ziellaser: beim Schuss gleich wie das Ventil (Wasserhahn zu = Trockentest)
#define valvePin   27             // Wasserventil
#define lidarPwrPin 26            // Speisung LD06
#define radarPwrPin 23            // Speisung Compact Dome
#define spotPin    32             // Spot (heute ungenutzt, bleibt AUS)

// --- IR-Positionskamera DFRobot SEN0158 (I2C) ---
#define IR_SDA_PIN    21
#define IR_SCL_PIN    22
#define IR_ADDR       0x58
#define IR_POINTS     4
#define IR_NONE       1023
#define IR_PERIOD_MS  50

// --- Schuss -----------------------------------------------------------------------------
#define FIRE_MS        500        // Wasserstoss / Laser
#define WATER_RANGE_MM 6500       // Reichweite des Wasserstrahls
#define NEAR_MM        500        // naeher nicht schiessen
#define TRACK_LOST_MS  2500       // so lange ohne catTracked = Track verloren
#define ENGAGE_MAX_MS  60000      // laenger verfolgt er einen Track nicht
#define REFIRE_MS      3000       // nach einem Schuss so lange kein neuer (gleicher Track)
#define LASER_HOLD_MS  400        // Laser-Folgemodus: Laser aus, wenn so lange kein catObserved
#define CATDET_DUP_MS  1500       // CatIdent sendet doppelt

// Veranda: Linie y = 0 von x = 3,0 bis 8,0 m (Welt-mm), dahinter (y < 0) die Veranda.
// Steht der Aktor auf der Veranda (y < 0, 3 < x < 8 m), darf der Strahl NUR durch diese
// Oeffnung gehen (sonst Wand nass); steht er woanders, darf er sie NICHT kreuzen.
#define VERANDA_X0  3000
#define VERANDA_X1  8000
#define VERANDA_Y   0
#define VERANDA_MARGIN 300        // Sicherheitsrand (Strahlbreite/Streuung): von der Veranda aus
                                  // muss der Strahl so weit innerhalb der Oeffnung durch, sonst
                                  // so weit neben ihr vorbei

// --- Traegerpose (poseProc.ino): LP40B-Rundumschwenk + LD06 -> VPS /localize_carrier ---
#define POSE_START_DELAY_MS  8000   // nach dem Boot: LD06 (am Relais) hochfahren lassen
#define POSE_SWEEP_LO        30     // Servo-Enden des Schwenks (bleibt 10..20 davor stehen)
#define POSE_SWEEP_HI        4065
#define POSE_SWEEP_SPEED     250    // Servo-Schritte/s: ~16 s Schwenk, ~0,5 Grad je LP40B-Messung
#define POSE_SWEEP_MAX_MS    30000
#define POSE_LP_MIN_MM       500    // naeher = Turm/Traeger selbst
#define LP40B_OFFSET_MM      15     // LP40B sitzt 1,5 cm vor der Drehachse (auf dem Strahl)
#define POSE_RETRY_MS        300000UL   // ohne Pose: neuer Versuch nach 5 min
#define POSE_HTTP_TIMEOUT_MS 60000
#define POSE_VPS_PATH        "/localize_carrier"
// LD06 im Traeger (Step6_CalibTool/calibration.json): Traeger = R(rot) * (x, y) + (0, off)
#define LD06_ROT_DEG         177.52f
#define LD06_OFF_MM          135
#define LD06_REQ_PORT        4320   // Rohdaten-Strom wie das Kalibrier-Tool
#define LD06_DATA_PORT       4321
#define LD06_SCAN_MS         5000   // so lange Rohdaten sammeln (ab dem ersten Paket)
#define LD06_SPINUP_MS       1000   // erste Sekunde verwerfen (Motor laeuft an)
#define LD06_MAX_WAIT_MS     30000  // so lange auf das erste Paket warten: nach dem Einschalten
                                    // lokalisiert sich der LD06 gerade selbst (HTTP blockiert)
#define LD06_WAIT_IP_MS      15000  // LD06 noch nicht gehoert -> so lange warten, dann ohne
#define LD06_POSE_TRIES      10     // Pose-Uebergabe an den LD06: Versuche ...
#define LD06_POSE_CHECK_MS   5000   // ... in diesem Abstand (seine eigene Lokalisierung blockiert ~20 s)
#define LD06_MIN_QUALITY     150
#define LD06_MIN_MM          400

// --- No-Shot-Karte (zusaetzliche Pruefung im Aktor) ---
#define NOSHOT_PATH          "/noshot.csv"
#define MAP_WAIT_MS          4000
#define MAP_RETRY_MS         60000
#define MAP_RECHECK_MS       3600000UL
#define MAP_ANNOUNCE_SLOT_MS 300

// --- Einstellungen ------------------------------------------------------------------------
// stgLidarMotor = Speisung LD06, stgRadarPower = Speisung Compact Dome (beide Default AN,
// persistiert). stgLaserFollow = Test: Laser folgt jedem catObserved (Default AUS, nicht
// persistiert).
#define STG_SUPPORTED ((1u<<stgLidarMotor)|(1u<<stgRadarPower)|(1u<<stgLaserFollow))
#define STG_DEFAULT   ((1u<<stgLidarMotor)|(1u<<stgRadarPower))
#define STG_PERSIST   ((1u<<stgLidarMotor)|(1u<<stgRadarPower))
#define STG_ACTIONS   (1u<<actCalibrate)   // "Kalibrieren" = Traegerpose neu bestimmen
