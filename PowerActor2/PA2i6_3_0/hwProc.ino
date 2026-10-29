// hwProc.ino -- PowerActor2: HB, Ausgaenge/Relais, Servo, LP40B, IR-Kamera
// (Treiber aus Development/AfterSerialRebuild/Step5_RelaysIR uebernommen)

void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

// ---- HB (pa2HbPayload, drop-in fuer Display/Button) -----------------------------------
// Die alten festen Winkel-Limits gibt es nicht mehr (dynamisch, aimProc.ino):
// limitsActive = 0, far/near = Reichweite des Wasserstrahls.
void sendHBmsg() {
  hbState.hb.ip = getLastIpByte();
  hbState.limitsActive = 0;
  hbState.leftLimit = 0;
  hbState.rightLimit = 4096;
  hbState.farLimit = WATER_RANGE_MM;
  hbState.nearLimit = NEAR_MM;
  broadcastMsg(HB, hbState);
}

void heardBeat() {
  if (millis() - HBtimer >= periodeForHB) { sendHBmsg(); HBtimer = millis(); }
}

// ---- Ausgaenge -----------------------------------------------------------------------
void initOutputs() {
  const uint8_t pins[] = {laserPin, valvePin, lidarPwrPin, radarPwrPin, spotPin};
  for (uint8_t p : pins) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
}

// Speisung LD06 / Compact Dome nach den Einstellungen (NVS)
void applyPower() {
  static int8_t lidarWas = -1, radarWas = -1;
  bool lidar = settingOn(stgLidarMotor), radar = settingOn(stgRadarPower);
  digitalWrite(lidarPwrPin, lidar ? HIGH : LOW);
  digitalWrite(radarPwrPin, radar ? HIGH : LOW);
  if (lidar != lidarWas || radar != radarWas)
    sendUdpTextln(String("PowerActor2: LD06 ") + (lidar ? "an" : "aus") +
                  ", Compact Dome " + (radar ? "an" : "aus"));
  lidarWas = lidar; radarWas = radar;
}

// Schuss: Laser und Ventil gleich; Ventil nur mit stgWater (sonst Trockentest)
void fireOn() {
  digitalWrite(laserPin, HIGH);
  if (settingOn(stgWater)) digitalWrite(valvePin, HIGH);
}
void fireOff() {
  digitalWrite(valvePin, LOW);
  digitalWrite(laserPin, LOW);
}

// ---- Servo ---------------------------------------------------------------------------
bool     servoOk = false;
uint16_t servoGoal = SERVO_CENTER;
uint16_t servoPos = SERVO_CENTER;      // zuletzt gelesene Ist-Position
unsigned long servoRetryAt = 0;

void initServo() {
  bus.beginPins(SERVO_BAUD, SERVO_RX_PIN, SERVO_TX_PIN);
  delay(50);
  servoOk = servo.ping();
  if (!servoOk) { sendUdpTextln("PowerActor2: Servo antwortet NICHT"); return; }
  servo.setMode(ServoMode::POSITION);
  FeetechST3020::Profile p;
  p.statusReturnLevel = 1;
  p.returnDelayUnits  = 0;
  p.torqueOnAfterInit = true;
  servo.init(p);
  uint16_t lo = 0, hi = 0;
  if (servo.readAngleLimits(lo, hi) && (lo != SERVO_MIN || hi != SERVO_MAX))
    servo.setAngleLimits(SERVO_MIN, SERVO_MAX);    // EEPROM nur wenn noetig
  servo.setAcceleration(0);
  servo.moveTime(servoGoal, 1500, 800);
}

void servoGoTo(uint16_t pos) {
  servoGoal = constrain(pos, SERVO_MIN, SERVO_MAX);
  if (servoOk) servo.moveTime(servoGoal, 0, SERVO_FAST);
}

// Ist-Position lesen (fuer "ausgerichtet"), Servo nachtraeglich angesteckt?
void serviceServo() {
  static unsigned long lastRead = 0;
  if (!servoOk) {
    if (millis() > servoRetryAt) { initServo(); servoRetryAt = millis() + 5000; }
    return;
  }
  if (millis() - lastRead < 40) return;
  lastRead = millis();
  uint16_t p;
  if (servo.readPresentPosition(p)) servoPos = p;
}

bool servoAligned() {
  return servoOk && abs((int)servoPos - (int)servoGoal) <= AIM_TOL;
}

// ---- LP40B: misst nur waehrend eines Zielvorgangs (Dauermessung Start/Stopp) ----------
uint32_t lidarMm = 0;
uint8_t  lidarErr = 0xFF;              // 0 = ok
unsigned long lidarAt = 0;
bool     lidarRunning = false;

