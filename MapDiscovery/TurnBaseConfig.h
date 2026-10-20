// Wiring (ESP32):
//   TX --[1kΩ]--+-- SERVO BUS DATA
//   RX ----------+  SERVO BUS DATA

#define servoBusSpeed   1000000
#define panServoID      2
#define panServoOrigin  2048
#define tiltServoID     4

// ── Tilt-Servo-Typ ────────────────────────────────────────────────────────────
// Einen der beiden Defines aktiv lassen, den anderen auskommentieren:
//
//   TILT_SERVO_SC → SC-Servo (z.B. SC15)   — 1024 ticks/180°, Origin 680
//   TILT_SERVO_ST → ST-Servo (z.B. ST3020) — 4096 ticks/360°, Origin 2048
//
// Um auf ST-Servo umzustellen:
//   //#define TILT_SERVO_SC
//   #define TILT_SERVO_ST
#define TILT_SERVO_SC
//#define TILT_SERVO_ST

#if defined(TILT_SERVO_SC)
  #define tiltServoOrigin  680
#elif defined(TILT_SERVO_ST)
  #define tiltServoOrigin  2048
#else
  #error "Tilt-Servo-Typ nicht definiert: TILT_SERVO_SC oder TILT_SERVO_ST in TurnBaseConfig.h setzen"
#endif
// ─────────────────────────────────────────────────────────────────────────────

#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define SERVO_RX_PIN   2
  #define SERVO_TX_PIN   1
  #define sensorRxD 7
  #define sensorTxD 8
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #define SERVO_RX_PIN   17
  #define SERVO_TX_PIN   16
  #define sensorRxD 26
  #define sensorTxD 25
#else
  #error "Dieses Sketch unterstützt aktuell nur ESP32 classic und ESP32-S3"
#endif
