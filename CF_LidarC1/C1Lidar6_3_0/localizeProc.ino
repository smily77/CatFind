// localizeProc.ino -- geraetespezifischer Teil der Welt-Lokalisierung.
// Generisch (VPS-HTTP, Pose-Plausibilisierung, NVS-Persistenz inkl. mirror) liegt in
// xComProc: vpsLocalize() / resolvePose() / loadPose() / savePose(). Hier nur das
// Bauen des Lidar-Scans und die Orchestrierung.

// 360-Bin-Hintergrund (bgMin2) -> uint16-Array (mm; 0 = ungueltig/open) fuer den VPS.
static void buildBackgroundScan(uint16_t out[NUM_BINS]) {
  for (int i = 0; i < NUM_BINS; i++)
    out[i] = (bgCnt[i] >= MIN_CAL_SAMPLES && bgMin2[i] < MAX_RANGE_MM) ? (uint16_t)bgMin2[i] : 0;
}

// Pose bestimmen: Hintergrund-Scan an den VPS, Ergebnis von resolvePose gegen die
// NVS-Pose plausibilisieren und nach myPose uebernehmen (oder NVS behalten).
void doLocalize() {
  Serial << "Lokalisierung: Scan an VPS " << ipVPS.toString() << " ..." << endl;
  lidar.stop();                                   // Serial1 ruhig waehrend HTTP
  static uint16_t scan[NUM_BINS];
  buildBackgroundScan(scan);
  int32_t vx = 0, vy = 0; float vh = 0, vinlier = 0; int vmir = 1; String conf = "";
  bool ok = vpsLocalize(scan, NUM_BINS, vx, vy, vh, vmir, conf, vinlier);
  lidar.startScan();

  if (ok) Serial << "VPS-Pose (" << vx / 1000.0f << ", " << vy / 1000.0f << ") m, h=" << vh
                 << " mir=" << vmir << " conf=" << conf << " inlier=" << vinlier << endl;
  resolvePose(ok, vx, vy, vh, vmir, conf, vinlier, hadNVSpose, LOC_MIN_INLIER);
  Serial << (myPose.validWorldPose ? "Pose gueltig." : "nicht lokalisiert.") << endl;
}
