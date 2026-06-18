// encoderTest6_3_1 - geraetespezifische Funktionen (Turm-Seite)
// abgeleitet aus PA1_1_6_3_0/hwProc.ino, reduziert auf Stepper + AS5600 + Melden.

void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

const long microstepsPerTurretRev =
    (long)stepsPerRev * defaultMicrostep * ringTeeth / pulleyTeeth;   // = 20800

// --- Stepper-Low-Level (A4988 ueber PCF8574) ---
void setMicrostep(int stepSize) {
  switch (stepSize) {
    case fullStep:      rotor.digitalWrite(MS1,LOW);  rotor.digitalWrite(MS2,LOW);  rotor.digitalWrite(MS3,LOW);  break;
    case halfStep:      rotor.digitalWrite(MS1,HIGH); rotor.digitalWrite(MS2,LOW);  rotor.digitalWrite(MS3,LOW);  break;
    case quaterStep:    rotor.digitalWrite(MS1,LOW);  rotor.digitalWrite(MS2,HIGH); rotor.digitalWrite(MS3,LOW);  break;
    case eighthStep:    rotor.digitalWrite(MS1,HIGH); rotor.digitalWrite(MS2,HIGH); rotor.digitalWrite(MS3,LOW);  break;
    case sixteenthStep: rotor.digitalWrite(MS1,HIGH); rotor.digitalWrite(MS2,HIGH); rotor.digitalWrite(MS3,HIGH); break;
  }
}

void stepOnce(bool dir) {
  rotor.digitalWrite(DIR, dir);
  rotor.digitalWrite(STEP, HIGH);
  delayMicroseconds(stepPulseUs);
  rotor.digitalWrite(STEP, LOW);
  delayMicroseconds(stepPulseUs);
  globalPosition += (dir == dirRight) ? 1 : -1;
}

void moveToPos(long target) {
  setMicrostep(defaultMicrostep);
  while (globalPosition != target) {
    stepOnce(target > globalPosition ? dirRight : dirLeft);
    ArduinoOTA.handle();
  }
}

bool leftReed()  { return !digitalRead(reedLeft);  }
bool rightReed() { return !digitalRead(reedRight); }

// --- Winkel-Abbildung (PA-Welt 0..4096, 2048 = geradeaus) ---
long paAngleToSteps(float paAngle) {
  return lround((paAngle - 2048.0) * (double)microstepsPerTurretRev / 4096.0);
}
// aktuelle Schritt-Position als PA-Winkel
float stepsToPaAngle() {
  return 2048.0 + (double)globalPosition * 4096.0 / microstepsPerTurretRev;
}
// AS5600-Messung als PA-Winkel (Faktor je nach Montage Stepper/Turm)
float as5600ToPaAngle() {
  return 2048.0 + (double)winkel.getCumulativePosition() * 4096.0 / as5600CountsPerTurretRev;
}

// Turm auf einen PA-Winkel fahren (auf 0..4096 begrenzt)
void aimAt(float paAngle) {
  if (!homed) return;
  if (paAngle < 0)    paAngle = 0;
  if (paAngle > 4096) paAngle = 4096;
  moveToPos(paAngleToSteps(paAngle));
}

// Position melden: measurement-posPayload
//   angle  = Schritt-Position (PA-Winkel)
//   radius = AS5600-Position   (PA-Winkel)
//   sensor = 1 wenn AS5600 am Turm, sonst 0
void sendPos() {
  posPayload p{};
  p.angle  = stepsToPaAngle();
  p.radius = as5600ToPaAngle();
#ifdef AS5600_ON_TOWER
  p.sensor = 1;
#else
  p.sensor = 0;
#endif
  int px, py;
  toPaKart(px, py, p.angle, 1000);   // nur zur Vollstaendigkeit
  p.x = px; p.y = py;
  broadcastMsg(measurement, p);
}

// --- Init ---
void initHw() {
  Serial.begin(115200);
  initPixel();
  pinMode(water,         OUTPUT);  digitalWrite(water,         LOW);
  pinMode(extPower,      OUTPUT);  digitalWrite(extPower,      LOW);
  pinMode(wirelessPower, OUTPUT);  digitalWrite(wirelessPower, HIGH);  // Turm-Sensoren versorgen
  pinMode(laser,         OUTPUT);  digitalWrite(laser,         LOW);
  pinMode(taster,    INPUT_PULLUP);
  pinMode(reedLeft,  INPUT_PULLUP);
  pinMode(reedRight, INPUT_PULLUP);
  I2Cone.begin(pinSDA, pinSCL, 100000U);
}

void initStepper() {
  for (uint8_t p = 0; p < 8; p++) rotor.pinMode(p, OUTPUT);
  rotor.digitalWrite(EN,  LOW);
  rotor.digitalWrite(SLP, HIGH);
  rotor.digitalWrite(RST, LOW);
  delay(100);
  rotor.digitalWrite(RST, HIGH);
  rotor.digitalWrite(STEP, LOW);
  rotor.digitalWrite(DIR,  dirLeft);
  setMicrostep(defaultMicrostep);
}

void initWinkel() {
  winkel.setDirection();
  winkel.resetCumulativePosition();
}

// Homing wie PA1_1: Mitte zwischen den Reeds = geradeaus (PA 2048).
// Beim Setzen von globalPosition=0 wird auch der AS5600 genullt.
void homeTurret() {
  setMicrostep(defaultMicrostep);
  long guard;

  guard = 0;
  while (!leftReed() && guard < maxHomingMicrosteps) { stepOnce(dirLeft); guard++; }
  if (guard >= maxHomingMicrosteps) { writelnComment("Homing: linker Reed nicht gefunden"); return; }
  long leftPos = globalPosition;

  guard = 0;
  while (!rightReed() && guard < maxHomingMicrosteps) { stepOnce(dirRight); guard++; }
  if (guard >= maxHomingMicrosteps) { writelnComment("Homing: rechter Reed nicht gefunden"); return; }
  long rightPos = globalPosition;

  moveToPos((leftPos + rightPos) / 2);
  globalPosition = 0;
  winkel.resetCumulativePosition();   // AS5600-Null = geradeaus
  homed = true;
  if (DEBUG) Serial << "Homing ok, Spanne " << (rightPos - leftPos) << " Mikroschritte" << endl;
}
