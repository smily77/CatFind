// hwProc.ino -- Status-LEDs, Kalibrierung/Perimeter, Init-Orchestrierung

void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

// Status-Pixel je Init-Phase (Detektion ueberschreibt im aktiven Zustand).
void statusLeds() {
  switch (phase) {
    case PH_CALIB:    allPixel(0x0000FF); break;   // blau   = Hintergrund lernen
    case PH_LOCALIZE: allPixel(0xFFFF00); break;   // gelb   = Pose per VPS
    case PH_NOSHOT:   allPixel(0xFF00FF); break;   // violett= No-Shot-Karte laden
    case PH_ACTIVE:   allPixel(0x000000); break;   // aus    = bereit (idle)
    case PH_NOLOC:    allPixel(0xFF0000); break;   // rot    = nicht lokalisiert
    default: break;
  }
}

// Detektions-LEDs: rot wenn aktuell ein Ziel im schiessbaren Bereich, Pixel 1 = Richtung.
void updateDetectLeds() {
  const bool det = (millis() - lastDetectMs) < DETECT_HOLD_MS && settingOn(stgCatLed);  // Anzeige schaltbar
  if (det) { leds[0] = CRGB::Red; leds[1] = CHSV(lastHue, 255, 255); }
  else     { leds[0] = CRGB::Black; leds[1] = CRGB::Black; }
  FastLED.show();
}

void startCalibration() {
  for (int i = 0; i < NUM_BINS; i++) { bgMin1[i] = MAX_RANGE_MM; bgMin2[i] = MAX_RANGE_MM; bgCnt[i] = 0; }
  calibrated = false; calStartMs = millis();
  phase = PH_CALIB; statusLeds();
  Serial << "Phase 1: 20 s Hintergrund lernen (blau) ..." << endl;
}

// Perimeter (Grenzabstand je Bin) lernen + ueber kleine Luecken interpolieren.
// Ohne Wandmodell -- der Sensor steht frei, sieht rundum.
void buildPerimeter() {
  static bool anchor[NUM_BINS];
  for (int i = 0; i < NUM_BINS; i++) {
    perim[i] = MAX_RANGE_MM; anchor[i] = false;
    if (bgCnt[i] >= MIN_CAL_SAMPLES && bgMin2[i] < MAX_RANGE_MM) { perim[i] = bgMin2[i]; anchor[i] = true; }
  }
  static int leftIdx[NUM_BINS], rightIdx[NUM_BINS], leftGap[NUM_BINS], rightGap[NUM_BINS];
  int last = -1, gap = 0;
  for (int k = 0; k < 2 * NUM_BINS; k++) {
    const int i = k % NUM_BINS;
    if (anchor[i]) { last = i; gap = 0; } else { gap++; }
    if (k >= NUM_BINS) { leftIdx[i] = last; leftGap[i] = gap; }
  }
  last = -1; gap = 0;
  for (int k = 2 * NUM_BINS - 1; k >= 0; k--) {
    const int i = k % NUM_BINS;
    if (anchor[i]) { last = i; gap = 0; } else { gap++; }
    if (k < NUM_BINS) { rightIdx[i] = last; rightGap[i] = gap; }
  }
  int anchors = 0, openBins = 0;
  for (int i = 0; i < NUM_BINS; i++) {
    if (anchor[i]) { anchors++; continue; }
    const int li = leftIdx[i], ri = rightIdx[i];
    if (li >= 0 && ri >= 0) {
      const int span = leftGap[i] + rightGap[i];
      if (span <= PERIM_MAX_GAP_DEG && span > 0) {
        const float t = (float)leftGap[i] / (float)span;
        perim[i] = perim[li] + (perim[ri] - perim[li]) * t;
        continue;
      }
    }
    perim[i] = MAX_RANGE_MM; openBins++;
  }
  perimAnchors = anchors; perimOpen = openBins;
  Serial << "Perimeter: " << anchors << " Anker, " << openBins << " offen." << endl;
}

// Knapper Status-Multicast nach Abschluss der Initialisierung.
void sendStatusText() {
  String s = "LidarC1 ";
  if (myPose.validWorldPose) {
    s += "loc=1 x=" + String(myPose.worldX / 1000.0f, 1) + " y=" + String(myPose.worldY / 1000.0f, 1)
       + " h=" + String((int)lroundf(myPose.heading * 360.0f / 4096.0f));
  } else {
    s += "loc=0";
  }
  s += noShotOK ? " ns=1" : " ns=0";
  sendUdpTextln(s);
  Serial << "Status: " << s << endl;
}

// Abschluss der Lernphase: Perimeter -> VPS-Pose -> No-Shot -> aktiv.
void finishInit() {
  calibrated = true;
  Serial << "Phase 1 fertig." << endl;
  buildPerimeter();

  phase = PH_LOCALIZE; statusLeds();
  doLocalize();                                   // setzt myPose.validWorldPose

  phase = PH_NOSHOT; statusLeds();
  noShotOK = acquireNoShot(NOSHOT_PATH, device[Manager].IP, MAP_WAIT_MS);

  phase = myPose.validWorldPose ? PH_ACTIVE : PH_NOLOC;
  statusLeds();
  sendStatusText();
  sendSettingsReport();              // Einstellungen erneut annoncieren (Display/VPS)
  Serial << "Phase 2: Ueberwachung aktiv." << endl;
}
