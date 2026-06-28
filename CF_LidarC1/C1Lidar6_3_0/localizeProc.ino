// localizeProc.ino -- Welt-Pose: NVS lesen/schreiben + VPS-Lokalisierung (HTTP)

// myPose (Infrastruktur) aus den nativen Pose-Variablen (Grad) aktualisieren.
static void applyPose(bool valid) {
  myPose.validWorldPose = valid;
  if (valid) {
    myPose.worldX  = (int32_t)lroundf(poseXmm);
    myPose.worldY  = (int32_t)lroundf(poseYmm);
    myPose.heading = wrap360(poseHeadDeg) * 4096.0f / 360.0f;   // Grad -> PA-Einheiten
  }
}

// Eigene Pose-Persistenz (inkl. mirror, den die generische savePose nicht kennt).
bool loadPoseC1() {
  c1prefs.begin("c1pose", true);
  bool ok = c1prefs.getBool("v", false);
  if (ok) {
    poseXmm     = c1prefs.getFloat("x", 0.0f);
    poseYmm     = c1prefs.getFloat("y", 0.0f);
    poseHeadDeg = c1prefs.getFloat("h", 0.0f);
    poseMirror  = c1prefs.getInt("m", 1);
  }
  c1prefs.end();
  myPose.validWorldPose = false;     // nach Boot erst nach Plausibilisierung true
  return ok;
}

void savePoseC1(bool valid) {
  c1prefs.begin("c1pose", false);
  c1prefs.putBool("v", valid);
  c1prefs.putFloat("x", poseXmm);
  c1prefs.putFloat("y", poseYmm);
  c1prefs.putFloat("h", poseHeadDeg);
  c1prefs.putInt("m", poseMirror);
  c1prefs.end();
}

// 360-Bin-Hintergrund als JSON {"scan":[...]} (mm; 0 = ungueltig/open).
static String buildBackgroundScan() {
  String s; s.reserve(NUM_BINS * 6 + 16);
  s = "{\"scan\":[";
  for (int i = 0; i < NUM_BINS; i++) {
    int v = (bgCnt[i] >= MIN_CAL_SAMPLES && bgMin2[i] < MAX_RANGE_MM) ? (int)bgMin2[i] : 0;
    s += v;
    if (i < NUM_BINS - 1) s += ',';
  }
  s += "]}";
  return s;
}

// Mini-JSON-Helfer fuer die kleine Antwort.
static float jsonNum(const String& s, const char* key) {
  int i = s.indexOf(key); if (i < 0) return NAN;
  i = s.indexOf(':', i);  if (i < 0) return NAN;
  return s.substring(i + 1).toFloat();
}
static String jsonStr(const String& s, const char* key) {
  int i = s.indexOf(key); if (i < 0) return "";
  i = s.indexOf(':', i);  if (i < 0) return "";
  int a = s.indexOf('"', i); if (a < 0) return "";
  int b = s.indexOf('"', a + 1); if (b < 0) return "";
  return s.substring(a + 1, b);
}

// Scan an den VPS schicken, Pose zurueck. true bei HTTP 200 + plausibler Antwort.
static bool vpsLocalize(float& x, float& y, float& head, int& mir, String& conf) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = "http://" + ipVPS.toString() + ":" + String(VPS_PORT) + VPS_PATH;
  if (!http.begin(url)) return false;
  http.setTimeout(VPS_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(buildBackgroundScan());
  bool ok = false;
  if (code == 200) {
    String r = http.getString();
    x = jsonNum(r, "\"x_mm\""); y = jsonNum(r, "\"y_mm\"");
    head = jsonNum(r, "\"heading_deg\""); mir = (int)jsonNum(r, "\"mirror\"");
    conf = jsonStr(r, "\"confidence\"");
    ok = !isnan(x) && !isnan(y) && !isnan(head) && (mir == 1 || mir == -1);
  } else {
    Serial << "VPS HTTP " << code << endl;
  }
  http.end();
  return ok;
}

// Pose bestimmen: NVS-Pose gegen die globale VPS-Pose plausibilisieren, sonst
// VPS-Pose uebernehmen. Bei VPS-Ausfall die NVS-Pose (unbestaetigt) nutzen.
void doLocalize() {
  Serial << "Lokalisierung: Scan an VPS " << ipVPS.toString() << " ..." << endl;
  lidar.stop();                                   // Serial1 ruhig waehrend HTTP
  float vx, vy, vh; int vmir; String conf;
  bool vps = vpsLocalize(vx, vy, vh, vmir, conf);
  lidar.startScan();

  if (vps) {
    Serial << "VPS-Pose (" << vx / 1000.0f << ", " << vy / 1000.0f << ") m, h=" << vh
           << " mir=" << vmir << " conf=" << conf << endl;
    const bool plausibleNVS = hadNVSpose &&
        (sqrtf((poseXmm - vx) * (poseXmm - vx) + (poseYmm - vy) * (poseYmm - vy)) < LOC_PLAUSIBLE_MM) &&
        (fabsf(angDiffDeg(poseHeadDeg, vh)) < LOC_PLAUSIBLE_DEG) &&
        (conf != "NIEDRIG");
    if (plausibleNVS) {
      Serial << "NVS-Pose durch VPS bestaetigt." << endl;
      applyPose(true);                            // NVS-Pose behalten
    } else if (conf == "HOCH" || conf == "MITTEL") {
      poseXmm = vx; poseYmm = vy; poseHeadDeg = wrap360(vh); poseMirror = vmir;
      savePoseC1(true); applyPose(true);
      Serial << "VPS-Pose uebernommen und in NVS gespeichert." << endl;
    } else {
      Serial << "VPS-Konfidenz NIEDRIG, keine valide Pose." << endl;
      applyPose(false);
    }
  } else if (hadNVSpose) {
    Serial << "VPS nicht erreichbar -> NVS-Pose (unbestaetigt) verwenden." << endl;
    applyPose(true);
  } else {
    Serial << "VPS nicht erreichbar und keine NVS-Pose -> nicht lokalisiert." << endl;
    applyPose(false);
  }
}
