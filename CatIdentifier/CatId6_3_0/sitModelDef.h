// Typdefinitionen des Zielmodells "Sitzende Katze" (sitModel.ino). Eigener Header
// (wie catTrackDef.h), weil der Arduino-Builder Funktions-Prototypen VOR den Code
// der .ino-Dateien setzt.
#pragma once

// Parameter: identische Namen/Bedeutung wie DEFAULT_SIT in VPS/dashboard/sitmodel.py
struct SitParams {
  int   enabled;
  float r_mm, tau_s, sit_s, corr_s, gap_s, sd_mm, sd_min_mm, min_rate, max_rate;
  float arrive_mm, arrive_max_mm, arrive_window_s, leave_mm;
  float track_period_s, track_max_s;
  int   hlk_only;
};

#define SIT_MAX_CAND   16
#define SIT_HIST       64
#define SIT_PRE         8
#define SIT_MAX_BLOCKS  8
#define SIT_MAX_CELLS 400

struct SitHist { double t; uint8_t sender; int32_t x, y; };
struct SitBlock { int32_t x, y, r; };

// Ein Sitzplatz. sx/sy/sxx/syy/w sind GLEITENDE Summen (Abwertung exp(-dt/tau)).
struct SitCand {
  bool     used, arrived, fired, done, inCorr;
  uint8_t  sender;
  double   tFirst, tLast, tu, tFire, tTrack;
  uint32_t n;
  double   w, sx, sy, sxx, syy;
  int32_t  x0, y0, lx, ly;
  int32_t  preX[SIT_PRE], preY[SIT_PRE];
  uint8_t  preN;
  float    arriveMm, maxgap;
  uint16_t run, frz;
};
