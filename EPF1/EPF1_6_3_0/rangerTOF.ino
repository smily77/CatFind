// rangerTOF.ino -- Entfernungsmesser TOFSense-F (Nooploop) fuer die Scan-Betriebsart
// Beim Start gesucht: zuerst UART (Werkseinstellung, 921600 Baud, aktive Ausgabe), beide
// Pin-Richtungen; dann I2C (0x08..0x17), beide Pin-Richtungen.
#if defined(EPF1_MODE_LIDAR)

enum TofLink { TOF_NONE, TOF_UART, TOF_I2C };
TofLink  tofLink = TOF_NONE;
uint8_t  tofAddr = 0;
int      tofRx = SLAVE_PIN_A, tofTx = SLAVE_PIN_B;    // tatsaechlich gefundene Belegung
TofFrame uartLast; bool uartHave = false;             // juengster UART-Frame
uint8_t  uartBuf[16]; int uartN = 0;

static uint32_t le32(const uint8_t* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t le16(const uint8_t* b) { return b[0] | (b[1] << 8); }

// UART-Frame NLink_TOFSense_Frame0 (16 Bytes): 0x57 0x00 res id | system_time u32 |
// dis int24 (mm) | dis_status u8 | signal_strength u16 | range_precision u8 | sum
static void uartPoll() {
  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    if (uartN == 0 && b != 0x57) continue;
    if (uartN == 1 && b != 0x00) { uartN = (b == 0x57) ? 1 : 0; continue; }
    uartBuf[uartN++] = b;
    if (uartN == 16) {
      uartN = 0;
      uint8_t sum = 0; for (int i = 0; i < 15; i++) sum += uartBuf[i];
      if (sum != uartBuf[15]) continue;
      uartLast.sysTime  = le32(uartBuf + 4);
      int32_t d = uartBuf[8] | (uartBuf[9] << 8) | (uartBuf[10] << 16);
      if (d & 0x800000) d |= 0xFF000000;
      uartLast.dist     = d < 0 ? 0 : (uint32_t)d;
      uartLast.status   = uartBuf[11];
      uartLast.strength = le16(uartBuf + 12);
      uartLast.precision = uartBuf[14];
      uartHave = true;
    }
  }
}

static bool tryUart(int rx, int tx) {
  Serial2.end();
  Serial2.begin(TOF_UART_BAUD, SERIAL_8N1, rx, tx);
  uartHave = false; uartN = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 600) { uartPoll(); if (uartHave) return true; delay(2); }
  Serial2.end();
  return false;
}

static bool tryI2c(int sda, int scl) {
  Wire.end();
  Wire.begin(sda, scl, TOF_I2C_HZ);
  for (uint8_t a = 0x08; a <= 0x17; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { tofAddr = a; return true; }
  }
  Wire.end();
  return false;
}

static void findTof() {
  tofLink = TOF_NONE;
  if (tryUart(SLAVE_PIN_A, SLAVE_PIN_B)) { tofLink = TOF_UART; tofRx = SLAVE_PIN_A; tofTx = SLAVE_PIN_B; }
  else if (tryUart(SLAVE_PIN_B, SLAVE_PIN_A)) { tofLink = TOF_UART; tofRx = SLAVE_PIN_B; tofTx = SLAVE_PIN_A; }
  else if (tryI2c(SLAVE_PIN_A, SLAVE_PIN_B)) { tofLink = TOF_I2C; tofRx = SLAVE_PIN_A; tofTx = SLAVE_PIN_B; }
  else if (tryI2c(SLAVE_PIN_B, SLAVE_PIN_A)) { tofLink = TOF_I2C; tofRx = SLAVE_PIN_B; tofTx = SLAVE_PIN_A; }
  if (tofLink == TOF_UART)
    say("TOFSense per UART " + String(TOF_UART_BAUD) + " (ESP-RX GPIO" + String(tofRx) + ", TX GPIO" + String(tofTx) + ")");
  else if (tofLink == TOF_I2C)
    say("TOFSense per I2C 0x" + String(tofAddr, HEX) + " (SDA GPIO" + String(tofRx) + ", SCL GPIO" + String(tofTx) + ")");
  else
    say("KEIN TOFSense am Slave-Port gefunden (UART 921600 und I2C 0x08..0x17, GPIO7/8)");
}

static bool tofRead(TofFrame& f) {
  if (tofLink == TOF_UART) {
    uartPoll();
    if (!uartHave) return false;
    f = uartLast; return true;
  }
  if (tofLink != TOF_I2C) return false;
  Wire.beginTransmission(tofAddr);
  Wire.write(0x20);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t b[16];
  if (Wire.requestFrom(tofAddr, (uint8_t)16) != 16) return false;
  for (int i = 0; i < 16; i++) b[i] = Wire.read();
  f.sysTime = le32(b); f.dist = le32(b + 4); f.status = le16(b + 8);
  f.strength = le16(b + 10); f.precision = b[12];
  return true;
}
static bool tofValid(const TofFrame& f) {
  return f.status == TOF_VALID_STATUS && f.strength > 0 && f.dist >= TOF_MIN_MM && f.dist <= TOF_MAX_MM;
}

// Wartet auf bis zu n NEUE Messungen (system_time aendert sich), Median der gueltigen
// (0 = keine gueltige). statusOut = Status der letzten Messung (0xFFFF = keine).
static uint16_t tofMeasure(int n, uint16_t& statusOut) {
  TofFrame f; uint32_t lastT = 0; bool haveT = false;
  uint32_t v[8]; int k = 0; statusOut = 0xFFFF;
  if (tofRead(f)) { lastT = f.sysTime; haveT = true; }
  for (int got = 0; got < n; ) {
    unsigned long t0 = millis(); bool fresh = false;
    while (millis() - t0 < SCAN_SAMPLE_TO_MS) {
      if (tofRead(f) && (!haveT || f.sysTime != lastT)) { fresh = true; break; }
      delay(2);
    }
    if (!fresh) break;
    lastT = f.sysTime; haveT = true; got++;
    statusOut = f.status;
    if (tofValid(f) && k < 8) v[k++] = f.dist;
  }
  if (!k) return 0;
  for (int i = 1; i < k; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { uint32_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  return (uint16_t)v[k / 2];
}

// ----------------------------------------------------------------- Ranger-Schnittstelle
bool rangerFind()    { findTof(); return tofLink != TOF_NONE; }
bool rangerPresent() { return tofLink != TOF_NONE; }
void rangerPoll()    { if (tofLink == TOF_UART) uartPoll(); }   // UART-Puffer leer halten
void rangerScanBegin() {}
void rangerScanEnd()   {}
bool rangerLaser(bool) { return false; }   // TOFSense: kein schaltbarer Laser
// mm (0 = keine gueltige); st: 0 gueltig, 1 andere, 0xFFFF keine Messung
uint16_t rangerMeasure(uint16_t& st) {
  uint16_t s; uint16_t d = tofMeasure(SCAN_SAMPLES, s);
  st = (s == 0xFFFF) ? 0xFFFF : (s == TOF_VALID_STATUS ? 0 : 1);
  return d;
}
void rangerTest() {
  if (tofLink == TOF_NONE) findTof();
  TofFrame f;
  for (int i = 0; i < 5; i++) {
    delay(150);
    if (tofRead(f)) say("TOF dist=" + String(f.dist) + " status=" + String(f.status) + " strength=" + String(f.strength));
    else say("TOF: keine Antwort");
  }
}

#endif  // EPF1_MODE_LIDAR
