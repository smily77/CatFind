// catTrack.ino - Echtzeit-Katzenerkennung auf dem ESP32 (Streaming-Port des
// VPS-Referenzmodells VPS/dashboard/catmodel.py).
//
// Unterschied zur VPS-Version: dort wird ein fertiger Track als Ganzes bewertet,
// hier muss KAUSAL entschieden werden, waehrend die Katze noch im Feld ist.
// Darum fliessen nur die zum Zeitpunkt verfuegbaren Merkmale ein (kein
// Austritts-Bonus/-Malus, kein Sprung-Malus); die Bestaetigung feuert, sobald
// der Score die Schwelle erreicht - wie t_confirm im VPS-Modell.
//
// Parameter: identische Namen wie DEFAULT_PARAMS in catmodel.py; sie kommen als
// key=value-Zeilen vom VPS (/aparams.csv) und werden im LittleFS gecacht.

void announceCat(const catDetectedPayload& cd);   // im Hauptsketch

// ------------------------------------------------------------------ Parameter
// (Struct-Definitionen in catTrackDef.h - Arduino-Autoprototypen brauchen sie frueher)
CatParams CTP;

// Defaults = DEFAULT_PARAMS in catmodel.py (Stand der letzten Modell-Iteration).
void ctSetDefaults() {
  CTP.gate_speed_mm_s = 4000; CTP.gate_base_mm = 400;
  CTP.track_gap_s = 1.5f; CTP.stitch_gap_s = 6.0f; CTP.stitch_dist_mm = 700;
  CTP.move_min_points = 4; CTP.move_window_s = 1.2f; CTP.move_min_net_mm = 400;
  CTP.speed_min_mm_s = 60; CTP.speed_max_mm_s = 4000; CTP.speed_ok_ratio = 0.7f;
  CTP.min_confirm_weight = 0.9f;
  CTP.w_hlk = 1.0f; CTP.w_lidar = 0.3f; CTP.w_default = 0.6f;
  CTP.confirm_score = 60; CTP.score_move = 50; CTP.score_entry = 18;
  CTP.score_fusion = 25; CTP.score_crossing = 12; CTP.score_long = 8;
  CTP.score_stationary = 8; CTP.penalty_mid_birth = 12; CTP.penalty_short = 10;
  CTP.crossing_net_mm = 800; CTP.long_track_points = 10; CTP.short_track_s = 0.8f;
  CTP.max_path_mm = 40000; CTP.edge_dist_mm = 900;
  CTP.stationary_end_s = 3.0f; CTP.stationary_radius_mm = 350;
  CTP.storm_window_s = 3.0f; CTP.storm_rate_evts_s = 8.0f;
  CTP.storm_min_events = 12; CTP.storm_scatter_mm = 2500;
  CTP.fusion_distance_mm = 500; CTP.fusion_time_s = 0.7f;
}

