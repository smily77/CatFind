// bmp180.h -- minimaler, NICHT blockierender BMP180-Treiber (Dome-C1).
//
// Warum keine Bibliothek: die ueblichen Treiber warten per delay() auf die
// Wandlung (bis ~26 ms bei hoechster Aufloesung). In dieser Zeit laeuft der
// 256-kBaud-Radarstrom (HLK-2450) weiter und kann den Serial2-Puffer fuellen.
// Hier wird die Wandlung nur angestossen und spaeter aus loop() abgeholt.
//
// Rechnung exakt nach Bosch-Datenblatt BST-BMP180-DS000 (Kap. 3.5).
// Nutzung:  bmpBegin(sda, scl)  einmal;  bmpStart()  Messung anstossen;
//           bmpService()  in jeder loop() -> true, sobald ein neuer Wert vorliegt
//           (bmpTempDeci in 0,1 Grad C, bmpPressPa in Pa).
#pragma once
#include <Wire.h>

#define BMP180_ADDR 0x77
#define BMP180_OSS  3          // Oversampling 0..3 (3 = ultra high resolution, 25,5 ms)

static bool     bmpOk = false;
static int16_t  bmpAC1, bmpAC2, bmpAC3, bmpB1, bmpB2, bmpMB, bmpMC, bmpMD;
static uint16_t bmpAC4, bmpAC5, bmpAC6;
static int16_t  bmpTempDeci = 0;   // letzte Temperatur (0,1 Grad C)
static int32_t  bmpPressPa  = 0;   // letzter Luftdruck (Pa)
static uint8_t  bmpState    = 0;   // 0 = frei, 1 = Temperatur wandelt, 2 = Druck wandelt
static unsigned long bmpT0  = 0;   // Start der laufenden Wandlung
static int32_t  bmpUT       = 0;

static bool bmpWrite8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(BMP180_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool bmpRead(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(BMP180_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)BMP180_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// I2C starten, Chip-ID (0x55) pruefen und Kalibrierdaten (22 Byte ab 0xAA) lesen.
bool bmpBegin(int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.setTimeOut(20);
  uint8_t id = 0;
  if (!bmpRead(0xD0, &id, 1) || id != 0x55) { bmpOk = false; return false; }
  uint8_t c[22];
  if (!bmpRead(0xAA, c, 22)) { bmpOk = false; return false; }
  bmpAC1 = (int16_t)((c[0]  << 8) | c[1]);
  bmpAC2 = (int16_t)((c[2]  << 8) | c[3]);
  bmpAC3 = (int16_t)((c[4]  << 8) | c[5]);
  bmpAC4 = (uint16_t)((c[6]  << 8) | c[7]);
  bmpAC5 = (uint16_t)((c[8]  << 8) | c[9]);
  bmpAC6 = (uint16_t)((c[10] << 8) | c[11]);
  bmpB1  = (int16_t)((c[12] << 8) | c[13]);
  bmpB2  = (int16_t)((c[14] << 8) | c[15]);
  bmpMB  = (int16_t)((c[16] << 8) | c[17]);
  bmpMC  = (int16_t)((c[18] << 8) | c[19]);
  bmpMD  = (int16_t)((c[20] << 8) | c[21]);
  bmpOk = true;
  return true;
}

// Messung anstossen (Temperatur, danach automatisch Druck). false = Sensor fehlt/belegt.
bool bmpStart() {
  if (!bmpOk || bmpState != 0) return false;
  if (!bmpWrite8(0xF4, 0x2E)) return false;
  bmpState = 1; bmpT0 = millis();
  return true;
}

static void bmpCompute(int32_t UP) {
  const int oss = BMP180_OSS;
  int32_t X1 = ((bmpUT - (int32_t)bmpAC6) * (int32_t)bmpAC5) >> 15;
  int32_t X2 = ((int32_t)bmpMC << 11) / (X1 + bmpMD);
  int32_t B5 = X1 + X2;
  bmpTempDeci = (int16_t)((B5 + 8) >> 4);

  int32_t B6 = B5 - 4000;
  X1 = ((int32_t)bmpB2 * ((B6 * B6) >> 12)) >> 11;
  X2 = ((int32_t)bmpAC2 * B6) >> 11;
  int32_t X3 = X1 + X2;
  int32_t B3 = ((((int32_t)bmpAC1 * 4 + X3) << oss) + 2) / 4;
  X1 = ((int32_t)bmpAC3 * B6) >> 13;
  X2 = ((int32_t)bmpB1 * ((B6 * B6) >> 12)) >> 16;
  X3 = ((X1 + X2) + 2) >> 2;
  uint32_t B4 = ((uint32_t)bmpAC4 * (uint32_t)(X3 + 32768)) >> 15;
  uint32_t B7 = ((uint32_t)UP - B3) * (uint32_t)(50000UL >> oss);
  int32_t p = (B7 < 0x80000000UL) ? (int32_t)((B7 * 2) / B4) : (int32_t)((B7 / B4) * 2);
  X1 = (p >> 8) * (p >> 8);
  X1 = (X1 * 3038) >> 16;
  X2 = (-7357 * p) >> 16;
  bmpPressPa = p + ((X1 + X2 + 3791) >> 4);
}

// Aus loop(): laufende Wandlung abholen. true = neues Wertepaar fertig.
// Faellt der Bus mitten in der Messung aus, wird abgebrochen (bmpState=0, false).
bool bmpService() {
  if (bmpState == 1 && millis() - bmpT0 >= 5) {           // Temperatur: 4,5 ms
    uint8_t b[2];
    if (!bmpRead(0xF6, b, 2)) { bmpState = 0; return false; }
    bmpUT = (int32_t)((b[0] << 8) | b[1]);
    if (!bmpWrite8(0xF4, 0x34 + (BMP180_OSS << 6))) { bmpState = 0; return false; }
    bmpState = 2; bmpT0 = millis();
  }
  else if (bmpState == 2 && millis() - bmpT0 >= 28) {     // Druck, OSS 3: 25,5 ms
    uint8_t b[3];
    bmpState = 0;
    if (!bmpRead(0xF6, b, 3)) return false;
    int32_t UP = (((int32_t)b[0] << 16) | ((int32_t)b[1] << 8) | b[2]) >> (8 - BMP180_OSS);
    bmpCompute(UP);
    return true;
  }
  return false;
}
