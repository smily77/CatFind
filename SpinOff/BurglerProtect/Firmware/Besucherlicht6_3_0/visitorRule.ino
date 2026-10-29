// Ausloeseregel (Nachbau von VPS visitor.py evaluate) und Licht-Ablauf.
//   Spur: Meldung gehoert zur naechsten Spur innerhalb link_mm/link_s, sonst neue.
//   Ausloesen: eine Spur hat im Polygon >= min_events Meldungen in window_s, die
//              >= min_span_s auseinanderliegen. Ohne Polygon zaehlt der ganze Quadrant.
//   Halten:    solange das Licht mir gehoert, verlaengert JEDE Meldung im Polygon
//              auf "jetzt + on_s".
//   Hat ein Bewohner das Licht geaendert/ausgeschaltet, waehrend der Besucher noch
//   da ist, schaltet das Geraet erst wieder ein, wenn das Polygon QUIET_REARM_MS
//   leer war (sonst kaempft es gegen den Bewohner).

VisTrack trk[TRACK_SLOTS];

unsigned long lastInsideMs = 0;       // letzte Meldung im Polygon (beliebige Spur)
bool          waitQuiet = false;      // nach Handeingriff: erst Ruhe im Polygon abwarten
unsigned long homeUntilMs = 0;        // "jemand zuhause" gilt bis dann (Hue nicht dauernd fragen)
unsigned long lastPollMs = 0;
unsigned long onAtMs = 0;             // Einschaltzeitpunkt (Referenzwerte danach lesen)
bool          refRead = false;
int           refBri[MAX_GROUPS], refCt[MAX_GROUPS];
unsigned long offFailSinceMs = 0;     // Ausschalten schlug fehl seit ...

static bool insidePoly(int32_t x, int32_t y) {
  if (cfg.nPoly < 3) return true;
  bool in = false;
  for (int i = 0, j = cfg.nPoly - 1; i < cfg.nPoly; j = i++) {
    if ((cfg.py[i] > y) != (cfg.py[j] > y)) {
      float dy = (float)(cfg.py[j] - cfg.py[i]);
      if (x < (cfg.px[j] - cfg.px[i]) * (y - cfg.py[i]) / (dy ? dy : 1e-9f) + cfg.px[i]) in = !in;
    }
  }
  return in;
}

static VisTrack& trackFor(int32_t x, int32_t y, unsigned long now) {
  float link2 = (float)cfg.link_mm * cfg.link_mm;
  VisTrack* best = nullptr; float bd = link2;
  VisTrack* freeSlot = nullptr; VisTrack* oldest = &trk[0];
  for (auto& t : trk) {
    if (t.used && now - t.lastMs > (unsigned long)(cfg.link_s * 1000)) t.used = false;
    if (!t.used) { if (!freeSlot) freeSlot = &t; continue; }
    float d = (float)(x - t.x) * (x - t.x) + (float)(y - t.y) * (y - t.y);
    if (d <= bd) { best = &t; bd = d; }
    if (t.lastMs < oldest->lastMs) oldest = &t;
  }
  if (!best) {
    best = freeSlot ? freeSlot : oldest;
    memset(best, 0, sizeof(VisTrack));
    best->used = true;
  }
  best->lastMs = now; best->x = x; best->y = y;
  return *best;
}

// Ausloesebedingung der Spur nach Hinzufuegen von now
static bool trackFires(VisTrack& t, unsigned long now) {
  t.win[t.head] = now;
  t.head = (t.head + 1) % WIN_SLOTS;
  if (t.n < WIN_SLOTS) t.n++;
  unsigned long winMs = (unsigned long)(cfg.window_s * 1000);
  int cnt = 0; unsigned long first = now;
  for (int k = 0; k < t.n; k++) {
    unsigned long u = t.win[(t.head + WIN_SLOTS - 1 - k) % WIN_SLOTS];
    if (now - u > winMs) break;
    cnt++; first = u;
  }
  return cnt >= cfg.min_events && (now - first) >= (unsigned long)(cfg.min_span_s * 1000);
}

void releaseLight(const String& why) {
  lightMine = false;
  offAtMs = 0;
  note("Licht freigegeben: " + why);
}

