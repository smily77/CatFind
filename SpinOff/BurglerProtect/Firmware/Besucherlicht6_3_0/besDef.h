// Datentypen des Besucherlichts - eigener Header, damit die von der Arduino-IDE
// erzeugten Funktions-Prototypen (z.B. VisTrack& trackFor) die Typen schon kennen.
#pragma once

// Konfiguration vom VPS (config.ino), Standardwerte = visitor.py DEFAULTS
struct VisCfg {
  int   min_events = 12;
  float window_s = 3.0f, min_span_s = 1.0f;
  int   link_mm = 1000;
  float link_s = 1.0f;
  int   on_s = 600;
  int   bri_pct = 70, ct_k = 2700;
  int   start_off_min = 0, end_min = 1440;
  String light[MAX_GROUPS]; int nLight = 0;
  String home[MAX_GROUPS];  int nHome = 0;
  int32_t px[MAX_POLY], py[MAX_POLY]; int nPoly = 0;
  String version = "";
};

// Eine Hue-Gruppe (Raum/Zone) aus GET /groups (hue.ino)
struct HueGroup {
  String id, name;
  bool anyOn, allOn;
  int bri, ct;                       // -1 = nicht gemeldet
};

// Eine Besucher-Spur (visitorRule.ino)
struct VisTrack {
  bool used;
  unsigned long lastMs;
  int32_t x, y;
  unsigned long win[WIN_SLOTS];       // Zeitpunkte der Meldungen im Polygon (Ring)
  uint8_t head, n;
  bool dayNoted;                      // "ausserhalb der Zeit" fuer diese Spur schon gemeldet
};
