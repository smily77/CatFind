// localizeProc.ino -- geraetespezifischer Teil der Welt-Lokalisierung (wie LidarC1).
// Generisch (VPS-HTTP, Quality-Gate, NVS) liegt in xComProc: vpsLocalize()/resolvePose().

// 360-Bin-Hintergrund (bgMin2) -> mm-Array fuer den VPS (0 = ungueltig/offen,
// darunter die vom Drehturm verdeckten Grad).
static void buildBackgroundScan(uint16_t out[NUM_BINS]) {
  for (int i = 0; i < NUM_BINS; i++)
    out[i] = (bgCnt[i] >= MIN_CAL_SAMPLES && bgMin2[i] < MAX_RANGE_MM) ? (uint16_t)bgMin2[i] : 0;
}

// Pose bestimmen. Kein Rueckgriff auf die NVS-Pose: der Traeger steht frei im Feld
// und wird umgestellt - eine alte Pose waere falsch. Nur eine HOCH-Pose zaehlt.
void doLocalize() {
  static uint16_t scan[NUM_BINS];
  buildBackgroundScan(scan);
  int32_t vx = 0, vy = 0; float vh = 0, vinlier = 0; int vmir = 1; String conf = "";
  bool ok = vpsLocalize(scan, NUM_BINS, vx, vy, vh, vmir, conf, vinlier);
  while (Serial2.available()) Serial2.read();      // waehrend HTTP aufgestaute Pakete verwerfen
  resolvePose(ok, vx, vy, vh, vmir, conf, vinlier, false, LOC_MIN_INLIER);
  sendUdpTextln("LD06 VPS: " + (ok ? "x=" + String(vx / 1000.0f, 2) + " y=" + String(vy / 1000.0f, 2) +
                " h=" + String(vh, 1) + " m=" + String(vmir) + " " + conf + " inlier=" + String(vinlier, 2)
                : String("keine Antwort")) + (myPose.validWorldPose ? " -> uebernommen" : " -> verworfen"));
}
