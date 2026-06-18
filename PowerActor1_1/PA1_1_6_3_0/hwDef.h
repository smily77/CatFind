// =============================================================================
//  PA1_1_6_3_0 - Hardware-Definition
//  Aelterer PowerActor (PA1b): ESP32, Drehturm ueber Schrittmotor.
//  Stepper-Treiber A4988, angesteuert ueber Port-Expander PCF8574 (I2C).
//  Winkelgeber AS5600 am selben I2C-Bus (aktuell auf der Stepper-Welle).
//  Riemenantrieb: Pulley am Stepper 20 Zaehne, Zahnkranz am Turm 130 Zaehne.
//  Zwei Reed-Kontakte am Drehturm (Referenz/Endlagen).
//  Relais: water (Wasserventil/Feuer), wirelessPower (Strom Turm-Sensoren),
//          extPower (externe Versorgung).
//
//  ACHTUNG (gelernte Lektion): Pin-/Konstanten-Makros duerfen NICHT mit
//  Feldnamen aus den gemeinsamen Payloads kollidieren (z.B. pa2HbPayload hat
//  leftLimit/rightLimit/readyToFire...). Hier ist alles kollisionsfrei.
// =============================================================================

#define hostName "PowerActor1_1"
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
#define water         12     // Relais Wasserventil  = Feuer-Aktor
#define extPower      13     // Relais externe Versorgung
#define wirelessPower 14     // Relais Strom fuer Turm-Sensoren (an bei Boot)
#define taster        33     // lokaler Taster (INPUT_PULLUP)
#define reedLeft      26     // Reed-Kontakt links  (INPUT_PULLUP, am Turm)
#define reedRight     32     // Reed-Kontakt rechts (INPUT_PULLUP, am Turm)
#define laser         18     // Ziellaser

// --- I2C (PCF8574 + AS5600) ---
// pinSDA/pinSCL statt sda/scl: 'sda'/'scl' sind Parameternamen in Wire.h und
// PCF8574.h - als Makro wuerden sie deren Deklarationen zerstoeren.
#define pinSDA 21
#define pinSCL 19
#define motorAdr 0x20        // PCF8574-Adresse des Stepper-Ports

// --- PCF8574-Portbelegung -> A4988 ---
#define DIR  0
#define STEP 1
#define SLP  2
#define RST  3
#define MS3  4
#define MS2  5
#define MS1  6
#define EN   7

// --- Mikroschritt-Stufen (MS1..MS3) ---
#define fullStep      1
#define halfStep      2
#define quaterStep    4
#define eighthStep    8
#define sixteenthStep 16
#define defaultMicrostep sixteenthStep

// --- Drehrichtung (eigene Namen, damit kein Konflikt mit leftLimit/rightLimit) ---
#define dirLeft  HIGH
#define dirRight LOW

// --- Mechanik / Geometrie ---
#define stepsPerRev 200      // Vollschritte pro Stepper-Umdrehung (1.8°-Motor) - anpassen falls 0.9°
#define pulleyTeeth 20       // Zaehne Pulley am Stepper
#define ringTeeth   130      // Zaehne Zahnkranz am Drehturm
// Mikroschritte pro voller TURM-Umdrehung = 200 * 16 * 130 / 20 = 20800
// (als long berechnet im Programm)

// --- Winkel/Distanz-Bereich (wie PA2, PA-Welt 0..4096, 2048 = geradeaus) ---
// maxAngle 180 -> systemAngle = 180*4096/360 = 2048 -> Default-Limits 2048+-2048
// = voller Kreis [0..4096]. Der Drehturm kann +-180 Grad um geradeaus schwenken.
#define maxAngle     180     // nutzbarer Halbwinkel in Grad -> systemAngle = maxAngle*4096/360
#define maxDist      8000    // mm
#define initDeadZone 500     // mm
#define fireDuration 500     // ms Wasserstoss

// --- Stepper-Timing / Sicherheit ---
#define stepPulseUs   600    // halbe STEP-Periode in us (ueber PCF8574/I2C ohnehin langsam)
#define maxHomingMicrosteps 41600  // Sicherheitslimit Homing = 2 Turm-Umdrehungen