static uint8_t lidarCrc(const uint8_t* p, uint8_t len) {   // CRC-8, Polynom 0x31
  uint8_t crc = 0;
  while (len--) {
    crc ^= *p++;
    for (uint8_t i = 0; i < 8; i++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

static void lidarSend(uint8_t key, uint8_t value) {
  uint8_t f[8] = {0x55, key, 0, 0, 0, value, 0, 0xAA};
  f[6] = lidarCrc(f + 1, 5);
  Serial2.write(f, sizeof(f));
}

void initLidar() {
  Serial2.begin(LIDAR_BAUD, SERIAL_8N1, LIDAR_RX_PIN, LIDAR_TX_PIN);
  lidarSend(0x06, 0);                  // evtl. laufende Messung stoppen
  delay(20);
  lidarSend(0x04, 0x01);               // Byte-Format
  delay(20);
  lidarSend(0x0D, 0x02);               // Dauermessung auf Startbefehl
}

void lidarStart() { if (!lidarRunning) { lidarSend(0x05, 0); lidarRunning = true; } }
void lidarStop()  { if (lidarRunning)  { lidarSend(0x06, 0); lidarRunning = false; } }

bool lidarFresh() { return lidarErr == 0 && millis() - lidarAt < LIDAR_STALE_MS; }

void pollLidar() {
  static uint8_t f[8];
  static uint8_t n = 0;
  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    if (n == 0 && b != 0x55) continue;
    f[n++] = b;
    if (n < 8) continue;
    n = 0;
    if (f[7] != 0xAA || lidarCrc(f + 1, 5) != f[6]) {
      for (uint8_t i = 1; i < 8; i++)
        if (f[i] == 0x55) { memmove(f, f + i, 8 - i); n = 8 - i; break; }
      continue;
    }
    if (f[1] != 0x07) continue;
    lidarErr = f[2];
    lidarMm  = ((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 8) | f[5];
    lidarAt  = millis();
  }
}

// ---- IR-Positionskamera (fuer die spaetere Pose-Findung, heute nur gelesen) -----------
bool     irOk = false;
uint16_t irX[IR_POINTS], irY[IR_POINTS];
uint8_t  irS[IR_POINTS];

static bool irWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IR_ADDR);
  Wire.write(reg);
  Wire.write(val);
  bool ok = Wire.endTransmission() == 0;
  delay(10);
  return ok;
}

void initIR() {
  Wire.begin(IR_SDA_PIN, IR_SCL_PIN, 100000);
  for (uint8_t i = 0; i < IR_POINTS; i++) { irX[i] = irY[i] = IR_NONE; irS[i] = 0; }
  irOk = irWrite(0x30, 0x01) && irWrite(0x30, 0x08) && irWrite(0x06, 0x90) &&
         irWrite(0x08, 0xC0) && irWrite(0x1A, 0x40) && irWrite(0x33, 0x33);
}

void pollIR() {
  static unsigned long last = 0, retryAt = 0;
  if (!irOk) {
    if (millis() > retryAt) { initIR(); retryAt = millis() + 10000; }
    return;
  }
  if (millis() - last < IR_PERIOD_MS) return;
  last = millis();
  Wire.beginTransmission(IR_ADDR);
  Wire.write(0x36);
  if (Wire.endTransmission() != 0 || Wire.requestFrom(IR_ADDR, 16) != 16) {
    while (Wire.available()) Wire.read();
    irOk = false;
    return;
  }
  uint8_t b[16];
  for (uint8_t i = 0; i < 16; i++) b[i] = Wire.read();
  for (uint8_t i = 0; i < IR_POINTS; i++) {
    uint8_t s = b[3 + 3 * i];
    irX[i] = b[1 + 3 * i] | ((s & 0x30) << 4);
    irY[i] = b[2 + 3 * i] | ((s & 0xC0) << 2);
    irS[i] = s & 0x0F;
  }
}

// ---- Statuszeile fuer den VPS-Debug ----------------------------------------------------
String statusText(const char* what) {
  String s = String("PowerActor2 ") + what + ": ";
  if (myPose.validWorldPose)
    s += "Pose x=" + String(myPose.worldX / 1000.0f, 2) + " y=" + String(myPose.worldY / 1000.0f, 2) +
         " h=" + String(myPose.heading * 360.0f / 4096.0f, 1) + " m=" + String(myPose.mirror);
  else s += "KEINE Pose";
  s += String(", Servo ") + (servoOk ? "ok" : "FEHLT");
  s += String(", IR ") + (irOk ? "ok" : "fehlt");
  s += String(", No-Shot ") + (noShotOK ? "ok" : "FEHLT");
  s += String(", Wasser ") + (settingOn(stgWater) ? "AN" : "aus (Trockentest)");
  s += String(", ") + (hbState.readyToFire ? "SCHARF" : "gesichert");
  return s;
}
