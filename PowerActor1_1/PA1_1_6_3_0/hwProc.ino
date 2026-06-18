// PA1_1_6_3_0 - geraetespezifische Funktionen

void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

// ---------------------------------------------------------------------------
//  Heartbeat / Zustand  (reuse pa2HbPayload -> drop-in kompatibel zu PA2)
// ---------------------------------------------------------------------------
void sendHBmsg() {
  hbState.hb.ip = getLastIpByte();
  broadcastMsg(HB, hbState);   // Header (sender, timeStamp) fuellt die Sendeprozedur
}

void heardBeat() {
  if (millis() - HBtimer >= periodeForHB) {   // ueberlaufsicher
    sendHBmsg();
    HBtimer = millis();
  }
}

void loadHBFromVault() {
  vault.begin("settings", true);
  hbState.limitsActive = vault.getBool ("limitsActive", false);
  hbState.leftLimit    = vault.getFloat("leftLimit",  2048 - systemAngle);
  hbState.rightLimit   = vault.getFloat("rightLimit", 2048 + systemAngle);
  hbState.farLimit     = vault.getFloat("farLimit",   maxDist);
  hbState.nearLimit    = vault.getFloat("nearLimit",  initDeadZone);
  vault.end();
}

void saveHBToVault() {
  vault.begin("settings", false);
  vault.putBool ("limitsActive", hbState.limitsActive);
  vault.putFloat("leftLimit",    hbState.leftLimit);
  vault.putFloat("rightLimit",   hbState.rightLimit);
  vault.putFloat("farLimit",     hbState.farLimit);
  vault.putFloat("nearLimit",    hbState.nearLimit);
  vault.end();
}

void assembleHBmsg() {
  hbState.readyToFire  = false;            // nach Boot immer gesichert
  hbState.hb.ip        = getLastIpByte();
  hbState.hb.HBperiode = periodeForHB;
  loadHBFromVault();
}

// ---------------------------------------------------------------------------
//  Stepper-Low-Level (A4988 ueber PCF8574)
// ---------------------------------------------------------------------------
void setMicrostep(int stepSize) {
  switch (stepSize) {
    case fullStep:      rotor.digitalWrite(MS1,LOW);  rotor.digitalWrite(MS2,LOW);  rotor.digitalWrite(MS3,LOW);  break;
    case halfStep:      rotor.digitalWrite(MS1,HIGH); rotor.digitalWrite(MS2,LOW);  rotor.digitalWrite(MS3,LOW);  break;
    case quaterStep:    rotor.digitalWrite(MS1,LOW);  rotor.digitalWrite(MS2,HIGH); rotor.digitalWrite(MS3,LOW);  break;
    case eighthStep:    rotor.digitalWrite(MS1,HIGH); rotor.digitalWrite(MS2,HIGH); rotor.digitalWrite(MS3,LOW);  break;
    case sixteenthStep: rotor.digitalWrite(MS1,HIGH); rotor.digitalWrite(MS2,HIGH); rotor.digitalWrite(MS3,HIGH); break;
  }
}

// Ein Mikroschritt in Richtung dir; zaehlt globalPosition mit (rechts = +)
void stepOnce(bool dir) {
  rotor.digitalWrite(DIR, dir);
  rotor.digitalWrite(STEP, HIGH);
  delayMicroseconds(stepPulseUs);
  rotor.digitalWrite(STEP, LOW);
  delayMicroseconds(stepPulseUs);
  globalPosition += (dir == dirRight) ? 1 : -1;
}

// Faehrt blockierend auf eine absolute Mikroschritt-Position
void moveToPos(long target) {
  setMicrostep(defaultMicrostep);
  while (globalPosition != target) {
    stepOnce(target > globalPosition ? dirRight : dirLeft);
    ArduinoOTA.handle();        // waehrend langer Fahrten reaktiv bleiben
  }
}

bool leftReed()  { return !digitalRead(reedLeft);  }
bool rightReed() { return !digitalRead(reedRight); }

