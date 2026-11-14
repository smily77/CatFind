// hwDef.h -- EPF1_6_3_0 (Drehturm, XIAO ESP32-S3)
// Pins/Servos/Betriebsart gemeinsam mit dem Hardware-Test: ../EPF1Config.h
#pragma once
#include "../EPF1Config.h"

#define hostName     "EPF1"
#define periodeForHB 10000          // ms zwischen zwei Heartbeats

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

// Rundumscan: Pan in 1-Grad-Schritten von -SCAN_LIMIT bis +SCAN_LIMIT (immer von
// derselben Seite -> Getriebespiel einseitig), je Schritt Median aus SCAN_SAMPLES
// neuen Messungen. Bins ausserhalb (Kabel-Sperrbereich hinten) bleiben 0 = ungueltig.
#define SCAN_LIMIT_DEG   170
#define SCAN_STEP_MS      40        // Fahrzeit je 1-Grad-Schritt
#define SCAN_SETTLE_MS    60        // danach ausschwingen lassen
#define SCAN_SAMPLES       3        // neue TOF-Messungen je Schritt
#define SCAN_SAMPLE_TO_MS 400       // so lange hoechstens auf eine neue Messung warten
#define AUTO_SCAN_DELAY_MS 30000UL  // nach dem Boot so lange warten (Turm aufstellen, weggehen)
#define VPS_LOC_TIMEOUT_MS 30000
#define LOC_MIN_INLIER     0.60f    // Quality-Gate wie LidarC1
#define MODE_NAME "Lidar"
#define MODE_ACTIONS ((1u<<actCalibrate)|(1u<<actClearPose))
#else
#error "Keine Betriebsart in EPF1Config.h gewaehlt (EPF1_MODE_...)"
#endif

// --- Einstellungen & Steuerung ------------------------------------------------------
#define STG_SUPPORTED ((1u<<stgHbLed))
#define STG_DEFAULT   ((1u<<stgHbLed))
#define STG_PERSIST   ((1u<<stgHbLed))
#define STG_ACTIONS   MODE_ACTIONS
#define HB_BLINK_MS   150
