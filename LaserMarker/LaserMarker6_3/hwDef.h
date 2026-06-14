// LaserMarker6_3 - Hardware-Definition
// Board: ESP32-C3 Super Mini  (siehe ../Cirquit/Scannen.pdf)
//
// Pinbelegung uebernommen aus dem Infrastruktur-Test
// (LaserMarker/InfrastructureTest/Basic1/Basic1.ino)

#define hostName "Laser_Marker"
#define periodeForHB 5000          // ms zwischen zwei Heartbeats

// --- gesteuerte Ausgaenge ---
#define mainLaser 3                // GPIO3  -> TTL-Laser-Modul (IN), HIGH = an
#define subLaser  21               // GPIO21 -> BS170-MOSFET -> Laserdiode, HIGH = an
#define Aux       7                // GPIO7  -> Aux-Ausgang (frei verwendbar), HIGH = an
#define pixelPin  10               // GPIO10 -> 1x WS2812 NeoPixel (RGB)

// --- Schnittstellen (am Stecker herausgefuehrt, derzeit ungenutzt) ---
#define SDA 0                      // GPIO0  -> I2C SDA
#define SCL 1                      // GPIO1  -> I2C SCL
#define TxD 20                     // GPIO20 -> UART TX
#define RxD 4                      // GPIO4  -> UART RX
