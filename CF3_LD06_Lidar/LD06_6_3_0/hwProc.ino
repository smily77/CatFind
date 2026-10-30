// hwProc.ino -- LD06-Empfang, Pixelring, Hintergrund/Perimeter, Init-Ablauf

void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

void initHw() {
  Serial.begin(115200);
  initPixel();
  FastLED.setBrightness(BRIGHTNESS);
  allPixel(0x000000);
  pinMode(extRelais, OUTPUT); digitalWrite(extRelais, LOW);
  pinMode(laser, OUTPUT);     digitalWrite(laser, LOW);
  pinMode(lidarPwr, OUTPUT);  digitalWrite(lidarPwr, LOW);
}

// ---- LD06 ----------------------------------------------------------------------------
// CRC-8 des LD06 (Polynom 0x4D, MSB zuerst, Start 0) ueber die ersten 46 Bytes.
static uint8_t crcTable[256];
static void initCrc() {
  for (int i = 0; i < 256; i++) {
    uint8_t c = i;
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x4D) : (uint8_t)(c << 1);
    crcTable[i] = c;
  }
}

void initLidar() {
  initCrc();
  Serial2.setRxBufferSize(2048);     // Puffer fuer HTTP-Pausen (Lokalisierung)
  Serial2.begin(LIDAR_BAUD, SERIAL_8N1, dataInPin);
}

static bool lidarMotorOn = false;
void setMotor(bool on) {
  if (on == lidarMotorOn) return;
  lidarMotorOn = on;
  digitalWrite(lidarPwr, on ? HIGH : LOW);
  sendUdpTextln(String("LD06 Motor ") + (on ? "an" : "aus (Ruhe)"));
}
bool motorOn() { return lidarMotorOn; }

// Ein Byte lesen; true = ein vollstaendiges, CRC-gueltiges Paket liegt in mData.
bool readLidar() {
  static int pos = 0;
  uint8_t b = Serial2.read();
  if (pos == 0 && b != 0x54) return false;
  if (pos == 1 && b != 0x2C) { pos = (b == 0x54) ? 1 : 0; return false; }
  rawData[pos++] = b;
  if (pos < sentenceLength) return false;
  pos = 0;
  uint8_t crc = 0;
  for (int i = 0; i < sentenceLength - 1; i++) crc = crcTable[(crc ^ rawData[i]) & 0xFF];
  if (crc != rawData[sentenceLength - 1]) { crcErrors++; return false; }
  calculateLidarData();
  return true;
}

void calculateLidarData() {
  float startA = float(rawData[5] << 8 | rawData[4]) / 100.0f;
  float endA   = float(rawData[43] << 8 | rawData[42]) / 100.0f;
  float span = endA - startA;
  if (span < 0) span += 360.0f;
  float step = span / (dataPointPerSentence - 1);
  sentenceRawStart = startA;
  for (int i = 0; i < dataPointPerSentence; i++) {
    mData[i].dist    = int(rawData[3 * i + 7] << 8 | rawData[3 * i + 6]);
    mData[i].quality = int(rawData[3 * i + 8]);
    float a = startA + i * step;
    if (a >= 360.0f) a -= 360.0f;
    mData[i].winkel = 360.0f - a;                       // LD06 dreht im Uhrzeigersinn
    if (mData[i].winkel >= 360.0f) mData[i].winkel -= 360.0f;
  }
}

// ---- Pixelring -----------------------------------------------------------------------
static int ringIndex(float deg) {
  float p = (deg - RING_PIX0_DEG) * RING_DIR * pixelNum / 360.0f;
  int i = (int)lroundf(p) % pixelNum;
  return i < 0 ? i + pixelNum : i;
}

static void ringFill(CRGB c) { for (int i = 0; i < pixelNum; i++) leds[i] = c; }

// ---- Traeger-Anzeige (PowerActor2, cmdCarrierState) -----------------------------------
// In der Startphase zeigt der Ring den Zustand des ganzen Traegers (Vorschlag 2026-10-30):
// weiss atmend = startet, tuerkiser Punkt = Turm-Richtung beim Schwenk, blauer Komet =
// LD06-Scan, gelbe Punkte = VPS, gruen = Pose + Uebergabe (vorderes Pixel blinkt bis der
// Compact Dome bestaetigt), orange vorn = Uebergabe gescheitert, roter Countdown = neuer
// Versuch, rot atmend = wartet auf "Kalibrieren". csDone/Timeout -> wieder LD06-Anzeige.
static uint8_t  csState = 0;
static int32_t  csVal = 0;
static unsigned long csAt = 0, csSince = 0;
static bool     csSwept[pixelNum];

void noteCarrierState(int32_t info) {
  uint8_t st = info & 0xFF;
  if (st != csState) {
    csSince = millis();
    if (st == csSweep) memset(csSwept, 0, sizeof(csSwept));
  }
  csState = st; csVal = info >> 8; csAt = millis();
}

