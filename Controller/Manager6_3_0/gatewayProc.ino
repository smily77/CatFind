// gatewayProc.ino -- Master als Gateway: Bus-Ereignisse gebündelt an den VPS posten.
//
// Der Manager hört ohnehin Multicast (catObserved, HB) und Text-Multicast (Debug).
// Diese werden gepuffert und ~alle GW_FLUSH_MS per HTTP-POST an das VPS-Dashboard
// (ipVPS:80/ingest) geschickt. Faellt der VPS/Manager aus, laeuft das lokale Netz
// weiter -- nur die Visualisierung pausiert.

#include <HTTPClient.h>

#define GW_MAX_EVENTS 40        // Burst-Schutz: max. so viele catObserved pro Push
#define GW_MAX_DEBUG   8
#define GW_MAX_HB     18
#define GW_FLUSH_MS  1500

struct GwEvent { uint8_t sender, sensor; int32_t wx, wy; uint8_t wv; int32_t x, y; uint8_t group; };
static GwEvent gwEvents[GW_MAX_EVENTS]; static int gwEventN = 0;
static String  gwDebug[GW_MAX_DEBUG];   static int gwDebugN = 0;
struct GwHb { uint8_t sender, ip; };
static GwHb    gwHb[GW_MAX_HB];         static int gwHbN = 0;
static unsigned long gwLastFlush = 0;

static String jsonEsc(const String& s) {
  String o; o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n' || c == '\r' || c == '\t') o += ' ';
    else o += c;
  }
  return o;
}

void gwAddEvent(uint8_t sender, const posPayload& p) {
  if (gwEventN >= GW_MAX_EVENTS) return;
  GwEvent& e = gwEvents[gwEventN++];
  e.sender = sender; e.sensor = p.sensor;
  e.wx = p.worldX; e.wy = p.worldY; e.wv = p.worldValid;
  e.x = p.x; e.y = p.y; e.group = device[sender].group;
}
void gwAddDebug(const String& s) { if (gwDebugN < GW_MAX_DEBUG) gwDebug[gwDebugN++] = s; }
void gwAddHb(uint8_t sender, uint8_t ip) {
  for (int i = 0; i < gwHbN; i++) if (gwHb[i].sender == sender) { gwHb[i].ip = ip; return; }
  if (gwHbN < GW_MAX_HB) { gwHb[gwHbN].sender = sender; gwHb[gwHbN].ip = ip; gwHbN++; }
}

void gwFlush() {
  if (WiFi.status() != WL_CONNECTED) { gwEventN = gwDebugN = gwHbN = 0; return; }
  if (gwEventN == 0 && gwDebugN == 0 && gwHbN == 0) return;

  String body = "{\"events\":[";
  for (int i = 0; i < gwEventN; i++) {
    GwEvent& e = gwEvents[i]; if (i) body += ',';
    body += "{\"sender\":" + String(e.sender) + ",\"sensor\":" + String(e.sensor)
          + ",\"wx\":" + String(e.wx) + ",\"wy\":" + String(e.wy) + ",\"wv\":" + String(e.wv)
          + ",\"x\":" + String(e.x) + ",\"y\":" + String(e.y) + ",\"group\":" + String(e.group) + "}";
  }
  body += "],\"debug\":[";
  for (int i = 0; i < gwDebugN; i++) { if (i) body += ','; body += "\"" + jsonEsc(gwDebug[i]) + "\""; }
  body += "],\"hb\":[";
  for (int i = 0; i < gwHbN; i++) { if (i) body += ','; body += "{\"sender\":" + String(gwHb[i].sender) + ",\"ip\":" + String(gwHb[i].ip) + "}"; }
  body += "]}";

  HTTPClient http;
  String url = "http://" + ipVPS.toString() + ":80/ingest";
  if (http.begin(url)) {
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(4000);
    http.POST(body);
    http.end();
  }
  gwEventN = gwDebugN = gwHbN = 0;
}

// Aus loop() aufrufen: periodisch flushen.
void gwTick() {
  if (millis() - gwLastFlush >= GW_FLUSH_MS) { gwFlush(); gwLastFlush = millis(); }
}
