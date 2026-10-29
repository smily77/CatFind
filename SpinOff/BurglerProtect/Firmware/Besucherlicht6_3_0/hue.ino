// Philips Hue Bridge lokal (API v1, HTTP): GET /api/<key>/groups liefert alle
// Raeume/Zonen mit Name, state.any_on/all_on und der letzten Gruppen-Einstellung
// (action.bri/ct); PUT /api/<key>/groups/<id>/action schaltet. hueIp und
// hueApiKey stehen in Credentials.h. Gruppen werden ueber den NAMEN gefunden
// (gross/klein und Umlaute egal: "Büro" = "Buro"), die IDs liest jede Abfrage neu.

HueGroup hueG[HUE_MAX_GROUPS];
int hueN = 0;
bool hueTrunc = false;               // Bridge hat mehr Gruppen als Platz

static String hueNorm(String s) {
  s.toLowerCase();
  s.replace("ä", "a"); s.replace("ö", "o"); s.replace("ü", "u");
  s.replace("Ä", "a"); s.replace("Ö", "o"); s.replace("Ü", "u");
  s.trim();
  return s;
}

// Index in hueG zum Gruppennamen, -1 = nicht gefunden
int hueFind(const String& name) {
  String n = hueNorm(name);
  for (int i = 0; i < hueN; i++) if (hueNorm(hueG[i].name) == n) return i;
  return -1;
}

static String hueUrl(const String& tail) {
  return "http://" + String(hueIp) + "/api/" + String(hueApiKey) + tail;
}

// Alle Gruppen lesen. false = Bridge nicht erreichbar / Antwort unbrauchbar.
bool hueRead() {
  if (WiFi.status() != WL_CONNECTED) { hueOk = false; hueErr = "kein WLAN"; return false; }
  HTTPClient http;
  http.useHTTP10(true);              // ohne chunked encoding direkt aus dem Strom parsen
  if (!http.begin(hueUrl("/groups"))) { hueOk = false; hueErr = "URL"; return false; }
  http.setTimeout(HUE_TIMEOUT_MS);
  http.setConnectTimeout(HUE_TIMEOUT_MS);
  int code = http.GET();
  if (code != 200) {
    http.end();
    hueOk = false; hueErr = "HTTP " + String(code);
    return false;
  }
  JsonDocument filter;
  filter["*"]["name"] = true;
  filter["*"]["state"] = true;
  filter["*"]["action"]["bri"] = true;
  filter["*"]["action"]["ct"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err || !doc.is<JsonObject>()) { hueOk = false; hueErr = "JSON"; return false; }
  hueN = 0;
  hueTrunc = false;
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (hueN >= HUE_MAX_GROUPS) { hueTrunc = true; break; }
    JsonObject g = kv.value().as<JsonObject>();
    if (g["name"].isNull()) continue;   // z.B. Fehlermeldung "unauthorized user" (Array, kein Objekt)
    HueGroup& h = hueG[hueN++];
    h.id = kv.key().c_str();
    h.name = g["name"].as<String>();
    h.anyOn = g["state"]["any_on"] | false;
    h.allOn = g["state"]["all_on"] | false;
    h.bri = g["action"]["bri"] | -1;
    h.ct = g["action"]["ct"] | -1;
  }
  if (!hueN) { hueOk = false; hueErr = "keine Gruppen (Key?)"; return false; }
  hueOk = true; hueErr = "";
  return true;
}

// Eine Gruppe schalten. body = JSON fuer /action
static bool huePut(const String& id, const String& body) {
  HTTPClient http;
  if (!http.begin(hueUrl("/groups/" + id + "/action"))) return false;
  http.setTimeout(HUE_TIMEOUT_MS);
  http.setConnectTimeout(HUE_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(body);
  String resp = code == 200 ? http.getString() : "";
  http.end();
  return code == 200 && resp.indexOf("success") >= 0;
}

// Alle Licht-Gruppen ein (on=true) oder aus. Nur ein/aus: die Lampen kommen mit
// ihrer zuletzt eingestellten Helligkeit/Farbe (User 2026-10-29: ein Alarm darf
// nichts verstellen). false = mindestens eine Gruppe schlug fehl.
bool hueSwitchLight(bool on) {
  String body = on ? String("{\"on\":true}") : String("{\"on\":false}");
  bool ok = cfg.nLight > 0;
  for (int i = 0; i < cfg.nLight; i++) {
    int g = hueFind(cfg.light[i]);
    if (g < 0) { ok = false; continue; }
    if (!huePut(hueG[g].id, body)) ok = false;
  }
  return ok;
}

// Gruppen-Check fuer die Anzeige / den Start: Bridge erreichbar, alle Namen da?
void hueCheck() {
  if (!hueRead()) return;
  String missing;
  for (int i = 0; i < cfg.nLight; i++) if (hueFind(cfg.light[i]) < 0) missing += " " + cfg.light[i];
  for (int i = 0; i < cfg.nHome; i++)  if (hueFind(cfg.home[i]) < 0)  missing += " " + cfg.home[i];
  static String lastMissing = "";
  if (missing == lastMissing) { if (missing.length()) hueErr = "Gruppe fehlt:" + missing; return; }
  lastMissing = missing;                 // nur bei Aenderung an den VPS melden
  if (missing.length()) {
    String all;
    for (int i = 0; i < hueN; i++) all += (i ? ", " : "") + hueG[i].name;
    hueErr = "Gruppe fehlt:" + missing;
    sendUdpTextln("Besucherlicht: Hue-Gruppe nicht gefunden:" + missing + " (Bridge hat: " + all + ")" +
                  (hueTrunc ? " - Liste voll, HUE_MAX_GROUPS erhoehen" : ""));
  }
}
