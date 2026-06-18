// =============================================================================
//  encoderTest6_3_1 - Hardware-Definition  (Turm-Seite, Framework: PA1_1_6_3_0)
//
//  Testprogramm fuer den Umbau des Winkelgebers AS5600 von der Stepper-Welle
//  an den Drehturm. Per #define wird zwischen beiden Montagen umgeschaltet -
//  das aendert nur den Umrechnungsfaktor AS5600-Counts -> Turm-Winkel.
// =============================================================================

#define hostName "PA1_1_encTest"
#define periodeForHB 5000

// --- WS2812 Status-Pixel (gemeinsame FastLED-Helfer via containLed) ---
#define pixelPin    25
#define pixelNum    2
#define LED_TYPE    WS2812
#define COLOR_ORDER RGB
uint8_t BRIGHTNESS = 10;
#define minPix 0
#define maxPix 1

// --- Relais & lokale Ein-/Ausgaenge (ESP32-GPIO) ---
#define water         12
#define extPower      13
#define wirelessPower 14
#define taster        33
#define reedLeft      26
#define reedRight     32
#define laser         18

// --- I2C (PCF8574 + AS5600) ---  pinSDA/pinSCL: sda/scl sind Lib-Parameternamen!
#define pinSDA 21
#define pinSCL 19
#define motorAdr 0x20

// --- PCF8574-Portbelegung -> A4988 ---
#define DIR  0
#define STEP 1
#define SLP  2
#define RST  3
#define MS3  4
#define MS2  5
#define MS1  6
#define EN   7

// --- Mikroschritt-Stufen ---
#define fullStep      1
#define halfStep      2
#define quaterStep    4
#define eighthStep    8
#define sixteenthStep 16
#define defaultMicrostep sixteenthStep

// --- Drehrichtung ---
#define dirLeft  HIGH
#define dirRight LOW

// --- Mechanik / Geometrie ---
#define stepsPerRev 200
#define pulleyTeeth 20
#define ringTeeth   130
// Mikroschritte pro Turm-Umdrehung = 200*16*130/20 = 20800

// --- Winkelbereich (wie PA2 aber +-180 Grad: maxAngle 180 -> voller Kreis) ---
#define maxAngle 180

// --- Stepper-Timing / Sicherheit ---
#define stepPulseUs 600
#define maxHomingMicrosteps 41600

// =============================================================================
//  AS5600-Montage (Kern des Umbau-Tests)
//  - auskommentiert: AS5600 auf der STEPPER-Welle (Standard). Der Geber dreht
//    pro Turm-Umdrehung 6.5x (Riemen 20:130) -> 4096*130/20 = 26624 Counts/Turm-U.
//  - definiert (AS5600_ON_TOWER): AS5600 direkt am DREHTURM. 1x pro Turm-U
//    -> 4096 Counts/Turm-Umdrehung.
// =============================================================================
//#define AS5600_ON_TOWER
#ifdef AS5600_ON_TOWER
  #define as5600CountsPerTurretRev 4096L
#else
  #define as5600CountsPerTurretRev ((long)4096 * ringTeeth / pulleyTeeth)   // 26624
#endif

// Meldeintervall der Position (ms)
#define reportPeriode 200
