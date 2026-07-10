// Typdefinitionen des Echtzeit-Modells (catTrack.ino). Eigener Header, weil der
// Arduino-Builder Funktions-Prototypen VOR den Code der .ino-Dateien setzt -
// Structs, die dort als Parameter auftauchen, muessen vorher bekannt sein.
#pragma once

// Modell-Parameter: identische Namen/Bedeutung wie DEFAULT_PARAMS in catmodel.py
// (ms-Werte werden hier in Sekunden gefuehrt).
struct CatParams {
  float gate_speed_mm_s, gate_base_mm;
  float track_gap_s, stitch_gap_s, stitch_dist_mm;
  int   move_min_points; float move_window_s, move_min_net_mm;
  float speed_min_mm_s, speed_max_mm_s, speed_ok_ratio, min_confirm_weight;
  float w_hlk, w_lidar, w_default;
  int   confirm_score, score_move, score_entry, score_fusion, score_crossing,
        score_long, score_stationary, penalty_mid_birth, penalty_short;
  float crossing_net_mm; int long_track_points; float short_track_s;
  float max_path_mm, edge_dist_mm;
  float stationary_end_s, stationary_radius_mm;
  float storm_window_s, storm_rate_evts_s; int storm_min_events; float storm_scatter_mm;
  float fusion_distance_mm; float fusion_time_s;
};

// Sturm-Erkennung: 0,5-s-Zeitscheiben je Sensor (Rate + raeumliche Streuung)
#define CT_STORM_SLOTS 8
struct CtStormSlot { uint32_t key; uint16_t cnt; int32_t x0, x1, y0, y1; };

// Ein aktiver Track (Ring der juengsten Punkte fuer Bewegungsfenster/Fusion/Sitzen)
#define CT_MAX_TRACKS 10
#define CT_RING 48
struct CtTrack {
  bool     used, reported, moveFound, fusion, dead;
  double   t0, tLast;
  int32_t  x0, y0, xLast, yLast;
  uint32_t n;
  float    path;
  bool     birthValid;                 // Randlogik war bei Geburt aktiv (Abdeckung bekannt)
  float    birthDist;                  // SIGNIERTE Distanz zur Abdeckungsgrenze bei Geburt:
                                       // >0 = innen (Tiefe), <0 = aussen (Abstand) - wie die
                                       // VPS-_edge_dist-Logik: Bonus bei |dist|<=edge_dist_mm
  uint32_t senderMask;
  double   rt[CT_RING];
  int32_t  rx[CT_RING], ry[CT_RING];
  uint8_t  rs[CT_RING];                // sender je Ringpunkt
  uint8_t  rn, rh;                     // Anzahl (<=CT_RING), Schreibkopf
};


// Erfassungs-Polygone vom VPS (/coverage_export.csv). Der ESP32 nutzt eine
// Liste aktiver Polygone (inside-any) statt eine teure Polygon-Union zu bauen.
#define CT_MAX_COV_POLYS 12
#define CT_MAX_COV_VERTS 16
struct CtCoveragePoly {
  bool     used;
  uint8_t  sender;
  uint8_t  n;
  int32_t  x[CT_MAX_COV_VERTS];
  int32_t  y[CT_MAX_COV_VERTS];
};