static bool carrierShown() {
  return csState && csState != csDone && millis() - csAt < CARRIER_TIMEOUT_MS;
}

static void px(int i, CRGB c, float v) {    // Pixel i (beliebig) mit Helligkeit v 0..1 setzen
  i = ((i % pixelNum) + pixelNum) % pixelNum;
  if (v <= 0) return;
  if (v > 1) v = 1;
  c.nscale8_video((uint8_t)(v * 255));
  if (c.getLuma() > leds[i].getLuma()) leds[i] = c;
}

static void drawCarrier() {
  const CRGB white(255, 244, 225), cyan(0, 215, 255), blue(40, 90, 255), yellow(255, 196, 0),
             green(40, 230, 90), red(255, 30, 20), orange(255, 110, 0);
  float t = (millis() - csSince) / 1000.0f;
  ringFill(CRGB::Black);
  switch (csState) {
    case csStart:
      for (int i = 0; i < pixelNum; i++) px(i, white, 0.12f + 0.12f * (1 + sinf(t * 2.4f)) / 2);
      break;
    case csSweep: {
      int k = ringIndex((float)csVal);
      csSwept[k] = true;
      for (int i = 0; i < pixelNum; i++) if (csSwept[i]) px(i, cyan, 0.07f);
      px(k, cyan, 1);                  // Schweif hinter dem Strahl (Schwenk im Uhrzeigersinn
      px(k + RING_DIR, cyan, 0.35f);   // = gegen die Pixelnummern)
      px(k + 2 * RING_DIR, cyan, 0.12f);
      break;
    }
    case csLdScan: {
      int h = (int)(t * pixelNum * 1.2f);
      for (int j = 0; j < 6; j++) px(-(h - j), blue, 1 - j * 0.17f);
      break;
    }
    case csVps: {
      int h = (int)(t * pixelNum * 0.35f);
      for (int o = 0; o < 2; o++) {
        int k = h + o * pixelNum / 2;
        px(k, yellow, 0.9f); px(k - 1, yellow, 0.35f);
      }
      break;
    }
    case csHandOver:
      if (t < 1.6f) { for (int i = 0; i < pixelNum; i++) px(i, green, sinf(PI * t / 1.6f) * 0.9f); }
      else px(ringIndex((float)csVal), green, ((int)(t * 4) % 2) ? 1.0f : 0.1f);
      break;
    case csHandFail:
      px(ringIndex((float)csVal), orange, ((int)(t * 3) % 2) ? 1.0f : 0.08f);
      break;
    case csRetry: {
      int lit = (int)ceilf(pixelNum * csVal / 1000.0f);
      for (int i = 0; i < lit; i++) px(i, red, 0.45f + 0.1f * sinf(t * 3));
      break;
    }
    case csWait:
      for (int i = 0; i < pixelNum; i++) px(i, red, 0.06f + 0.22f * (1 + sinf(t * 1.1f)) / 2);
      break;
  }
  FastLED.show();
}

// Phase-Farben wie LidarC1: blau = Hintergrund lernen, gelb = Pose per VPS,
// violett = No-Shot-Karte, rot = nicht lokalisiert, aus = bereit.
// Bereit: HB = jeder 4. Pixel kurz gruen; Treffer = Pixel in Trefferrichtung, rot wenn
// gemeldet (in der No-Shot-Karte), orange wenn gesehen aber ausserhalb (nicht gemeldet),
// Nachbarn schwaecher. Nur bei Aenderung neu zeichnen.
void updateLeds() {
  static int shown = -1;
  if (carrierShown()) {                // Traeger-Anzeige hat Vorrang (animiert)
    static unsigned long frameAt = 0;
    if (millis() - frameAt >= CARRIER_FRAME_MS) { frameAt = millis(); drawCarrier(); }
    shown = -1;                        // danach eigene Anzeige neu zeichnen
    return;
  }
  int want;
  if (phase == PH_CALIB)         want = 1000;
  else if (phase == PH_LOCALIZE) want = 1001;
  else if (phase == PH_NOSHOT)   want = 1002;
  else if (phase == PH_NOLOC)    want = 1003;
  else {
    bool det = settingOn(stgCatLed) && millis() - lastDetectMs < DETECT_HOLD_MS;
    bool hb  = settingOn(stgHbLed) && millis() < hbBlinkUntil;
    want = det ? ringIndex(lastDirDeg) + (lastDetectReported ? 0 : 100) : (hb ? 2000 : 3000);
  }
  if (want == shown) return;
  shown = want;
  switch (want) {
    case 1000:
      ringFill(CRGB::Blue);
#ifdef RING_TEST                     // Ausrichtung bestimmen: Pixel 0 weiss, Pixel 1 gruen
      leds[0] = CRGB::White; leds[1] = CRGB::Green;
#endif
      break;
    case 1001: ringFill(CRGB::Yellow); break;
    case 1002: ringFill(CRGB::Purple); break;
    case 1003: ringFill(CRGB::Red); break;
    case 2000: ringFill(CRGB::Black); for (int i = 0; i < pixelNum; i += 4) leds[i] = CRGB::Green; break;
    case 3000: ringFill(CRGB::Black); break;
    default: {                       // Trefferrichtung: < 100 rot (gemeldet), >= 100 orange
      bool rep = want < 100;
      int p = want % 100;
      ringFill(CRGB::Black);
      leds[p] = rep ? CRGB(255, 0, 0) : CRGB(255, 90, 0);
      leds[(p + 1) % pixelNum] = rep ? CRGB(60, 0, 0) : CRGB(60, 20, 0);
      leds[(p + pixelNum - 1) % pixelNum] = rep ? CRGB(60, 0, 0) : CRGB(60, 20, 0);
    }
  }
  FastLED.show();
}

