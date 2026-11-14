// EPF1 - Hardware-Konfiguration (gemeinsam fuer Test-Sketch und Bus-Firmware)
//
// EPF1 = Drehturm (Pan/Tilt, Feetech-Bus-Servos) = "Turn Base Compact" aus MapDiscovery,
// Elektronik Seeed XIAO ESP32-S3. Schema: Schema_EPF1.pdf (gedreht gescannt).
// Pins wie MapDiscovery/TurnBaseConfig.h (ESP32-S3-Zweig). Unterschied: der Tilt ist
// hier ein ST-Servo. Die Umschaltung TILT_SERVO_ST / TILT_SERVO_SC bleibt erhalten,
// damit der MapDiscovery-Kopf (SC15-Tilt) spaeter als EPF1b laufen kann.
//
// Board: esp32:esp32:XIAO_ESP32S3
#pragma once

// ── Betriebsart (was gerade auf dem Turm sitzt) ───────────────────────────────
// Genau eine aktiv lassen. Jede Betriebsart steckt in einer eigenen Datei
// EPF1_6_3_0/mode<Name>.ino; der Kern (Bus, Servos, Pose, Kommandos) ist gemeinsam.
//   EPF1_MODE_LIDAR -> TOFSense-F am Slave-Port: Rundumscan -> Welt-Pose (VPS /localize)
#define EPF1_MODE_LIDAR

// ── Tilt-Servo-Typ ────────────────────────────────────────────────────────────
// Genau einen aktiv lassen:
//   TILT_SERVO_ST -> ST-Servo (ST3020/ST3215) - 4096 Ticks/360°, Origin 2048  (EPF1)
//   TILT_SERVO_SC -> SC-Servo (SC15)          - 1024 Ticks/180°, Origin 680   (EPF1b = MapDiscovery-Kopf)
#define TILT_SERVO_ST
//#define TILT_SERVO_SC

#if defined(TILT_SERVO_ST) && defined(TILT_SERVO_SC)
  #error "TILT_SERVO_ST und TILT_SERVO_SC gleichzeitig gesetzt - nur einen aktiv lassen"
#elif defined(TILT_SERVO_SC)
  #define tiltServoOrigin  680
  #define TILT_TICKS_PER_DEG (1024.0f / 180.0f)
  #define TILT_TICKS_MAX     1023
#elif defined(TILT_SERVO_ST)
  #define tiltServoOrigin  2048
  #define TILT_TICKS_PER_DEG (4096.0f / 360.0f)
  #define TILT_TICKS_MAX     4095
#else
  #error "Tilt-Servo-Typ nicht definiert: TILT_SERVO_ST oder TILT_SERVO_SC in EPF1Config.h setzen"
#endif

// ── Servo-Bus (Feetech, Half-Duplex, 1 Mbit/s) ────────────────────────────────
// TX --[1kΩ]--+-- Servo-Datenleitung (Turmkabel BR "Servo", Slave-Port WS)
// RX ---------+
#define servoBusSpeed   1000000
#define SERVO_TX_PIN    1             // GPIO1 / D0 (ueber 1 kΩ)
#define SERVO_RX_PIN    2             // GPIO2 / D1 (direkt)
#define panServoID      2             // ST3020, wie MapDiscovery
#define panServoOrigin  2048
#define PAN_TICKS_PER_DEG (4096.0f / 360.0f)
#define tiltServoID     4
// Grenzen aus MapDiscovery - am EPF1 nach dem ersten Test pruefen/anpassen
#define PAN_LIMIT_DEG   170.0f
#define TILT_LIMIT_DEG  30.0f
// Physisches Pan-Fenster (Kabel!) in absoluten Servo-Ticks - unabhaengig davon, wohin
// die 0 per "NULL HIER" verschoben wurde.
#define PAN_PHYS_MIN    ((int)(panServoOrigin - PAN_LIMIT_DEG * PAN_TICKS_PER_DEG))
#define PAN_PHYS_MAX    ((int)(panServoOrigin + PAN_LIMIT_DEG * PAN_TICKS_PER_DEG))

// ── Stell-Protokoll (UDP, kein xCom) fuer das CYD-Bediengeraet EPF1_Stell ──────
// Das CYD sendet an Broadcast oder die gelernte IP, EPF1 antwortet an den Absender.
#define STELL_PORT      4330
#define STELL_MAGIC     0xE1
enum : uint8_t { ST_POLL = 0, ST_PAN = 1, ST_ZERO_HERE = 2, ST_GO_ZERO = 3, ST_SCAN = 4 };
struct __attribute__((packed)) StellCmd   { uint8_t magic, type; uint16_t value; };   // ST_PAN: absolute Ticks
struct __attribute__((packed)) StellState {
  uint8_t  magic;
  uint8_t  servoOk;       // Pan antwortet
  uint16_t panGoal;       // absolute Ticks
  uint16_t panPos;        // absolute Ticks (Ist)
  uint16_t panOrigin;     // aktuelle 0 in Ticks
  uint8_t  tempC;
  uint8_t  busy;          // 1 = Scan laeuft
  uint8_t  poseValid;
  int32_t  poseX, poseY;  // mm
  uint16_t poseHead;      // PA-Einheiten 0..4095
  int8_t   poseMir;
};

// ── Slave-Port / Turmkabel "SIS" (GPIO7/8) ────────────────────────────────────
// Fuer den Aufsatz: MapDiscovery betrieb hier den SEN0366 per UART (ESP-RX 7, ESP-TX 8).
// Im Schema als SCL/SDA beschriftet -> UART ODER I2C, je nach Aufsatz.
#define SLAVE_PIN_A     7             // GPIO7 / D8: UART-RX (Sensor-TX)  bzw. I2C
#define SLAVE_PIN_B     8             // GPIO8 / D9: UART-TX (Sensor-RX)  bzw. I2C

// ── I2C-AUX-Stecker (GPIO5/6) ─────────────────────────────────────────────────
#define AUX_PIN_SDA     5             // GPIO5 / D4  (Zuordnung SDA/SCL aus dem Schema, pruefen)
#define AUX_PIN_SCL     6             // GPIO6 / D5

// ── Status-Pixel ──────────────────────────────────────────────────────────────
#define pixelPin        4             // GPIO4 / D3 -> 1x WS2812 (5 mm), aus dem Schema gelesen