// ---------------------------------------------------------------------------
//  Winkel-Abbildung PA-Welt (0..4096, 2048 = geradeaus) <-> Mikroschritte
//  Nullpunkt (geradeaus) entspricht globalPosition 0 nach homeTurret().
// ---------------------------------------------------------------------------
long paAngleToSteps(float paAngle) {
  return lround((paAngle - 2048.0) * (double)microstepsPerTurretRev / 4096.0);
}

bool targetWithinLimits(const posPayload &obs) {
  if (!hbState.limitsActive) return true;
  if (obs.angle  < hbState.leftLimit || obs.angle  > hbState.rightLimit) return false;
  if (obs.radius < hbState.nearLimit || obs.radius > hbState.farLimit)   return false;
  return true;
}

// Turm auf einen PA-Winkel richten (auf Limits geklemmt)
void aimAt(float paAngle) {
  if (!homed) return;
  if (hbState.limitsActive) {
    if (paAngle < hbState.leftLimit)  paAngle = hbState.leftLimit;
    if (paAngle > hbState.rightLimit) paAngle = hbState.rightLimit;
  }
  moveToPos(paAngleToSteps(paAngle));
}

void fireWater() {
  digitalWrite(water, HIGH);
  fireOn = true;
  fireTimer = millis();
}

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------
void initHw() {
  Serial.begin(115200);
  initPixel();
  pinMode(water,         OUTPUT);  digitalWrite(water,         LOW);
  pinMode(extPower,      OUTPUT);  digitalWrite(extPower,      LOW);
  pinMode(wirelessPower, OUTPUT);  digitalWrite(wirelessPower, HIGH);   // Turm-Sensoren versorgen
  pinMode(laser,         OUTPUT);  digitalWrite(laser,         LOW);
  pinMode(taster,    INPUT_PULLUP);
  pinMode(reedLeft,  INPUT_PULLUP);
  pinMode(reedRight, INPUT_PULLUP);
  I2Cone.begin(pinSDA, pinSCL, 100000U);
}

void initStepper() {
  for (uint8_t p = 0; p < 8; p++) rotor.pinMode(p, OUTPUT);
  rotor.digitalWrite(EN,  LOW);     // Treiber aktiv
  rotor.digitalWrite(SLP, HIGH);    // nicht schlafen
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

// ---------------------------------------------------------------------------
//  Homing des Drehturms  (wie PA2, Schwenkbereich +-180 Grad)
//
//  Die beiden Reed-Kontakte sind die Endlagen des Schwenkbereichs. Es wird nach
//  links bis reedLeft und nach rechts bis reedRight gefahren, die Mitte =
//  "geradeaus" (PA 2048) und dort globalPosition = 0 gesetzt. Mit maxAngle 180
//  spannt der nutzbare Bereich +-180 Grad um geradeaus (voller Kreis), die
//  Limits sind per Default 0..4096 (in der hwDef/Vault anpassbar).
// ---------------------------------------------------------------------------
void homeTurret() {
  setMicrostep(defaultMicrostep);
  long guard;

  // nach links bis linker Reed
  guard = 0;
  while (!leftReed() && guard < maxHomingMicrosteps) { stepOnce(dirLeft); guard++; }
  if (guard >= maxHomingMicrosteps) { writelnComment("Homing: linker Reed nicht gefunden"); return; }
  long leftPos = globalPosition;

  // nach rechts bis rechter Reed
  guard = 0;
  while (!rightReed() && guard < maxHomingMicrosteps) { stepOnce(dirRight); guard++; }
  if (guard >= maxHomingMicrosteps) { writelnComment("Homing: rechter Reed nicht gefunden"); return; }
  long rightPos = globalPosition;

  // Mitte anfahren, dort = geradeaus = Nullpunkt
  moveToPos((leftPos + rightPos) / 2);
  globalPosition = 0;
  homed = true;
  if (DEBUG) Serial << "Homing ok, Spanne " << (rightPos - leftPos)
                    << " Mikroschritte, AS5600 " << winkel.getCumulativePosition() << endl;
}