// ---- Hintergrund / Perimeter (wie LidarC1) ------------------------------------------
void startCalibration() {
  for (int i = 0; i < NUM_BINS; i++) { bgMin1[i] = MAX_RANGE_MM; bgMin2[i] = MAX_RANGE_MM; bgCnt[i] = 0; }
  calibrated = false;
  calStartMs = millis();
  phase = PH_CALIB;
  setMotor(true);
}

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
}

void sendStatusText() {
  String s = "LD06 ";
  if (myPose.validWorldPose)
    s += "loc=1 x=" + String(myPose.worldX / 1000.0f, 2) + " y=" + String(myPose.worldY / 1000.0f, 2)
       + " h=" + String(myPose.heading * 360.0f / 4096.0f, 1) + " m=" + String(myPose.mirror);
  else s += "loc=0";
  s += noShotOK ? " ns=1" : " ns=0";
  s += " bins=" + String(perimAnchors) + " offen=" + String(perimOpen);
  s += String(" erkennung=") + (deviceActive() ? "an" : "aus");
  if (crcErrors) s += " crcFehler=" + String(crcErrors);
  sendUdpTextln(s);
}

// Lernphase fertig: Perimeter -> VPS-Pose -> No-Shot -> bereit (oder nicht lokalisiert).
void finishInit() {
  calibrated = true;
  buildPerimeter();

  phase = PH_LOCALIZE; updateLeds();
  doLocalize();

  phase = PH_NOSHOT; updateLeds();
  noShotOK = acquireNoShot(NOSHOT_PATH, device[Manager].IP, MAP_WAIT_MS);
  noShotSynced = mapLastSynced;
  lastNoShotTry = millis();

  phase = myPose.validWorldPose ? PH_ACTIVE : PH_NOLOC;
  if (phase == PH_NOLOC) locFailMs = millis();
  sendStatusText();
  sendPoseReport();                  // Pose sofort bekannt machen (Manager/VPS/CatIdent)
  sendSettingsReport();
}

// Zielerkennung zu-/abgeschaltet und Pose-Wiederholung ohne gueltige Pose.
void serviceActivation() {
  static bool wasActive = false;
  bool act = deviceActive();
  if (act != wasActive) {
    wasActive = act;
    if (act && calibrated) {         // Hintergrund ist alt (Motor stand) -> neu lernen
      sendUdpTextln("LD06: Zielerkennung an - Hintergrund neu lernen");
      startCalibration();
    } else if (!act) sendUdpTextln("LD06: Zielerkennung aus (nur Pose)");
  }
  if (phase == PH_NOLOC && myPose.validWorldPose) phase = PH_ACTIVE;   // Pose kam von aussen
  if (phase == PH_NOLOC && millis() - locFailMs >= LOC_RETRY_MS) {
    sendUdpTextln("LD06: noch keine Pose - neuer Versuch");
    startCalibration();
  }
}

void serviceNoShotMap() {
  if (!calibrated) return;
  if (mapRecheckNow) {
    if ((long)(millis() - mapRecheckAt) < 0) return;
  } else {
    unsigned long dueMs = (noShotOK && noShotSynced) ? MAP_RECHECK_MS : MAP_RETRY_MS;
    if (lastNoShotTry != 0 && millis() - lastNoShotTry < dueMs) return;
  }
  lastNoShotTry = millis();
  mapRecheckNow = false;
  noShotOK = acquireNoShot(NOSHOT_PATH, device[Manager].IP, MAP_WAIT_MS);
  noShotSynced = mapLastSynced;
  sendUdpTextln(noShotOK ? "LD06: No-Shot-Karte aktuell" : "LD06: No-Shot-Karte nicht verfuegbar");
}
