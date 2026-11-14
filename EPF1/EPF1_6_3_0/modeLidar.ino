// modeLidar.ino -- EPF1-Betriebsart Lidar: TOFSense-F am Slave-Port (GPIO7/8)
//
// Beim Start wird der TOFSense gesucht: zuerst UART (Werkseinstellung, 921600 Baud,
// aktive Ausgabe), beide Pin-Richtungen; dann I2C (0x08..0x17), beide Pin-Richtungen.
// Rundumscan Pan -170..+170 Grad in 1-Grad-Schritten -> 360-Bin-Scan (mm, 0 = ungueltig)
// an den VPS-Localizer (/localize, wie LidarC1) -> Welt-Pose. Automatisch
// AUTO_SCAN_DELAY_MS nach dem Boot und per Aktion "Kalibrieren" (cmdCalibrate).
#if defined(EPF1_MODE_LIDAR)

enum TofLink { TOF_NONE, TOF_UART, TOF_I2C };
TofLink  tofLink = TOF_NONE;
uint8_t  tofAddr = 0;
int      tofRx = SLAVE_PIN_A, tofTx = SLAVE_PIN_B;    // tatsaechlich gefundene Belegung
bool     scanRequested = false, autoScanDone = false;
uint16_t scanBins[360];
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

void requestScan() { scanRequested = true; }

static void doScanAndLocalize() {
  busy = true;
  doScan();
  busy = false;
}

void doScan() {
  if (tofLink == TOF_NONE) findTof();
  if (tofLink == TOF_NONE) { say("Scan abgebrochen: kein TOFSense"); return; }
  say("Rundumscan startet (ganzes Pan-Fenster, Pan-0 = " + String(panOrigin) + " Ticks)");
  setPix(0x000020);
  memset(scanBins, 0, sizeof(scanBins));
  tilt.moveTime(tiltDegToTicks(0), 800, 400);
  // ganzes physisches Fenster in 1-Grad-Schritten, immer von unten angefahren
  // (Getriebespiel einseitig); Bin = Ist-Winkel relativ zur 0
  pan.moveTime(max(0, PAN_PHYS_MIN - (int)(5 * PAN_TICKS_PER_DEG)), 2500, 400);
  waitMs(2800);
  int nValid = 0, nStat[3] = {0, 0, 0};
  const int nSteps = (int)((PAN_PHYS_MAX - PAN_PHYS_MIN) / PAN_TICKS_PER_DEG);
  for (int k = 0; k <= nSteps; k++) {
    uint16_t tk = clampPan(lroundf(PAN_PHYS_MIN + k * PAN_TICKS_PER_DEG));
    int deg = (int)lroundf(panTicksToDeg(tk));
    pan.moveTime(tk, SCAN_STEP_MS, 0);
    waitMs(SCAN_STEP_MS + SCAN_SETTLE_MS);
    uint16_t st;
    uint16_t d = tofMeasure(SCAN_SAMPLES, st);
    uint16_t ist = 0; float a = deg;
    if (pan.readPresentPosition(ist)) a = panTicksToDeg(ist);      // Ist-Winkel (Getriebe)
    int bin = ((int)lroundf(a) % 360 + 360) % 360;
    if (d) { scanBins[bin] = d; nValid++; }
    nStat[st == TOF_VALID_STATUS ? 0 : (st == 0xFFFF ? 2 : 1)]++;
    if (k % 60 == 0)
      Serial << "  " << deg << " Grad: " << d << " mm (Status " << st << ")" << endl;
  }
  goHome(2500);
  say("Scan fertig: " + String(nValid) + " gueltige Bins; Status " + String(TOF_VALID_STATUS) + ": " + String(nStat[0]) +
      ", andere: " + String(nStat[1]) + ", keine Messung: " + String(nStat[2]));
  // Rohscan fuer die Auswertung im VPS-Debug-Fenster (bin:mm, nur belegte)
  String line = "";
  for (int i = 0; i < 360; i++) {
    if (!scanBins[i]) continue;
    line += String(i) + ":" + String(scanBins[i]) + " ";
    if (line.length() > 170) { sendUdpTextln("EPF1 scan " + line); line = ""; waitMs(30); }
  }
  if (line.length()) sendUdpTextln("EPF1 scan " + line);

  if (nValid < 8) { say("zu wenige Messungen - keine Lokalisierung"); setPix(idleColor()); return; }
  setPix(0x200020);
  int32_t vx = 0, vy = 0; float vh = 0, vin = 0; int vm = 1; String conf = "";
  bool ok = vpsLocalize(scanBins, 360, vx, vy, vh, vm, conf, vin);
  if (ok) say("VPS-Pose x=" + String(vx) + " y=" + String(vy) + " h=" + String(vh, 1) +
              " mir=" + String(vm) + " conf=" + conf + " inlier=" + String(vin, 2));
  else    say("VPS-Lokalisierung fehlgeschlagen");
  resolvePose(ok, vx, vy, vh, vm, conf, vin, hadNVSpose, LOC_MIN_INLIER);
  if (myPose.validWorldPose) hadNVSpose = true;
  say(String("Pose ") + (myPose.validWorldPose ? "gueltig" : "UNGUELTIG") +
      " (x=" + String(myPose.worldX) + " y=" + String(myPose.worldY) + ")");
  sendPoseReport();
  setPix(idleColor());
}

// ----------------------------------------------------------------- Schnittstelle zum Kern
void modeSetup() {
  findTof();
  say("Betriebsart Lidar bereit, Rundumscan in " + String(AUTO_SCAN_DELAY_MS / 1000) + " s");
}

void modeLoop() {
  if (tofLink == TOF_UART) uartPoll();            // Puffer leer halten
  if (!autoScanDone && millis() - bootMs >= AUTO_SCAN_DELAY_MS) { autoScanDone = true; scanRequested = true; }
  if (scanRequested) { scanRequested = false; autoScanDone = true; doScanAndLocalize(); }
}

bool modeCommand(const cmdPayload& c) {
  if (c.cmd == cmdCalibrate) { scanRequested = true; return true; }
  return false;
}

const char* modeHelp() { return "c Scan | d TOF lesen | s TOF suchen"; }

bool modeSerial(char c, float) {
  switch (c) {
    case 'c': scanRequested = true; return true;
    case 's': findTof(); return true;
    case 'd': {
      if (tofLink == TOF_NONE) findTof();
      TofFrame f;
      for (int i = 0; i < 5; i++) {
        delay(150);
        if (tofRead(f)) Serial << "TOF t=" << f.sysTime << " dist=" << f.dist << " status=" << f.status
                               << " strength=" << f.strength << " prec=" << f.precision << endl;
        else Serial << "TOF: keine Antwort" << endl;
      }
    } return true;
  }
  return false;
}

#endif  // EPF1_MODE_LIDAR
