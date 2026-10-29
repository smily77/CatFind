// Aktive Zeit: Sonnenuntergang (+ start_off_min) bis end_min. end_min zaehlt ab
// Mitternacht des Abends, 1440 = 24:00, bis 1800 = 06:00 am Folgemorgen.
// Sonnenuntergang nach der NOAA-Naeherung (Genauigkeit ~1-2 min), dieselbe Formel
// wie im VPS (visitor.py sunset_min), damit Tab und Geraet dasselbe Fenster zeigen.

// Sonnenuntergang in Minuten ab lokaler Mitternacht fuer Tag yday (0..365)
int sunsetMin(int yday, int utcOffsetMin) {
  double g = 2.0 * PI / 365.0 * (yday + 0.5);    // Mittag des Tages
  double eqt = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g)
                         - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
  double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g)
              + 0.000907 * sin(2 * g) - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
  double lat = SITE_LAT * PI / 180.0;
  double c = cos(90.833 * PI / 180.0) / (cos(lat) * cos(decl)) - tan(lat) * tan(decl);
  c = constrain(c, -1.0, 1.0);
  double ha = acos(c) * 180.0 / PI;
  return (int)lround(720.0 - 4.0 * (SITE_LON - ha) - eqt) + utcOffsetMin;
}

static int utcOffsetMin(const struct tm& lt) {
  return 60 + (lt.tm_isdst > 0 ? 60 : 0);          // MEZ/MESZ (time_zone in xComDef)
}

// Beginn/Ende der aktiven Zeit heute (Minuten ab lokaler Mitternacht)
void activeWindow(int& startMin, int& endMin) {
  getLocalTime(&timeinfo, 0);
  startMin = sunsetMin(timeinfo.tm_yday, utcOffsetMin(timeinfo)) + cfg.start_off_min;
  endMin = cfg.end_min;
}

// Ohne Uhrzeit (NTP nie erreicht) gilt immer als aktiv: lieber einmal zu viel Licht.
bool inActiveTime() {
  if (!timeValid || !getLocalTime(&timeinfo, 0)) return true;
  int m = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int s, e;
  activeWindow(s, e);
  if (m >= s && m < min(e, 1440)) return true;
  return e > 1440 && m < e - 1440;                 // nach Mitternacht bis end_min
}

String hhmm(int m) {
  m = ((m % 1440) + 1440) % 1440;
  char b[6];
  snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60);
  return String(b);
}
