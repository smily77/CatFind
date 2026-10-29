// Konfiguration vom VPS: GET /visitor_device.cfg (Textformat, eine Angabe je Zeile
// "schluessel,wert"), wird erst nach der Schlusszeile "end" uebernommen und dann im
// LittleFS gecacht. Mehrfach-Schluessel: light (einzuschaltende Gruppen), home
// (Zuhause-Gruppen), poly (Ecke x,y in Welt-mm).

static bool cfgApplyText(const String& body) {
  VisCfg c;                          // Standardwerte, dann ueberschreiben
  bool done = false;
  int pos = 0;
  while (pos < (int)body.length()) {
    int nl = body.indexOf('\n', pos);
    if (nl < 0) nl = body.length();
    String line = body.substring(pos, nl);
    pos = nl + 1;
    line.trim();
    if (!line.length() || line[0] == '#') continue;
    if (line == "end") { done = true; break; }
    int k = line.indexOf(',');
    if (k < 0) continue;
    String key = line.substring(0, k), val = line.substring(k + 1);
    if      (key == "version")       c.version = val;
    else if (key == "min_events")    c.min_events = constrain(val.toInt(), 1, WIN_SLOTS);
    else if (key == "window_s")      c.window_s = val.toFloat();
    else if (key == "min_span_s")    c.min_span_s = val.toFloat();
    else if (key == "link_mm")       c.link_mm = val.toInt();
    else if (key == "link_s")        c.link_s = val.toFloat();
    else if (key == "on_s")          c.on_s = val.toInt();
    else if (key == "start_off_min") c.start_off_min = val.toInt();
    else if (key == "end_min")       c.end_min = val.toInt();
    else if (key == "light" && c.nLight < MAX_GROUPS) c.light[c.nLight++] = val;
    else if (key == "home"  && c.nHome  < MAX_GROUPS) c.home[c.nHome++] = val;
    else if (key == "poly"  && c.nPoly  < MAX_POLY) {
      int j = val.indexOf(',');
      if (j > 0) { c.px[c.nPoly] = val.substring(0, j).toInt(); c.py[c.nPoly] = val.substring(j + 1).toInt(); c.nPoly++; }
    }
  }
  if (!done) return false;           // abgeschnitten -> alten Stand behalten
  if (c.nPoly < 3) c.nPoly = 0;
  cfg = c;
  return true;
}

bool cfgLoadFile() {
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;
  String body = f.readString();
  f.close();
  return cfgApplyText(body);
}

bool cfgFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  if (!http.begin("http://" + ipVPS.toString() + "/visitor_device.cfg")) return false;
  http.setTimeout(CFG_TIMEOUT_MS);
  http.setConnectTimeout(CFG_TIMEOUT_MS);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String body = http.getString();
    String old = cfg.version;
    if (cfgApplyText(body)) {
      ok = true;
      if (cfg.version != old) {      // nur bei Aenderung schreiben (Flash schonen)
        File f = LittleFS.open(CFG_PATH, "w");
        if (f) { f.print(body); f.close(); }
      }
    }
  }
  http.end();
  return ok;
}

String cfgSummary() {
  String s = "Polygon " + String(cfg.nPoly) + " Ecken, Licht ";
  for (int i = 0; i < cfg.nLight; i++) s += (i ? "+" : "") + cfg.light[i];
  if (!cfg.nLight) s += "-";
  s += ", " + String(cfg.min_events) + " in " + String(cfg.window_s, 1) + " s, an " + String(cfg.on_s) + " s";
  return s;
}
