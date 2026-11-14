// hwDef.h -- EPF1_6_3_0 (Drehturm, XIAO ESP32-S3)
// Pins/Servos/Betriebsart gemeinsam mit dem Hardware-Test: ../EPF1Config.h
#pragma once
#include "../EPF1Config.h"

#define hostName     "EPF1"
#define periodeForHB 10000          // ms zwischen zwei Heartbeats

#if (defined(EPF1_MODE_LIDAR) + defined(EPF1_MODE_X40) + defined(EPF1_MODE_IR)) > 1
#error "Nur eine Betriebsart (EPF1_MODE_...) aktiv lassen"
#endif

#if defined(EPF1_MODE_LIDAR)
// --- Betriebsart Lidar: TOFSense-F (Nooploop) am Slave-Port ------------------------
// Erkennung beim Start: zuerst UART (Werkseinstellung 921600, aktive Ausgabe,
// 16-Byte-Frame 0x57 0x00 ...), dann I2C (Adresse 0x08 + Modul-ID, Register 0x20).
#define TOF_UART_BAUD  921600
#define TOF_I2C_HZ     100000
#define TOF_MIN_MM     100          // naeher = Turm-/Gehaeuse-Reflex, verwerfen
#define TOF_MAX_MM     15000        // TOFSense-F: 15 m
#define TOF_VALID_STATUS 1          // dis_status gueltiger Messungen - empirisch 2026-11-14 an diesem
                                    // Modul (I2C): Raum 0,5..2,7 m alle Status 1. Andere Werte = verwerfen
// hier statt im .ino: die Arduino-Prototypen stehen sonst vor dem Struct
struct TofFrame { uint32_t sysTime, dist; uint16_t status, strength; uint8_t precision; };

#define SCAN_SAMPLES   3            // neue TOF-Messungen je Schritt (Median)
#define MODE_NAME   "Lidar"
#define RANGER_NAME "TOFSense"
#elif defined(EPF1_MODE_X40)
// --- Betriebsart X40: Laser-Entfernungsmesser X-40 / Hi-AT am Slave-Port --------------
// UART 19200 8N1, ASCII-Befehle S/O/D/C (Library X40LaserDistanceMeter). Versorgung 2..3,3 V!
#define X40_BAUD            19200
#define X40_MEAS_TIMEOUT_MS 2500     // eine Messung (D) wartet hoechstens so lange
#define X40_MIN_MM          100
#define X40_MAX_MM          30000        // ~33,5 m kommt bei "kein Ziel"
struct X40Result { bool ok; int32_t mm; int sq; String raw; };
#define MODE_NAME   "Laser X40"
#define RANGER_NAME "X40"
#endif

#if defined(EPF1_MODE_LIDAR) || defined(EPF1_MODE_X40)
// Rundumscan: ganzes Pan-Fenster in 1-Grad-Schritten (immer von derselben Seite ->
// Getriebespiel einseitig), je Schritt eine Messung des Entfernungsmessers. Bins ausserhalb (Kabel-Sperrbereich hinten) bleiben 0 = ungueltig.
#define SCAN_STEP_MS      40        // Fahrzeit je 1-Grad-Schritt
#define SCAN_SETTLE_MS    60        // danach ausschwingen lassen
#define SCAN_STEP_DEFAULT  1        // Schrittweite in Grad (Befehl ohne Angabe)
#define SCAN_STEP_AUTO     5        // Auto-Scan nach dem Boot: Grobscan
#define SCAN_SAMPLE_TO_MS 400       // so lange hoechstens auf eine neue Messung warten
#define AUTO_SCAN_DELAY_MS 30000UL  // nach dem Boot so lange warten (Turm aufstellen, weggehen)
#define VPS_LOC_TIMEOUT_MS 30000
#define LOC_MIN_INLIER     0.60f    // Quality-Gate wie LidarC1
#define MODE_ACTIONS ((1u<<actCalibrate)|(1u<<actClearPose))
#define MODE_STG     0u
#elif defined(EPF1_MODE_IR)
// --- Betriebsart IR: IR-Strahler, Schaltleitung IR_PIN (GPIO7), HIGH = ein -----------
// Schalter wie Dome-C1 (stgIrLight + stgIrAuto, persistiert, Default AUTO):
//   AUS  -> Strahler aus | EIN -> Strahler dauernd ein
//   AUTO -> bei catObserved (irgendein Sensor) ein, IR_HOLD_MS lang; mit Welt-Pose dreht der
//           Turm in Richtung der Katze. Danach aus und zurueck auf 0.
#define MODE_NAME          "IR"
#define IR_HOLD_MS         60000UL
#define IR_AIM_MIN_MS      300      // hoechstens so oft neu ausrichten
#define IR_AIM_MOVE_MS     400      // Fahrzeit je Nachfuehrung
#define IR_AIM_DEADBAND_DEG 2.0f    // kleinere Aenderungen nicht anfahren
// Standort in der Hecke (2026-11-14): Pan nur +/-85 Grad um die 0. Gilt fuer ALLE Pan-
// Bewegungen in dieser Betriebsart (clampPan im Kern), solange der Strahler dort steht.
#define MODE_PAN_LIMIT_DEG 85.0f
#define MODE_ACTIONS       ((1u<<actClearPose))
#define MODE_STG           ((1u<<stgIrLight)|(1u<<stgIrAuto))
#else
#error "Keine Betriebsart in EPF1Config.h gewaehlt (EPF1_MODE_...)"
#endif

// --- Einstellungen & Steuerung ------------------------------------------------------
#define STG_SUPPORTED ((1u<<stgHbLed)|MODE_STG)
#define STG_DEFAULT   ((1u<<stgHbLed)|MODE_STG)
#define STG_PERSIST   ((1u<<stgHbLed)|MODE_STG)
#define STG_ACTIONS   MODE_ACTIONS
#define HB_BLINK_MS   150
