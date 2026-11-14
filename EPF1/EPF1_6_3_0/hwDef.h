// hwDef.h -- EPF1_6_3_0 (Drehturm, ESP32-C3 Super Mini)
// Pins/Servos gemeinsam mit dem Hardware-Test: ../EPF1Config.h
#pragma once
#include "../EPF1Config.h"

#define hostName     "EPF1"
#define periodeForHB 10000          // ms zwischen zwei Heartbeats

// --- TOFSense-F (Nooploop) an I2C ------------------------------------------------
// Das Modul muss vorher (NAssistant/USB-TTL) auf IIC umgestellt sein. Adresse
// 0x08 + Modul-ID; gesucht wird 0x08..0x17, die erste Antwort gewinnt.
// Frame ab Register 0x20 (16 Bytes, little-endian): system_time u32, dis u32 (mm),
// dis_status u16, signal_strength u16, range_precision u8.
#define TOF_ADDR_FIRST 0x08
#define TOF_ADDR_LAST  0x17
#define TOF_REG_DATA   0x20
#define TOF_I2C_HZ     100000
#define TOF_MIN_MM     100          // naeher = Turm-/Gehaeuse-Reflex, verwerfen
#define TOF_MAX_MM     15000        // TOFSense-F: 15 m
// hier statt im .ino: die Arduino-Prototypen stehen sonst vor dem Struct
struct TofFrame { uint32_t sysTime, dist; uint16_t status, strength; uint8_t precision; };

// --- Rundumscan fuer die Welt-Pose --------------------------------------------------
// Pan in 1-Grad-Schritten von -SCAN_LIMIT bis +SCAN_LIMIT (immer von derselben Seite ->
// Getriebespiel einseitig), je Schritt bis zu SCAN_SAMPLES neue TOF-Messungen, Median.
// Bins ausserhalb (Kabel-Sperrbereich hinten) bleiben 0 = ungueltig.
#define SCAN_LIMIT_DEG   170
#define SCAN_STEP_MS      40        // Fahrzeit je 1-Grad-Schritt
#define SCAN_SETTLE_MS    60        // danach ausschwingen lassen
#define SCAN_SAMPLES       2        // neue TOF-Messungen je Schritt
#define SCAN_SAMPLE_TO_MS 400       // so lange hoechstens auf eine neue Messung warten
#define AUTO_SCAN_DELAY_MS 30000UL  // nach dem Boot so lange warten (Turm aufstellen, weggehen)

// --- VPS-Lokalisierung (wie LidarC1) -------------------------------------------------
#define VPS_LOC_TIMEOUT_MS 30000
#define LOC_MIN_INLIER     0.60f    // Quality-Gate wie LidarC1

// --- Einstellungen & Steuerung ------------------------------------------------------
// HB-Blitz schaltbar [persistiert]. Aktionen: Kalibrieren (= Rundumscan + Lokalisierung),
// Pose loeschen.
#define STG_SUPPORTED ((1u<<stgHbLed))
#define STG_DEFAULT   ((1u<<stgHbLed))
#define STG_PERSIST   ((1u<<stgHbLed))
#define STG_ACTIONS   ((1u<<actCalibrate)|(1u<<actClearPose))
#define HB_BLINK_MS   150