// einen key=value-Eintrag anwenden (ms-Parameter werden hier in Sekunden gefuehrt)
static bool ctApplyParam(const String& k, float v) {
  if      (k == "gate_speed_mm_s")     CTP.gate_speed_mm_s = v;
  else if (k == "gate_base_mm")        CTP.gate_base_mm = v;
  else if (k == "track_gap_ms")        CTP.track_gap_s = v / 1000.0f;
  else if (k == "stitch_gap_ms")       CTP.stitch_gap_s = v / 1000.0f;
  else if (k == "stitch_dist_mm")      CTP.stitch_dist_mm = v;
  else if (k == "move_min_points")     CTP.move_min_points = (int)v;
  else if (k == "move_window_ms")      CTP.move_window_s = v / 1000.0f;
  else if (k == "move_min_net_mm")     CTP.move_min_net_mm = v;
  else if (k == "speed_min_mm_s")      CTP.speed_min_mm_s = v;
  else if (k == "speed_max_mm_s")      CTP.speed_max_mm_s = v;
  else if (k == "speed_ok_ratio")      CTP.speed_ok_ratio = v;
  else if (k == "min_confirm_weight")  CTP.min_confirm_weight = v;
  else if (k == "type_weight.HLK")     CTP.w_hlk = v;
  else if (k == "type_weight.Lidar")   CTP.w_lidar = v;
  else if (k == "type_weight.default") CTP.w_default = v;
  else if (k == "confirm_score")       CTP.confirm_score = (int)v;
  else if (k == "score_move")          CTP.score_move = (int)v;
  else if (k == "score_entry")         CTP.score_entry = (int)v;
  else if (k == "score_fusion")        CTP.score_fusion = (int)v;
  else if (k == "score_crossing")      CTP.score_crossing = (int)v;
  else if (k == "score_long")          CTP.score_long = (int)v;
  else if (k == "score_stationary")    CTP.score_stationary = (int)v;
  else if (k == "penalty_mid_birth")   CTP.penalty_mid_birth = (int)v;
  else if (k == "penalty_short")       CTP.penalty_short = (int)v;
  else if (k == "crossing_net_mm")     CTP.crossing_net_mm = v;
  else if (k == "long_track_points")   CTP.long_track_points = (int)v;
  else if (k == "short_track_ms")      CTP.short_track_s = v / 1000.0f;
  else if (k == "max_path_mm")         CTP.max_path_mm = v;
  else if (k == "edge_dist_mm")        CTP.edge_dist_mm = v;
  else if (k == "stationary_end_s")    CTP.stationary_end_s = v;
  else if (k == "stationary_radius_mm") CTP.stationary_radius_mm = v;
  else if (k == "storm_window_ms")     CTP.storm_window_s = v / 1000.0f;
  else if (k == "storm_rate_evts_s")   CTP.storm_rate_evts_s = v;
  else if (k == "storm_min_events")    CTP.storm_min_events = (int)v;
  else if (k == "storm_scatter_mm")    CTP.storm_scatter_mm = v;
  else if (k == "fusion_distance_mm")  CTP.fusion_distance_mm = v;
  else if (k == "fusion_time_ms")      CTP.fusion_time_s = v / 1000.0f;
  else return false;                   // unbekannt (z.B. reine VPS-Parameter) - ok
  return true;
}

// key=value-Text (VPS /aparams.csv bzw. LittleFS-Cache) anwenden. Immer von den
// Defaults aus, damit entfernte Parameter auf ihren Default zurueckfallen.
static bool ctApplyParamsText(const String& body) {
  ctSetDefaults();
  int applied = 0, start = 0;
  while (start < (int)body.length()) {
    int nl = body.indexOf('\n', start);
    String line = (nl < 0) ? body.substring(start) : body.substring(start, nl);
    line.trim();
    int eq = line.indexOf('=');
    if (line.length() && line[0] != '#' && eq > 0) {
      if (ctApplyParam(line.substring(0, eq), line.substring(eq + 1).toFloat()))
        applied++;
    }
    if (nl < 0) break;
    start = nl + 1;
  }
  return applied >= 10;                // Plausibilitaet: echte Parameterliste
}

bool ctLoadParamsFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  String body = f.readString();
  f.close();
  return ctApplyParamsText(body);
}

bool ctFetchParams() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = "http://" + ipVPS.toString() + "/aparams.csv";
  if (!http.begin(url)) return false;
  http.setTimeout(PARAMS_FETCH_TIMEOUT_MS);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String body = http.getString();
    if (ctApplyParamsText(body)) {
      File f = LittleFS.open(PARAMS_PATH, "w");   // Cache fuer VPS-Ausfall/Reboot
      if (f) { f.print(body); f.close(); }
      ok = true;
    }
  }
  http.end();
  return ok;
}

String ctParamSummary() {
  return "confirm=" + String(CTP.confirm_score) + " maxPath=" +
         String((int)(CTP.max_path_mm / 1000)) + "m edge=" + String((int)CTP.edge_dist_mm);
}

// ------------------------------------------------- Sensor-Posen & Erfassungsrand
// Sektoren = covL/covR/covRange aus device[] + Welt-Pose (poseReport, mitgehoert).
// ctUnionDepth: wie tief liegt ein Welt-Punkt in der GESAMT-Abdeckung (Vereinigung
// aller Sektoren)? >=0 = drin (mm bis zum Rand, analytisch statt Raster wie auf dem
// VPS), <0 = ausserhalb. Damit zaehlt eine Sensor-Uebergabe nicht als Rand.
static worldPose ctPose[deviceCount];        // validWorldPose=false solange unbekannt

void ctSetPose(uint8_t sender, const worldPosePayload& wp) {
  if (!wp.validWorldPose) return;            // ungueltige Posen nicht uebernehmen
  ctPose[sender].worldX = wp.worldX; ctPose[sender].worldY = wp.worldY;
  ctPose[sender].heading = wp.heading; ctPose[sender].mirror = wp.mirror;
  ctPose[sender].validWorldPose = true;
}

