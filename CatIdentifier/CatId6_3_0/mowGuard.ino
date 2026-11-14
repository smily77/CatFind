// Maeher-Erkennung (2026-11-14): MAEHT der Rasenmaeher gerade? Dann pausiert die
// Katzenerkennung (kein catDetected/catTracked/targetDetected), sofern der Modell-
// Parameter mow_pause = 1 ist. Hintergrund: am 08.11. maehte er ab 14:32, also vor dem
// festen Ruhefenster, und Dutzende Maeher-Spuren wurden als Katze bestaetigt.
//
// "Nicht im Dock" heisst NICHT "maeht" (Winterquartier, steht woanders, ausserhalb des
// Bildes) - dann muss die Erkennung laufen. Deshalb braucht "maeht" einen positiven Grund:
//   (a) Abfahrt: KIVision sah ihn im Dock und jetzt nicht mehr -> mow_depart_ms lang
//   (b) Fahrspuren: in mow_window_ms kamen >= mow_path_mm Weg aus LANGEN Spuren
//       (ab mow_p0_mm Spurweg) zusammen, oder die Kamera sah ihn fahrend auf dem Rasen;
//       das haelt mow_hold_ms an. Auswertung 60 Tage: an allen Maehtagen erfuellt, kein
//       einziger als Katze bewerteter Track kam auch nur in die Naehe (laengster 25 m).
// Nie "maeht": Dock meldet "im Dock", Nacht (KIVision IR-Nacht) oder ausserhalb des
// Tagesfensters mow_day_t0_min..mow_day_t1_min (er maeht nachts nie - Igel).
// Das Modell wird waehrend des Maehens WEITER gefuettert (sonst gaebe es keine Spuren
// als Beweis) - gesperrt werden nur die Meldungen an die Aktoren.

#define MOW_BIN_S     10               // Zeitscheiben fuer den Fahrspur-Weg
#define MOW_BINS      40               // -> Fenster bis 400 s
#define MOW_DOCK_FRESH_MS 30000UL      // KIVision sendet alle 5 s; danach "keine Aussage"
#define MOW_VPS_TIMEOUT_MS 2000

static uint8_t  mowDockRx = mowDockUnknown, mowFlagsRx = 0, mowConfRx = 0;
static unsigned long mowRxMs = 0;
static uint8_t  mowLastKnown = mowDockUnknown;   // letzte Aussage home/away (Abfahrt erkennen)
static double   mowDepartT = -1e9, mowEvidT = -1e9;
static float    mowBin[MOW_BINS];
static uint32_t mowBinKey[MOW_BINS];
static bool     mowNow = false;
static String   mowWhy = "";

void mowNoteMsg(const mowerStatePayload& p) {
  mowDockRx = p.dock; mowFlagsRx = p.flags; mowConfRx = p.conf;
  mowRxMs = millis();
}

// aus ctFeed: Wegzuwachs d eines Tracks, der jetzt insgesamt path lang ist
void mowNotePath(float path, float d) {
  if (path < CTP.mow_p0_mm || d <= 0) return;
  uint32_t key = (uint32_t)(ctNow() / MOW_BIN_S);
  int i = key % MOW_BINS;
  if (mowBinKey[i] != key) { mowBinKey[i] = key; mowBin[i] = 0; }
  mowBin[i] += d;
}

static float mowEvidence(double t) {
  uint32_t now = (uint32_t)(t / MOW_BIN_S);
  uint32_t span = (uint32_t)(CTP.mow_window_s / MOW_BIN_S);
  if (span < 1) span = 1;
  if (span > MOW_BINS) span = MOW_BINS;
  float sum = 0;
  for (int i = 0; i < MOW_BINS; i++)
    if (mowBinKey[i] + span > now && mowBinKey[i] <= now) sum += mowBin[i];
  return sum;
}

// Tagesfenster (lokale Zeit). Uhr nicht gestellt -> als Tag werten (wie das
// Ruhefenster fail-open: dann entscheiden Dock + Fahrspuren allein).
static bool mowDayTime() {
  if (CTP.mow_day_t0_min == CTP.mow_day_t1_min) return true;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  if (lt.tm_year + 1900 < 2020) return true;
  int m = lt.tm_hour * 60 + lt.tm_min;
  return (CTP.mow_day_t0_min < CTP.mow_day_t1_min)
           ? (m >= CTP.mow_day_t0_min && m < CTP.mow_day_t1_min)
           : (m >= CTP.mow_day_t0_min || m < CTP.mow_day_t1_min);
}

static const char* mowDockName(uint8_t d) {
  return d == mowDockHome ? "im Dock" : d == mowDockAway ? "Dock leer" : "Dock ?";
}

// Wechsel an den VPS (Anzeige + Maehfahrten fuer die Coverage). Ohne VPS egal.
static void mowReportVps(bool on) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://" + ipVPS.toString() + "/mower?src=" + String(ID) +
               "&mowing=" + String(on ? 1 : 0) + "&pause=" + String(CTP.mow_pause ? 1 : 0);
  if (!http.begin(url)) return;
  http.setTimeout(MOW_VPS_TIMEOUT_MS);
  http.addHeader("Content-Type", "text/plain");
  http.POST(mowWhy);
  http.end();
}

// aus loop(): Zustand neu bestimmen (1x/s)
void mowTick() {
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  double t = ctNow();
  bool fresh = mowRxMs && millis() - mowRxMs < MOW_DOCK_FRESH_MS;
  uint8_t dock = fresh ? mowDockRx : mowDockUnknown;
  bool night = (fresh && (mowFlagsRx & mowFlagNight)) || !mowDayTime();
  if (night) mowLastKnown = mowDockUnknown;           // Abfahrt nur innerhalb eines Tages
  else if (dock != mowDockUnknown) {
    if (mowLastKnown == mowDockHome && dock == mowDockAway) mowDepartT = t;
    mowLastKnown = dock;
  }
  float ev = mowEvidence(t);
  bool lawn = fresh && (mowFlagsRx & mowFlagLawn);
  if (ev >= CTP.mow_path_mm || lawn) mowEvidT = t;

  bool depart = t - mowDepartT < CTP.mow_depart_s;
  bool evid = t - mowEvidT < CTP.mow_hold_s;
  bool now = !night && dock != mowDockHome && (depart || evid);
  String why = now ? (String(depart ? "Abfahrt aus dem Dock" : "") +
                      (depart && evid ? " + " : "") +
                      (evid ? (lawn ? "Kamera sieht ihn fahren" :
                               "Fahrspuren " + String((int)(ev / 1000)) + " m/" +
                               String((int)CTP.mow_window_s) + " s") : ""))
                   : (night ? "Nacht" : dock == mowDockHome ? "steht im Dock" :
                      "keine Fahrspuren mehr");
  if (now != mowNow) {
    mowNow = now;
    mowWhy = why + ", " + mowDockName(dock);
    sendUdpTextln(String("CatIdent: Maeher ") + (now ? "MAEHT" : "maeht nicht mehr") +
                  " (" + mowWhy + ") - Katzenerkennung " +
                  (now ? (CTP.mow_pause ? "PAUSIERT" : "laeuft weiter (mow_pause=0)")
                       : "laeuft"));
    mowReportVps(now);
  }
}

bool mowMowing() { return mowNow; }

// Sperre fuer catDetected/catTracked/Zielmodell
bool mowPaused() { return CTP.mow_pause && mowNow; }

String mowSummary() {
  return String("maeher=") + (CTP.mow_pause ? "pause" : "aus") + " " +
         String((int)(CTP.mow_path_mm / 1000)) + "m/" + String((int)CTP.mow_window_s) + "s";
}
