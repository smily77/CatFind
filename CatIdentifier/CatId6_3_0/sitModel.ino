// sitModel.ino - Zielmodell "Sitzende Katze" (Port von VPS/dashboard/sitmodel.py).
//
// Konzept: GesamtKonzeptCatFinder.md, Abschnitt "Zielmodell Sitzende Katze".
// ZWEITES, UNABHAENGIGES Modell neben dem Normalmodell (catTrack.ino): eigener
// Zustand, eigene Parameter, eigene Meldungen (targetDetected/targetTracked).
// Gemeinsam ist nur der Empfang der catObserved samt Vorfiltern (aktiv, No-Shot-
// Karte, NoShot-Zeit) in handleBusMsg. Ein spaeterer Umzug auf ein eigenes Geraet
// ist damit nur Kopieren dieser Datei + sitModelDef.h.
//
// Das Modell meldet ein Tier, das sich HINSETZT: Sitzplaetze statt Tracks (die
// Radarspur einer sitzenden Katze reisst immer wieder ab), Ausloesen nach sit_s
// ausserhalb des Katzenkorridors bzw. corr_s darin. Ablauf je Meldung wie in
// sitmodel.SitModel.feed - Aenderungen IMMER in beiden Dateien nachziehen.
//
// Parameter + Sperrstellen: GET /sitparams.csv, Korridor: GET /sit_corridor.csv
// (beim Boot und mit der Aktion "Parameter laden"), im LittleFS gecacht.
#include "sitModelDef.h"

#define SIT_PARAMS_PATH "/sitparams.csv"
#define SIT_CORR_PATH   "/sitcorr.csv"
#define SIT_TRACK_HOLD_S 6.0            // Nachfuehrung haelt so lange ueber Radarluecken (PA2
                                        // verliert einen Track nach 2,5 s ohne Nachfuehrung)

static SitParams SP;
static SitCand   sitC[SIT_MAX_CAND];
static SitHist   sitH[SIT_HIST];
static uint8_t   sitHN = 0, sitHHead = 0;
static SitBlock  sitBlk[SIT_MAX_BLOCKS];
static uint8_t   sitBlkN = 0;
static int16_t   sitCellX[SIT_MAX_CELLS], sitCellY[SIT_MAX_CELLS];
static uint16_t  sitCellN = 0;
static int32_t   sitCellMm = 500;

void sitDefaults() {                   // = DEFAULT_SIT in sitmodel.py
  SP.enabled = 1;
  SP.r_mm = 600; SP.tau_s = 8.0f; SP.sit_s = 20.0f; SP.corr_s = 90.0f; SP.gap_s = 25.0f;
  SP.sd_mm = 150; SP.sd_min_mm = 10; SP.min_rate = 1.0f; SP.max_rate = 7.0f;
  SP.arrive_mm = 500; SP.arrive_max_mm = 4000; SP.arrive_window_s = 10.0f; SP.leave_mm = 1000;
  SP.track_period_s = 0.5f; SP.track_max_s = 120.0f;
  SP.hlk_only = 1;
}

static void sitApplyLine(const String& line) {
  int eq = line.indexOf('=');
  if (eq <= 0 || line[0] == '#') return;
  String k = line.substring(0, eq), v = line.substring(eq + 1);
  if (k == "block") {                  // block=x,y,r
    int c1 = v.indexOf(','), c2 = v.indexOf(',', c1 + 1);
    if (c1 > 0 && c2 > c1 && sitBlkN < SIT_MAX_BLOCKS) {
      sitBlk[sitBlkN].x = v.substring(0, c1).toInt();
      sitBlk[sitBlkN].y = v.substring(c1 + 1, c2).toInt();
      sitBlk[sitBlkN].r = v.substring(c2 + 1).toInt();
      sitBlkN++;
    }
    return;
  }
  float f = v.toFloat();
  if      (k == "sit_enabled")     SP.enabled = (int)f;
  else if (k == "sit_r_mm")        SP.r_mm = f;
  else if (k == "sit_tau_s")       SP.tau_s = f;
  else if (k == "sit_s")           SP.sit_s = f;
  else if (k == "sit_corr_s")      SP.corr_s = f;
  else if (k == "sit_gap_s")       SP.gap_s = f;
  else if (k == "sit_sd_mm")       SP.sd_mm = f;
  else if (k == "sit_sd_min_mm")   SP.sd_min_mm = f;
  else if (k == "sit_min_rate")    SP.min_rate = f;
  else if (k == "sit_max_rate")    SP.max_rate = f;
  else if (k == "arrive_mm")       SP.arrive_mm = f;
  else if (k == "arrive_max_mm")   SP.arrive_max_mm = f;
  else if (k == "arrive_window_s") SP.arrive_window_s = f;
  else if (k == "leave_mm")        SP.leave_mm = f;
  else if (k == "track_period_s")  SP.track_period_s = f;
  else if (k == "track_max_s")     SP.track_max_s = f;
  else if (k == "sit_hlk_only")    SP.hlk_only = (int)f;
}