static bool ctEdgeActive() {
  for (int i = 0; i < deviceCount; i++)
    if (device[i].covRangeMm > 0 && ctPose[i].validWorldPose) return true;
  return false;
}

static float ctUnionDepth(int32_t wx, int32_t wy) {
  float best = -1.0f;
  for (int i = 0; i < deviceCount; i++) {
    if (device[i].covRangeMm == 0 || !ctPose[i].validWorldPose) continue;
    int xl, yl;
    worldToLocal(wx, wy, ctPose[i], xl, yl);
    float r = sqrtf((float)xl * xl + (float)yl * yl);
    float rng = device[i].covRangeMm;
    if (r > rng) continue;                                  // ausserhalb dieses Sektors
    float depth = rng - r;
    bool full = (device[i].covRightDeg - device[i].covLeftDeg) >= 360;
    if (!full) {
      float phi = atan2f((float)xl, (float)yl) * 180.0f / M_PI;   // wie toPol
      float margin = min(phi - device[i].covLeftDeg, (float)device[i].covRightDeg - phi);
      if (margin < 0) continue;                             // ausserhalb des Winkels
      depth = min(depth, margin * (float)M_PI / 180.0f * r);      // Bogenabstand zum Seitenrand
    }
    if (depth > best) best = depth;
  }
  return best;
}

// ------------------------------------------------------------- Sturm je Sensor
// Wie detect_storms: hohe Rate UND grosse raeumliche Streuung (0,5-s-Zeitscheiben).
// Ein stuermischer Sensor (Regen, Sonnen-Burst) fuettert das Tracking NICHT.
static CtStormSlot ctStorm[deviceCount][CT_STORM_SLOTS];

static bool ctStormFeedAndCheck(uint8_t sender, double t, int32_t wx, int32_t wy) {
  uint32_t key = (uint32_t)(t * 2.0);        // 0,5-s-Scheibe
  CtStormSlot& s = ctStorm[sender][key % CT_STORM_SLOTS];
  if (s.key != key) { s.key = key; s.cnt = 0; s.x0 = s.x1 = wx; s.y0 = s.y1 = wy; }
  s.cnt++;
  if (wx < s.x0) s.x0 = wx; if (wx > s.x1) s.x1 = wx;
  if (wy < s.y0) s.y0 = wy; if (wy > s.y1) s.y1 = wy;

  int slotsPerWin = (int)(CTP.storm_window_s * 2.0f + 0.5f);
  if (slotsPerWin < 1) slotsPerWin = 1;
  if (slotsPerWin > CT_STORM_SLOTS) slotsPerWin = CT_STORM_SLOTS;
  uint32_t total = 0; int nonEmpty = 0, wide = 0;
  for (int i = 0; i < CT_STORM_SLOTS; i++) {
    CtStormSlot& q = ctStorm[sender][i];
    if (q.cnt == 0 || key - q.key >= (uint32_t)slotsPerWin) continue;   // ausserhalb Fenster
    total += q.cnt; nonEmpty++;
    float dx = q.x1 - q.x0, dy = q.y1 - q.y0;
    if (sqrtf(dx * dx + dy * dy) >= CTP.storm_scatter_mm) wide++;
  }
  uint32_t need = (uint32_t)max((float)CTP.storm_min_events,
                                CTP.storm_rate_evts_s * CTP.storm_window_s);
  // Median-Ersatz: mindestens die Haelfte der belegten Scheiben ist weit gestreut
  return total >= need && nonEmpty > 0 && wide * 2 >= nonEmpty;
}

// -------------------------------------------------------------------- Tracks
static CtTrack ctTr[CT_MAX_TRACKS];

static float ctTypeWeight(uint8_t sender) {
  byte tp = device[sender].type;
  if (tp == HLK)   return CTP.w_hlk;
  if (tp == Lidar) return CTP.w_lidar;
  return CTP.w_default;
}

// chronologisch i-ter Ringpunkt (0 = aeltester)
static inline int ctRingIdx(const CtTrack& tr, int i) {
  return (tr.rh - tr.rn + i + 2 * CT_RING) % CT_RING;
}

