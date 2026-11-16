// MDV2Config.h — MapDiscoveryV2: Hardware-Konstanten des Schwenkkopfs
//
// Gleiche Hardware wie MapDiscovery (V1, TurnBaseConfig.h). Wird vom Head als
// "../Common/MDV2Config.h" eingebunden.
//
// Verdrahtung Servo-Bus (Half-Duplex-UART):
//   TX --[1kΩ]--+-- SERVO BUS DATA
//   RX ----------+
#pragma once

// ── Servos ──────────────────────────────────────────────────────────────────
#define MDV2_SERVO_BAUD     1000000
#define MDV2_PAN_ID         2          // Feetech ST3020, 4096 Ticks / 360°
#define MDV2_PAN_ORIGIN     2048
#define MDV2_TILT_ID        4

// Tilt-Servotyp: genau einen aktiv lassen
#define MDV2_TILT_SC        // SC15:   1024 Ticks / 180°, Origin 680
//#define MDV2_TILT_ST      // ST3020: 4096 Ticks / 360°, Origin 2048

#if defined(MDV2_TILT_SC)
  #define MDV2_TILT_ORIGIN       680
  #define MDV2_TILT_TICKS_PER_DEG (1024.0f / 180.0f)
  #define MDV2_TILT_TICKS_MAX    1023
#elif defined(MDV2_TILT_ST)
  #define MDV2_TILT_ORIGIN       2048
  #define MDV2_TILT_TICKS_PER_DEG (4096.0f / 360.0f)
  #define MDV2_TILT_TICKS_MAX    4095
#else
  #error "Tilt-Servotyp fehlt: MDV2_TILT_SC oder MDV2_TILT_ST setzen"
#endif

#define MDV2_PAN_TICKS_PER_DEG (4096.0f / 360.0f)

// ── Pins (je nach Board automatisch) ────────────────────────────────────────
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define MDV2_SERVO_RX   2
  #define MDV2_SERVO_TX   1
  #define MDV2_RANGER_RX  7
  #define MDV2_RANGER_TX  8
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #define MDV2_SERVO_RX   17
  #define MDV2_SERVO_TX   16
  #define MDV2_RANGER_RX  26
  #define MDV2_RANGER_TX  25
#endif

// ── Laser-Marker (CatFind LaserMarker 6_3) ──────────────────────────────────
#define MDV2_MARKER_IP     192, 168, 0, 182
#define MDV2_MARKER_PORT   23456
