// modeScan.ino -- EPF1-Betriebsarten mit Entfernungsmesser am Slave-Port (GPIO7/8)
//   EPF1_MODE_LIDAR -> TOFSense-F   (rangerTOF.ino)
//   EPF1_MODE_X40   -> X40-Laser    (rangerX40.ino)
// Rundumscan ueber das ganze Pan-Fenster in 1-Grad-Schritten -> 360-Bin-Scan (mm,
// 0 = ungueltig) an den VPS-Localizer (/localize, wie LidarC1). Automatisch
// AUTO_SCAN_DELAY_MS nach dem Boot und per Aktion "Kalibrieren" / CYD SCAN.
// Eine vorhandene Pose wird nicht ueberschrieben (siehe unten).
#if defined(EPF1_MODE_LIDAR) || defined(EPF1_MODE_X40)

bool     scanRequested = false, autoScanDone = false;
int      scanStepDeg = 1;                              // Schrittweite des naechsten Scans
uint16_t scanBins[360];

// step: Schrittweite in Grad (Grobscan z.B. 5; 0/ungueltig = SCAN_STEP_DEFAULT)
void requestScan(int step) {
  scanStepDeg = (step >= 1 && step <= 30) ? step : SCAN_STEP_DEFAULT;
  scanRequested = true;
}

static void doScanAndLocalize() {
  busy = true;
  doScan();
  busy = false;
}

void doScan() {
  if (!rangerPresent()) rangerFind();
  if (!rangerPresent()) { say("Scan abgebrochen: kein " RANGER_NAME); return; }
  say("Rundumscan startet (ganzes Pan-Fenster, Schritt " + String(scanStepDeg) + " Grad, Pan-0 = " + String(panOrigin) + " Ticks)");
  setPix(0x000020);
  memset(scanBins, 0, sizeof(scanBins));
  tilt.moveTime(tiltDegToTicks(0), 800, 400);
  // ganzes physisches Fenster in 1-Grad-Schritten, immer von unten angefahren
  // (Getriebespiel einseitig); Bin = Ist-Winkel relativ zur 0
  pan.moveTime(max(0, PAN_PHYS_MIN - (int)(5 * PAN_TICKS_PER_DEG)), 2500, 400);
  waitMs(2800);
  rangerScanBegin();
  int nValid = 0, nStat[3] = {0, 0, 0};
  const int nSteps = (int)((PAN_PHYS_MAX - PAN_PHYS_MIN) / (PAN_TICKS_PER_DEG * scanStepDeg));
  for (int k = 0; k <= nSteps; k++) {
    uint16_t tk = clampPan(lroundf(PAN_PHYS_MIN + k * scanStepDeg * PAN_TICKS_PER_DEG));
    int deg = (int)lroundf(panTicksToDeg(tk));
    pan.moveTime(tk, SCAN_STEP_MS, 0);
    waitMs(SCAN_STEP_MS + SCAN_SETTLE_MS);
    uint16_t st;
    uint16_t d = rangerMeasure(st);
    uint16_t ist = 0; float a = deg;
    if (pan.readPresentPosition(ist)) a = panTicksToDeg(ist);      // Ist-Winkel (Getriebe)
    int bin = ((int)lroundf(a) % 360 + 360) % 360;
    if (d) { scanBins[bin] = d; nValid++; }
    nStat[st == 0 ? 0 : (st == 0xFFFF ? 2 : 1)]++;
    if ((k * scanStepDeg) % 60 == 0)
      Serial << "  " << deg << " Grad: " << d << " mm (Status " << st << ")" << endl;
  }
  rangerScanEnd();
  goHome(2500);
  say("Scan fertig: " + String(nValid) + " gueltige Bins; gueltig: " + String(nStat[0]) +
      ", ungueltig: " + String(nStat[1]) + ", keine Messung: " + String(nStat[2]));
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
  // Eine vorhandene Pose (z.B. von Hand gesetzt) wird NICHT ueberschrieben: das
  // Quality-Gate hat draussen wie drinnen falsche HOCH-Posen durchgelassen. Der Scan
  // meldet dann nur die Abweichung. Neu lokalisieren = zuerst "Pose loeschen".
  if (myPose.validWorldPose) {
    if (ok) {
      float dh = fmodf(vh - myPose.heading * 360.0f / 4096.0f + 540.0f, 360.0f) - 180.0f;
      say("Pose behalten; Scan-Vorschlag weicht ab um " +
          String(hypotf(vx - myPose.worldX, vy - myPose.worldY) / 1000.0f, 2) + " m / " +
          String(dh, 1) + " Grad (mir " + String(vm) + " statt " + String(myPose.mirror) + ")");
    }
    setPix(idleColor());
    return;
  }
  resolvePose(ok, vx, vy, vh, vm, conf, vin, false, LOC_MIN_INLIER);
  if (myPose.validWorldPose) hadNVSpose = true;
  say(String("Pose ") + (myPose.validWorldPose ? "gueltig" : "UNGUELTIG") +
      " (x=" + String(myPose.worldX) + " y=" + String(myPose.worldY) + ")");
  sendPoseReport();
  setPix(idleColor());
}

// ----------------------------------------------------------------- Schnittstelle zum Kern
void modeSetup() {
  rangerFind();
  if (rangerPresent()) rangerTest();
  say("Betriebsart " MODE_NAME " bereit, Rundumscan in " + String(AUTO_SCAN_DELAY_MS / 1000) + " s");
}

void modeLoop() {
  rangerPoll();
  if (!autoScanDone && millis() - bootMs >= AUTO_SCAN_DELAY_MS) { autoScanDone = true; requestScan(SCAN_STEP_AUTO); }
  if (scanRequested) { scanRequested = false; autoScanDone = true; doScanAndLocalize(); }
}

bool modeCommand(const cmdPayload& c) {
  if (c.cmd == cmdCalibrate) { requestScan(c.info); return true; }   // info = Schritt in Grad
  if (c.cmd == cmdLaser) {                       // info 1 = an, 0 = aus; bricht den Auto-Scan ab
    autoScanDone = true;
    if (!busy) rangerLaser(c.info != 0);
    return true;
  }
  return false;
}

void modeObservation(const xMsg&) {}

const char* modeHelp() { return "c <schritt> Scan | d Messen | s Entfernungsmesser suchen | L/l Laser an/aus"; }

bool modeSerial(char c, float arg) {
  switch (c) {
    case 'c': requestScan((int)arg); return true;
    case 's': rangerFind(); return true;
    case 'L': autoScanDone = true; rangerLaser(true); return true;
    case 'l': rangerLaser(false); return true;
    case 'd': rangerTest(); return true;
  }
  return false;
}

#endif  // EPF1_MODE_LIDAR || EPF1_MODE_X40