static void sitForLines(const String& body, void (*fn)(const String&)) {
  int start = 0;
  while (start < (int)body.length()) {
    int nl = body.indexOf('\n', start);
    String line = (nl < 0) ? body.substring(start) : body.substring(start, nl);
    line.trim();
    if (line.length()) fn(line);
    if (nl < 0) break;
    start = nl + 1;
  }
}

static bool sitApplyParams(const String& body) {
  if (body.indexOf("sit_s=") < 0) return false;          // Plausibilitaet
  sitDefaults();
  sitBlkN = 0;
  sitForLines(body, sitApplyLine);
  return true;
}

static void sitCorrLine(const String& line) {
  if (line.startsWith("cell_mm=")) { sitCellMm = max(100L, line.substring(8).toInt()); return; }
  int c = line.indexOf(',');
  if (c > 0 && sitCellN < SIT_MAX_CELLS) {
    sitCellX[sitCellN] = (int16_t)line.substring(0, c).toInt();
    sitCellY[sitCellN] = (int16_t)line.substring(c + 1).toInt();
    sitCellN++;
  }
}

static bool sitApplyCorridor(const String& body) {
  if (!body.startsWith("cell_mm=")) return false;
  sitCellN = 0;
  sitForLines(body, sitCorrLine);
  return true;
}

static String sitReadFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "";
  String b = f.readString();
  f.close();
  return b;
}

static bool sitHttp(const char* route, String& body) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  if (!http.begin("http://" + ipVPS.toString() + route)) return false;
  http.setTimeout(PARAMS_FETCH_TIMEOUT_MS);
  bool ok = http.GET() == 200;
  if (ok) body = http.getString();
  http.end();
  return ok;
}

// Boot: Cache laden; danach (und bei "Parameter laden") frisch vom VPS + cachen.
void sitLoadCache() {
  sitDefaults();
  sitApplyParams(sitReadFile(SIT_PARAMS_PATH));
  sitApplyCorridor(sitReadFile(SIT_CORR_PATH));
}

bool sitFetch() {
  String b;
  bool ok = false;
  if (sitHttp("/sitparams.csv", b) && sitApplyParams(b)) {
    File f = LittleFS.open(SIT_PARAMS_PATH, "w");
    if (f) { f.print(b); f.close(); }
    ok = true;
  }
  if (sitHttp("/sit_corridor.csv", b) && sitApplyCorridor(b)) {
    File f = LittleFS.open(SIT_CORR_PATH, "w");
    if (f) { f.print(b); f.close(); }
  } else ok = false;
  for (int i = 0; i < SIT_MAX_CAND; i++) sitC[i].used = false;   // frisch anfangen
  return ok;
}

String sitSummary() {
  return "Zielmodell " + String(SP.enabled ? "an" : "aus") + ": sitzt " + String(SP.sit_s, 0) +
         " s (Korridor " + String(SP.corr_s, 0) + " s), Korridor " + String(sitCellN) +
         " Zellen, " + String(sitBlkN) + " Sperrstellen";
}

static bool sitInCorr(float x, float y) {
  int16_t ix = (int16_t)floorf(x / sitCellMm), iy = (int16_t)floorf(y / sitCellMm);
  for (uint16_t i = 0; i < sitCellN; i++)
    if (sitCellX[i] == ix && sitCellY[i] == iy) return true;
  return false;
}