// kohaerente Bewegungsphase im Fenster, das mit dem NEUSTEN Punkt endet
// (_find_move prueft alle Fenster - im Streaming kommt jedes Fensterende als
// eigener Punkt an, damit ist die Abdeckung dieselbe).
static bool ctCheckMove(const CtTrack& tr) {
  int first = -1;
  double tNew = tr.rt[ctRingIdx(tr, tr.rn - 1)];
  for (int i = 0; i < tr.rn; i++)
    if (tNew - tr.rt[ctRingIdx(tr, i)] <= CTP.move_window_s) { first = i; break; }
  if (first < 0) return false;
  int cnt = tr.rn - first;
  if (cnt < CTP.move_min_points) return false;
  int a = ctRingIdx(tr, first), b = ctRingIdx(tr, tr.rn - 1);
  float dx = tr.rx[b] - tr.rx[a], dy = tr.ry[b] - tr.ry[a];
  if (sqrtf(dx * dx + dy * dy) < CTP.move_min_net_mm) return false;
  int ok = 0, vs = 0;
  uint32_t senders = 0;
  for (int i = first; i < tr.rn; i++) {
    int ci = ctRingIdx(tr, i);
    senders |= (1u << tr.rs[ci]);
    if (i == first) continue;
    int pi = ctRingIdx(tr, i - 1);
    double dt = tr.rt[ci] - tr.rt[pi];
    if (dt <= 0) continue;
    float ddx = tr.rx[ci] - tr.rx[pi], ddy = tr.ry[ci] - tr.ry[pi];
    float v = sqrtf(ddx * ddx + ddy * ddy) / (float)dt;
    vs++;
    if (v >= CTP.speed_min_mm_s && v <= CTP.speed_max_mm_s) ok++;
  }
  if (vs == 0 || (float)ok / vs < CTP.speed_ok_ratio) return false;
  float wsum = 0;
  for (int s = 0; s < deviceCount; s++)
    if (senders & (1u << s)) wsum += ctTypeWeight(s);
  return wsum >= CTP.min_confirm_weight;
}

// sitzt der Track gerade stabil? (_stationary_end auf dem Ring)
static bool ctCheckStationary(const CtTrack& tr) {
  double tEnd = tr.tLast;
  int first = -1;
  for (int i = 0; i < tr.rn; i++)
    if (tEnd - tr.rt[ctRingIdx(tr, i)] <= CTP.stationary_end_s) { first = i; break; }
  if (first < 0) return false;
  int cnt = tr.rn - first;
  if (cnt < 3) return false;
  if (tEnd - tr.rt[ctRingIdx(tr, first)] < CTP.stationary_end_s * 0.6f) return false;
  float cx = 0, cy = 0;
  for (int i = first; i < tr.rn; i++) { int ci = ctRingIdx(tr, i); cx += tr.rx[ci]; cy += tr.ry[ci]; }
  cx /= cnt; cy /= cnt;
  for (int i = first; i < tr.rn; i++) {
    int ci = ctRingIdx(tr, i);
    float dx = tr.rx[ci] - cx, dy = tr.ry[ci] - cy;
    if (sqrtf(dx * dx + dy * dy) > CTP.stationary_radius_mm) return false;
  }
  return true;
}

// Score kausal bewerten und ggf. catDetected ausloesen
static void ctEvaluate(CtTrack& tr, bool stationaryNow) {
  if (tr.dead || tr.reported) return;
  if (CTP.max_path_mm > 0 && tr.path > CTP.max_path_mm) {   // Maeher/Person: hartes K.o.
    tr.dead = true;
    return;
  }
  if (!tr.moveFound) return;                                // Pflicht wie im VPS-Modell
  int score = CTP.score_move;
  float ndx = tr.xLast - tr.x0, ndy = tr.yLast - tr.y0;
  float net = sqrtf(ndx * ndx + ndy * ndy);
  if (net >= CTP.crossing_net_mm) score += CTP.score_crossing;
  if ((int)tr.n >= CTP.long_track_points) score += CTP.score_long;
  if (tr.tLast - tr.t0 < CTP.short_track_s) score -= CTP.penalty_short;
  if (tr.fusion) score += CTP.score_fusion;
  if (tr.birthDepth > -1.5f) {                              // Randlogik war aktiv
    if (tr.birthDepth >= 0 && tr.birthDepth <= CTP.edge_dist_mm) score += CTP.score_entry;
    else if (tr.birthDepth > CTP.edge_dist_mm) score -= CTP.penalty_mid_birth;
  }
  if (stationaryNow) score += CTP.score_stationary;
  if (score < 0) score = 0; if (score > 100) score = 100;

  if (score >= CTP.confirm_score) {
    tr.reported = true;
    catDetectedPayload cd;
    cd.worldX = tr.xLast; cd.worldY = tr.yLast;
    cd.score = (uint8_t)score;
    cd.flags = (stationaryNow ? catDetFlagStationary : 0) |
               (tr.fusion ? catDetFlagFusion : 0);
    cd.trackMs = (uint32_t)((tr.tLast - tr.t0) * 1000.0);
    cd.netMm = (int32_t)net;
    announceCat(cd);
  }
}