static void tryTrigger(VisTrack& t, int32_t x, int32_t y, unsigned long now) {
  if (!inActiveTime()) {
    if (!t.dayNoted) { t.dayNoted = true; note("Besuch ausserhalb der aktiven Zeit, kein Licht"); }
    return;
  }
  if (homeUntilMs && (long)(now - homeUntilMs) < 0) return;
  homeUntilMs = 0;
  if (!hueRead()) { note("Besuch, aber Hue nicht erreichbar (" + hueErr + ")"); homeUntilMs = now + HUE_HOME_HOLD_MS; return; }
  for (int i = 0; i < cfg.nHome; i++) {
    int g = hueFind(cfg.home[i]);
    if (g >= 0 && hueG[g].anyOn) {
      note("Besuch bei " + String(x / 1000.0f, 1) + "/" + String(y / 1000.0f, 1) +
           " m, aber " + hueG[g].name + " brennt -> jemand zuhause");
      homeUntilMs = now + HUE_HOME_HOLD_MS;
      return;
    }
  }
  bool ok = hueSwitchLight(true);
  lightMine = true;                   // auch bei Teilerfolg: was brennt, gehoert mir
  onAtMs = now; refRead = false; offFailSinceMs = 0;
  offAtMs = now + (unsigned long)cfg.on_s * 1000UL;
  lastTrigT = time(nullptr);
  note(String(ok ? "LICHT EIN" : "LICHT EIN (teilweise fehlgeschlagen)") + " - Besuch bei " +
       String(x / 1000.0f, 1) + "/" + String(y / 1000.0f, 1) + " m");
}

// Eine visitorObserved-Meldung verarbeiten (aus handleBusMsg)
void visitorFeed(int32_t x, int32_t y) {
  unsigned long now = millis();
  VisTrack& t = trackFor(x, y, now);
  if (!insidePoly(x, y)) return;
  bool quietBefore = lastInsideMs == 0 || now - lastInsideMs >= QUIET_REARM_MS;
  lastInsideMs = now;
  lastVisitT = time(nullptr);
  if (waitQuiet && quietBefore) waitQuiet = false;
  bool fires = trackFires(t, now);
  if (!settingOn(stgArmed)) return;   // Aufzeichnung macht der VPS ohnehin, hier nur Licht
  if (lightMine) { offAtMs = now + (unsigned long)cfg.on_s * 1000UL; return; }   // halten
  if (waitQuiet || !fires) return;
  tryTrigger(t, x, y, now);
}

// Periodisch: solange das Licht mir gehoert, Hue beobachten; am Ende ausschalten.
void lightTick() {
  if (!lightMine) return;
  unsigned long now = millis();
  bool due = (long)(now - offAtMs) >= 0;
  if (!due && now - lastPollMs < HUE_POLL_MS) return;
  if (!refRead && !due && now - onAtMs < HUE_REF_DELAY_MS) return;
  lastPollMs = now;
  if (!hueRead()) {
    if (due) {                         // Ausschalten nicht moeglich: weiter versuchen, irgendwann aufgeben
      if (!offFailSinceMs) offFailSinceMs = now;
      if (now - offFailSinceMs > HUE_OFF_RETRY_MS) releaseLight("Hue zum Ausschalten nicht erreichbar");
    }
    return;
  }
  // Wurde an "meinem" Licht etwas geaendert, gehoert es nicht mehr mir.
  for (int i = 0; i < cfg.nLight; i++) {
    int g = hueFind(cfg.light[i]);
    if (g < 0) continue;
    const HueGroup& h = hueG[g];
    if (!h.allOn) { releaseLight(h.name + " von Hand geaendert/aus"); waitQuiet = true; return; }
    if (!refRead) { refBri[i] = h.bri; refCt[i] = h.ct; continue; }
    if (h.bri != refBri[i] || h.ct != refCt[i]) { releaseLight(h.name + " gedimmt/umgestellt"); waitQuiet = true; return; }
  }
  refRead = true;
  // Brennt eine andere Zuhause-Gruppe, ist jemand heimgekommen: Licht stehen lassen
  // (bewusst: sonst steht man beim Heimkommen ploetzlich im Dunkeln, User 2026-10-29).
  for (int i = 0; i < cfg.nHome; i++) {
    bool isLight = false;
    for (int j = 0; j < cfg.nLight; j++) if (hueNorm(cfg.home[i]) == hueNorm(cfg.light[j])) isLight = true;
    int g = hueFind(cfg.home[i]);
    if (!isLight && g >= 0 && hueG[g].anyOn) { releaseLight(hueG[g].name + " brennt -> jemand zuhause"); return; }
  }
  if (!due) return;
  if (hueSwitchLight(false)) {
    lightMine = false; offAtMs = 0;
    note("Licht aus (" + String(cfg.on_s) + " s nach der letzten Meldung)");
  } else if (!offFailSinceMs) offFailSinceMs = now;
  else if (now - offFailSinceMs > HUE_OFF_RETRY_MS) releaseLight("Ausschalten fehlgeschlagen");
}
