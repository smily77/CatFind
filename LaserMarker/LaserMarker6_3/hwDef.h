// LaserMarker6_3 - Hardware-Definition
// Board: ESP32-C3 Super Mini  (siehe ../Cirquit/Scannen.pdf)
//
// Pinbelegung uebernommen aus dem Infrastruktur-Test
// (LaserMarker/InfrastructureTest/Basic1/Basic1.ino)
//
// WICHTIG: Die Pin-Makros tragen die Endung ...Pin, damit sie NICHT mit den
// gleichnamigen Feldern in markerHbPayload (mainLaser, subLaser, aux)
// kollidieren - sonst ersetzt der Praeprozessor z.B. state.mainLaser -> state.3

#define hostName "Laser_Marker"
#define periodeForHB 5000          // ms zwischen zwei Heartbeats

// --- gesteuerte Ausgaenge ---
#define mainLaserPin 3             // GPIO3  -> TTL-Laser-Modul (IN), HIGH = an
#define subLaserPin  21            // GPIO21 -> BS170-MOSFET -> Laserdiode, HIGH = an
#define auxPin       7             // GPIO7  -> Aux-Ausgang (frei verwendbar), HIGH = an
#define pixelPin     10            // GPIO10 -> 1x WS2812 NeoPixel (RGB)

// --- Schnittstellen (am Stecker herausgefuehrt, derzeit ungenutzt) ---
#define pinSDA 0                   // GPIO0  -> I2C SDA
#define pinSCL 1                   // GPIO1  -> I2C SCL
#define pinTxD 20                  // GPIO20 -> UART TX
#define pinRxD 4                   // GPIO4  -> UART RX