static bool sitBlocked(float x, float y) {
  for (uint8_t i = 0; i < sitBlkN; i++)
    if (hypotf(x - sitBlk[i].x, y - sitBlk[i].y) <= sitBlk[i].r) return true;
  return false;
}

static inline float sitCx(const SitCand& c) { return (float)(c.sx / c.w); }
static inline float sitCy(const SitCand& c) { return (float)(c.sy / c.w); }
static void sitSd(const SitCand& c, float& sdx, float& sdy) {
  double mx = c.sx / c.w, my = c.sy / c.w;
  double vx = c.sxx / c.w - mx * mx, vy = c.syy / c.w - my * my;
  sdx = vx > 0 ? sqrtf((float)vx) : 0; sdy = vy > 0 ? sqrtf((float)vy) : 0;
}

static float sitArrival(const SitCand& c) {
  float cx = sitCx(c), cy = sitCy(c);
  float best = hypotf(c.x0 - cx, c.y0 - cy);
  for (uint8_t i = 0; i < c.preN; i++) {
    float d = hypotf(c.preX[i] - cx, c.preY[i] - cy);
    if (d <= SP.arrive_max_mm && d > best) best = d;
  }
  return best;
}

// = SitModel.reason(c) == "" (alle Bedingungen erfuellt)
static bool sitReady(SitCand& c) {
  float dur = (float)(c.tLast - c.tFirst);
  float cx = sitCx(c), cy = sitCy(c), sdx, sdy;
  sitSd(c, sdx, sdy);
  bool inCorr = sitInCorr(cx, cy);
  float need = inCorr ? SP.corr_s : SP.sit_s;
  c.arriveMm = sitArrival(c);
  c.arrived = c.arriveMm >= SP.arrive_mm;
  if (!c.arrived) return false;
  if (sitBlocked(cx, cy)) return false;
  if (inCorr && SP.corr_s <= 0) return false;
  if (dur < need) return false;
  if (max(sdx, sdy) > SP.sd_mm) return false;
  if (min(sdx, sdy) < SP.sd_min_mm) return false;
  float rate = c.n / max(dur, 0.1f);
  if (rate < SP.min_rate || rate > SP.max_rate) return false;
  return true;
}

static void sitPayload(const SitCand& c, catDetectedPayload& cd) {
  float dur = (float)(c.tLast - c.tFirst);
  cd.worldX = (int32_t)lroundf(sitCx(c));
  cd.worldY = (int32_t)lroundf(sitCy(c));
  cd.score = (uint8_t)min(255, (int)lroundf(dur));
  cd.flags = catDetFlagStationary | (c.inCorr ? catDetFlagCorridor : 0);
  cd.trackMs = (uint32_t)(dur * 1000.0f);
  cd.netMm = (int32_t)lroundf(c.arriveMm);
}

static void sitAnnounce(const SitCand& c) {
  catDetectedPayload cd;
  sitPayload(c, cd);
  if (mowPaused()) return;             // Maeher maeht (mowGuard.ino)
  broadcastMsg(targetDetected, cd);    // doppelt (UDP-Verlustschutz, wie catDetected)
  delay(40);
  broadcastMsg(targetDetected, cd);
  sendUdpTextln("TargetDetected (Zielmodell, Schattenbetrieb) sitzt " + String(cd.score) +
                " s x=" + String(cd.worldX) + " y=" + String(cd.worldY) + " sender=" +
                String(c.sender) + (c.inCorr ? " Korridor" : ""));
}

