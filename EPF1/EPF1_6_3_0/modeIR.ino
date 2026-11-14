// modeIR.ino -- EPF1-Betriebsart IR-Strahler (EPF1_MODE_IR)
// Der Strahler haengt an der Schaltleitung IR_PIN (Slave-Port SDA, GPIO7): HIGH = ein.
// Schalter wie beim Dome-C1 (stgIrLight + stgIrAuto, VPS/Display zeigen AUS/EIN/AUTO):
//   AUS  -> aus
//   EIN  -> dauernd ein, der Turm bleibt, wo er steht (cmdRichtung/CYD drehen ihn)
//   AUTO -> "Katze anleuchten": jede catObserved (irgendein Sensor) schaltet fuer IR_HOLD_MS
//           ein. Hat die Meldung Welt-Koordinaten und EPF1 eine Welt-Pose, dreht der Turm
//           in Richtung der Katze (laufend nachgefuehrt, begrenzt auf +/-MODE_PAN_LIMIT_DEG).
//           Nach Ablauf aus und zurueck auf 0.
#if defined(EPF1_MODE_IR)

unsigned long irLastCatMs = 0, irLastAimMs = 0, irLastAimSay = 0;
bool     irHadCat = false, irAimPending = false;
int8_t   irOut = -1;                           // -1 = noch nie gesetzt
float    irAimDeg = 0, irAimedDeg = 1000;      // 1000 = noch nicht ausgerichtet
uint8_t  irAimSender = 0;
int32_t  irAimX = 0, irAimY = 0;

static const char* irModeName() {
  return !settingOn(stgIrLight) ? "AUS" : (settingOn(stgIrAuto) ? "AUTO" : "EIN");
}

// Welt-Punkt -> Pan-Winkel relativ zur 0 (Grad), ueber die eigene Pose (wie der Scan:
// lokaler Winkel a <-> Pan-Winkel a)
static float irWorldToPanDeg(int32_t wx, int32_t wy) {
  int xl = 0, yl = 0;
  worldToLocal(wx, wy, myPose, xl, yl);
  return atan2f((float)yl, (float)xl) * 180.0f / (float)M_PI;
}

void requestScan(int) { say("Betriebsart IR: kein Scan"); }

void modeSetup() {
  pinMode(IR_PIN, OUTPUT);
  digitalWrite(IR_PIN, LOW);
  say("Betriebsart " MODE_NAME " bereit: Strahler an GPIO" + String(IR_PIN) + ", Schalter " +
      irModeName() + ", Pan +/-" + String((int)MODE_PAN_LIMIT_DEG) + " Grad");
}

void modeObservation(const xMsg& m) {
  if (m.header.msgCode != catObserved || m.header.sender == ID) return;
  posPayload o;
  if (!getPayload(m, o)) return;
  irLastCatMs = millis(); irHadCat = true;
  if (!o.worldValid || !myPose.validWorldPose) return;
  irAimDeg = irWorldToPanDeg(o.worldX, o.worldY);
  irAimSender = m.header.sender; irAimX = o.worldX; irAimY = o.worldY;
  irAimPending = true;
}

void modeLoop() {
  const bool autoMode = settingOn(stgIrLight) && settingOn(stgIrAuto);
  bool want;
  if (!settingOn(stgIrLight)) want = false;
  else if (!autoMode)         want = true;
  else want = irHadCat && (millis() - irLastCatMs < IR_HOLD_MS);

  if ((int8_t)want != irOut) {
    digitalWrite(IR_PIN, want ? HIGH : LOW);
    if (irOut >= 0) say(String("IR-Strahler ") + irModeName() + ": " + (want ? "EIN" : "AUS"));
    if (!want && irOut == 1 && irAimedDeg < 999) { goHome(1500); irAimedDeg = 1000; }
    irOut = want;
  }

  if (!(autoMode && want && irAimPending) || busy) return;
  if (millis() - irLastAimMs < IR_AIM_MIN_MS) return;
  irAimPending = false;
  float d = constrain(irAimDeg, -MODE_PAN_LIMIT_DEG, MODE_PAN_LIMIT_DEG);
  if (irAimedDeg < 999 && fabsf(d - irAimedDeg) < IR_AIM_DEADBAND_DEG) return;
  panTo(panDegToTicks(d), IR_AIM_MOVE_MS, 0);
  irAimedDeg = d; irLastAimMs = millis();
  if (millis() - irLastAimSay > 5000) {          // Debug-Fenster nicht fluten
    irLastAimSay = millis();
    say("IR zielt auf Katze von #" + String(irAimSender) + " (" + String(irAimX) + "/" + String(irAimY) +
        "): Pan " + String(d, 1) + " Grad" + (d != irAimDeg ? " (Anschlag, Ziel " + String(irAimDeg, 1) + ")" : String("")));
  }
}

bool modeCommand(const cmdPayload&) { return false; }

const char* modeHelp() { return "i IR-Status"; }

bool modeSerial(char c, float) {
  switch (c) {
    case 'i':
      Serial << "IR " << irModeName() << ", Ausgang " << (digitalRead(IR_PIN) ? "EIN" : "AUS")
             << ", letzte Katze vor " << (irHadCat ? String((millis() - irLastCatMs) / 1000) + " s" : String("-"))
             << ", Pose " << (myPose.validWorldPose ? "gueltig" : "ungueltig") << endl;
      return true;
  }
  return false;
}

#endif  // EPF1_MODE_IR
