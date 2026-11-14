// EPF1 - Hardware-Konfiguration (gemeinsam fuer Test-Sketch und spaetere Firmware)
//
// EPF1 = Drehturm (Pan/Tilt, Feetech-Bus-Servos) mit Elektronik der LaserMarker-
// Bauart: ESP32-C3 Super Mini. Schema: Schema_EPF1.pdf (gespiegelt gescannt).
// Mechanik/Servos wie MapDiscovery (TurnBaseConfig.h), aber Tilt ist hier ein
// ST-Servo. Die Umschaltung TILT_SERVO_ST / TILT_SERVO_SC bleibt erhalten, damit
// der MapDiscovery-Kopf (SC15-Tilt) spaeter als EPF1b laufen kann.
#pragma once

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
// Verdrahtung wie MapDiscovery: TX --[1kΩ]--+-- Servo-Datenleitung (Kabel GE)
//                               RX ---------+
#define servoBusSpeed   1000000
#define SERVO_TX_PIN    20            // GPIO20 (ueber 1 kΩ)
#define SERVO_RX_PIN    4             // GPIO4  (direkt)
#define panServoID      2             // ST3020, wie MapDiscovery
#define panServoOrigin  2048
#define PAN_TICKS_PER_DEG (4096.0f / 360.0f)
#define tiltServoID     4
// Grenzen aus MapDiscovery - am EPF1 nach dem ersten Test pruefen/anpassen
#define PAN_LIMIT_DEG   170.0f
#define TILT_LIMIT_DEG  30.0f

// ── Ausgaenge (Pins identisch zum LaserMarker, LaserMarker6_3/hwDef.h) ─────────
#define mainLaserPin 3                // GPIO3  -> TTL-Laser-Modul (IN), HIGH = an
#define subLaserPin  21               // GPIO21 -> BS170-MOSFET -> Laserdiode (5 V), HIGH = an
                                      //   ACHTUNG: GPIO21 = U0TXD - das ROM-Bootlog
                                      //   laesst die Laserdiode beim Booten kurz flackern
#define auxPin       7                // GPIO7  -> AUX-Stecker (gegen GND), HIGH = an
#define pixelPin     10               // GPIO10 -> 1x WS2812 (5 mm, IN/VCC/GND/OUT)

// ── I2C (Kabel BR = SDA, GR = SCL) - fuer Erweiterungen am Turm ───────────────
#define pinSDA 0                      // GPIO0
#define pinSCL 1                      // GPIO1

// Strapping-Pins GPIO2/8/9 sind frei - nicht beschalten.