// neues catObserved (Welt-mm) ins Modell
void ctFeed(uint8_t sender, int32_t wx, int32_t wy) {
  double t = millis() / 1000.0;
  if (ctStormFeedAndCheck(sender, t, wx, wy)) return;       // Sturm: keine Evidenz

  // beste Fortsetzung suchen (Gate; nach track_gap bis stitch_gap: Naht wie stitch_tracks)
  int best = -1; float bestd = 1e12f;
  for (int i = 0; i < CT_MAX_TRACKS; i++) {
    CtTrack& tr = ctTr[i];
    if (!tr.used) continue;
    double dt = t - tr.tLast;
    if (dt > CTP.stitch_gap_s) { tr.used = false; continue; }   // abgelaufen
    float gate = (dt <= CTP.track_gap_s)
                   ? CTP.gate_base_mm + CTP.gate_speed_mm_s * (float)max(dt, 0.0)
                   : CTP.stitch_dist_mm;
    float dx = wx - tr.xLast, dy = wy - tr.yLast;
    float d = sqrtf(dx * dx + dy * dy);
    if (d <= gate && d < bestd) { best = i; bestd = d; }
  }
  if (best < 0) {                                           // neuer Track
    int slot = -1; double oldest = 1e18;
    for (int i = 0; i < CT_MAX_TRACKS; i++)
      if (!ctTr[i].used) { slot = i; break; }
    if (slot < 0)                                           // alle belegt: aeltesten opfern
      for (int i = 0; i < CT_MAX_TRACKS; i++)
        if (ctTr[i].tLast < oldest) { oldest = ctTr[i].tLast; slot = i; }
    CtTrack& tr = ctTr[slot];
    memset(&tr, 0, sizeof(tr));
    tr.used = true;
    tr.t0 = tr.tLast = t;
    tr.x0 = tr.xLast = wx; tr.y0 = tr.yLast = wy;
    tr.birthDepth = ctEdgeActive() ? ctUnionDepth(wx, wy) : -2.0f;
    best = slot;
  }
  CtTrack& tr = ctTr[best];

  // Fusion: anderer Sender sieht dasselbe Objekt fast gleichzeitig am fast selben Ort
  if (!tr.fusion) {
    for (int i = tr.rn - 1; i >= 0; i--) {
      int ci = ctRingIdx(tr, i);
      if (t - tr.rt[ci] > CTP.fusion_time_s) break;
      if (tr.rs[ci] == sender) continue;
      float dx = wx - tr.rx[ci], dy = wy - tr.ry[ci];
      if (sqrtf(dx * dx + dy * dy) <= CTP.fusion_distance_mm) { tr.fusion = true; break; }
    }
  }

  // Punkt anhaengen
  float dx = wx - tr.xLast, dy = wy - tr.yLast;
  if (tr.n > 0) tr.path += sqrtf(dx * dx + dy * dy);
  tr.tLast = t; tr.xLast = wx; tr.yLast = wy;
  tr.n++;
  tr.senderMask |= (1u << sender);
  tr.rt[tr.rh] = t; tr.rx[tr.rh] = wx; tr.ry[tr.rh] = wy; tr.rs[tr.rh] = sender;
  tr.rh = (tr.rh + 1) % CT_RING;
  if (tr.rn < CT_RING) tr.rn++;

  if (!tr.moveFound && ctCheckMove(tr)) tr.moveFound = true;
  ctEvaluate(tr, ctCheckStationary(tr));
}

// periodisch: abgelaufene Tracks freigeben (Speicherhygiene; die Bestaetigung
// ist laengst gefallen oder faellt nicht mehr)
void ctTick() {
  static unsigned long lastSweep = 0;
  if (millis() - lastSweep < 2000) return;
  lastSweep = millis();
  double t = millis() / 1000.0;
  for (int i = 0; i < CT_MAX_TRACKS; i++)
    if (ctTr[i].used && t - ctTr[i].tLast > CTP.stitch_gap_s) ctTr[i].used = false;
}