// Eine Meldung (nach den Vorfiltern in handleBusMsg). = SitModel.feed
void sitFeed(uint8_t sender, int32_t x, int32_t y) {
  if (!SP.enabled) return;
  if (SP.hlk_only && (sender >= deviceCount || device[sender].type != HLK)) return;
  double t = ctNow();
  // 1. Verlauf
  sitH[sitHHead] = {t, sender, x, y};
  sitHHead = (sitHHead + 1) % SIT_HIST;
  if (sitHN < SIT_HIST) sitHN++;
  // 2. abgelaufene Plaetze, 3. Tier hat einen ausgeloesten Platz verlassen
  for (int i = 0; i < SIT_MAX_CAND; i++) {
    SitCand& c = sitC[i];
    if (!c.used) continue;
    if (t - c.tLast > SP.gap_s) { c.used = false; continue; }
    if (c.fired && c.sender == sender && hypotf(x - sitCx(c), y - sitCy(c)) > SP.leave_mm)
      c.used = false;
  }
  // 4. zuordnen oder eroeffnen
  int best = -1; float bd = 0;
  for (int i = 0; i < SIT_MAX_CAND; i++) {
    const SitCand& c = sitC[i];
    if (!c.used || c.sender != sender) continue;
    float d = hypotf(x - sitCx(c), y - sitCy(c));
    if (d <= SP.r_mm && (best < 0 || d < bd)) { best = i; bd = d; }
  }
  if (best < 0) {
    int slot = -1;
    for (int i = 0; i < SIT_MAX_CAND; i++) if (!sitC[i].used) { slot = i; break; }
    if (slot < 0) {                    // aeltesten verdraengen
      slot = 0;
      for (int i = 1; i < SIT_MAX_CAND; i++) if (sitC[i].tLast < sitC[slot].tLast) slot = i;
    }
    SitCand& c = sitC[slot];
    memset(&c, 0, sizeof(c));
    c.used = true; c.sender = sender; c.tFirst = c.tLast = c.tu = t; c.n = 1;
    c.w = 1.0; c.sx = x; c.sy = y; c.sxx = (double)x * x; c.syy = (double)y * y;
    c.x0 = c.lx = x; c.y0 = c.ly = y; c.run = c.frz = 1;
    // juengste Punkte desselben Sensors VOR diesem (ohne den eben eingetragenen)
    for (int k = 1; k < sitHN && c.preN < SIT_PRE; k++) {
      const SitHist& h = sitH[(sitHHead - 1 - k + SIT_HIST) % SIT_HIST];
      if (t - h.t > SP.arrive_window_s) break;
      if (h.sender == sender) { c.preX[c.preN] = h.x; c.preY[c.preN] = h.y; c.preN++; }
    }
    best = slot;
  } else {
    SitCand& c = sitC[best];
    c.maxgap = max(c.maxgap, (float)(t - c.tLast));
    c.tLast = t; c.n++;
    double a = exp(-(t - c.tu) / SP.tau_s);
    c.w = c.w * a + 1.0;
    c.sx = c.sx * a + x; c.sy = c.sy * a + y;
    c.sxx = c.sxx * a + (double)x * x; c.syy = c.syy * a + (double)y * y;
    c.tu = t;
    c.run = (x == c.lx && y == c.ly) ? c.run + 1 : 1;
    if (c.run > c.frz) c.frz = c.run;
    c.lx = x; c.ly = y;
  }
  SitCand& c = sitC[best];
  // 5. ausloesen (6. Nachfuehrung in sitTick)
  if (!c.fired && sitReady(c)) {
    c.fired = true; c.tFire = c.tTrack = t;
    c.inCorr = sitInCorr(sitCx(c), sitCy(c));
    sitAnnounce(c);
  }
}

// 6. Nachfuehrung ausgeloester Plaetze (aus loop): alle track_period_s, auch wenn das Radar
// die sitzende Katze gerade verloren hat - aber hoechstens SIT_TRACK_HOLD_S nach der letzten
// Meldung und bis track_max_s nach dem Ausloesen. Gleiche Sperren wie catTracked.
void sitTick() {
  if (!SP.enabled || !deviceActive() || ctInQuietWindow() || mowPaused()) return;
  double t = ctNow();
  for (int i = 0; i < SIT_MAX_CAND; i++) {
    SitCand& c = sitC[i];
    if (!c.used || !c.fired || c.done) continue;
    if (t - c.tFire > SP.track_max_s) { c.done = true; continue; }
    if (t - c.tLast > SIT_TRACK_HOLD_S || t - c.tTrack < SP.track_period_s) continue;
    c.tTrack = t;
    catDetectedPayload cd;
    sitPayload(c, cd);
    broadcastMsg(targetTracked, cd);
  }
}
