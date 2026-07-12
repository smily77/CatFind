// gatewayProc.ino -- Master als Gateway: Bus-Ereignisse gebündelt an den VPS posten.
//
// Der Manager hört ohnehin Multicast (catObserved, HB) und Text-Multicast (Debug).
// Diese werden gepuffert und ~alle GW_FLUSH_MS per HTTP-POST an das VPS-Dashboard
// (ipVPS:80/ingest) geschickt. Faellt der VPS/Manager aus, laeuft das lokale Netz
// weiter -- nur die Visualisierung pausiert.

#include <HTTPClient.h>

#define GW_MAX_EVENTS 80        // Burst-Schutz: max. so viele catObserved pro Push
#define GW_MAX_DEBUG   8
#define GW_MAX_HB     18
#define GW_FLUSH_MS  1500

// ms = millis() beim Empfang: der VPS rechnet daraus ms-genaue Event-Zeiten
// (die Push-Zeit allein wuerde alle Events eines Flushs auf denselben
// Zeitstempel legen -- unbrauchbar fuer Geschwindigkeits-/Track-Analyse).
struct GwEvent { uint8_t sender, sensor; int32_t wx, wy; uint8_t wv; int32_t x, y; uint8_t group;
                 uint32_t ms; int32_t speed; };
static GwEvent gwEvents[GW_MAX_EVENTS]; static int gwEventN = 0;
static uint16_t gwDropped = 0;   // vom Burst-Schutz verworfene Events seit dem letzten Flush
static String  gwDebug[GW_MAX_DEBUG];   static int gwDebugN = 0;
struct GwHb { uint8_t sender, ip; };
static GwHb    gwHb[GW_MAX_HB];         static int gwHbN = 0;
static unsigned long gwLastFlush = 0;

// Einstellungen der Geraete (settingsReport vom Bus). Anders als Events/HB werden sie
// NICHT nach dem Flush geleert, sondern bei jedem Push mitgeschickt -> der VPS kennt den
// aktuellen Stand auch nach einem Neustart. Pro Sender ein Eintrag (jeweils neuester).
#define GW_MAX_SETTINGS deviceCount
struct GwSet { uint8_t sender; uint16_t sup, val, act; bool used; };
static GwSet gwSet[GW_MAX_SETTINGS];
static bool  gwSettingsDirty = false;   // erzwingt einen Push, sobald neue Settings kamen

// Welt-Posen der Geraete (poseReport vom Bus). Der VPS braucht sie, um die
// Erfassungsbereiche (xComDef: covLeft/covRight/covRange) als Sektoren in die
// Welt-Karte zu legen. Wie die Settings: pro Sender der neueste Stand, wird bei
// jedem Push mitgeschickt (nicht geleert). Periodisch fragt der Manager per
// poseRequest-Broadcast nach (Geraete antworten generisch via handleCommonMsg).
#define GW_MAX_POSES deviceCount
#define GW_POSE_REQ_MS 300000UL          // alle 5 min nach Posen fragen
struct GwPose { uint8_t sender, valid; int32_t x, y; float head; int8_t mir; bool used; };
static GwPose gwPose[GW_MAX_POSES];
static bool   gwPosesDirty = false;
static unsigned long gwLastPoseReq = 0;

void gwAddPose(uint8_t sender, const worldPosePayload& wp) {
  int slot = -1;
  for (int i = 0; i < GW_MAX_POSES; i++)
    if (gwPose[i].used && gwPose[i].sender == sender) { slot = i; break; }
  if (slot < 0) for (int i = 0; i < GW_MAX_POSES; i++)
    if (!gwPose[i].used) { slot = i; gwPose[i].used = true; gwPose[i].sender = sender; break; }
  if (slot < 0) return;
  gwPose[slot].valid = wp.validWorldPose;
  gwPose[slot].x = wp.worldX; gwPose[slot].y = wp.worldY;
  gwPose[slot].head = wp.heading; gwPose[slot].mir = wp.mirror;
  gwPosesDirty = true;
}

void gwAddSettings(uint8_t sender, const settingsPayload& sp) {
  gwSettingsDirty = true;
  for (int i = 0; i < GW_MAX_SETTINGS; i++)
    if (gwSet[i].used && gwSet[i].sender == sender) {
      gwSet[i].sup = sp.supported; gwSet[i].val = sp.values; gwSet[i].act = sp.actions; return;
    }
  for (int i = 0; i < GW_MAX_SETTINGS; i++)
    if (!gwSet[i].used) {
      gwSet[i].sender = sender; gwSet[i].sup = sp.supported;
      gwSet[i].val = sp.values; gwSet[i].act = sp.actions; gwSet[i].used = true; return;
    }
}

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
  if (gwEventN >= GW_MAX_EVENTS) { if (gwDropped < 0xFFFF) gwDropped++; return; }
  GwEvent& e = gwEvents[gwEventN++];
  e.sender = sender; e.sensor = p.sensor;
  e.wx = p.worldX; e.wy = p.worldY; e.wv = p.worldValid;
  e.x = p.x; e.y = p.y; e.group = device[sender].group;
  e.ms = millis(); e.speed = p.targetSpeed;
}
void gwAddDebug(const String& s) { if (gwDebugN < GW_MAX_DEBUG) gwDebug[gwDebugN++] = s; }
void gwAddHb(uint8_t sender, uint8_t ip) {
  for (int i = 0; i < gwHbN; i++) if (gwHb[i].sender == sender) { gwHb[i].ip = ip; return; }
  if (gwHbN < GW_MAX_HB) { gwHb[gwHbN].sender = sender; gwHb[gwHbN].ip = ip; gwHbN++; }
}

void gwFlush() {
  if (WiFi.status() != WL_CONNECTED) return;   // Puffer BEHALTEN - naechster Flush versucht es erneut
                                               // (Ueberlauf faengt der Burst-Schutz/gwDropped ab)
  // Eigene Einstellungen des Managers stets aktuell mitfuehren (er sendet sich selbst kein
  // settingsReport per Multicast) -> so erscheint auch der Manager auf der VPS-Steuerseite.
  {
    int slot = -1;
    for (int i = 0; i < GW_MAX_SETTINGS; i++) if (gwSet[i].used && gwSet[i].sender == ID) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < GW_MAX_SETTINGS; i++) if (!gwSet[i].used) { gwSet[i].used = true; gwSet[i].sender = ID; slot = i; break; }
    if (slot >= 0) { gwSet[slot].sup = mySettings.supported; gwSet[slot].val = mySettings.values; gwSet[slot].act = mySettings.actions; }
  }
  if (gwEventN == 0 && gwDebugN == 0 && gwHbN == 0 && !gwSettingsDirty && !gwPosesDirty) return;

  String body;
  body.reserve(2048 + gwEventN * 160);
  body = "{\"now_ms\":" + String((uint32_t)millis())
       + ",\"dropped\":" + String(gwDropped) + ",\"events\":[";
  for (int i = 0; i < gwEventN; i++) {
    GwEvent& e = gwEvents[i]; if (i) body += ',';
    body += "{\"sender\":" + String(e.sender) + ",\"sensor\":" + String(e.sensor)
          + ",\"wx\":" + String(e.wx) + ",\"wy\":" + String(e.wy) + ",\"wv\":" + String(e.wv)
          + ",\"x\":" + String(e.x) + ",\"y\":" + String(e.y) + ",\"group\":" + String(e.group)
          + ",\"ms\":" + String(e.ms) + ",\"speed\":" + String(e.speed) + "}";
  }
  body += "],\"debug\":[";
  for (int i = 0; i < gwDebugN; i++) { if (i) body += ','; body += "\"" + jsonEsc(gwDebug[i]) + "\""; }
  body += "],\"hb\":[";
  for (int i = 0; i < gwHbN; i++) { if (i) body += ','; body += "{\"sender\":" + String(gwHb[i].sender) + ",\"ip\":" + String(gwHb[i].ip) + "}"; }
  body += "],\"settings\":[";
  bool firstS = true;
  for (int i = 0; i < GW_MAX_SETTINGS; i++) if (gwSet[i].used) {
    if (!firstS) body += ','; firstS = false;
    body += "{\"sender\":" + String(gwSet[i].sender) + ",\"sup\":" + String(gwSet[i].sup)
          + ",\"val\":" + String(gwSet[i].val) + ",\"act\":" + String(gwSet[i].act) + "}";
  }
  body += "],\"poses\":[";
  bool firstP = true;
  for (int i = 0; i < GW_MAX_POSES; i++) if (gwPose[i].used) {
    if (!firstP) body += ','; firstP = false;
    body += "{\"sender\":" + String(gwPose[i].sender) + ",\"valid\":" + String(gwPose[i].valid)
          + ",\"x\":" + String(gwPose[i].x) + ",\"y\":" + String(gwPose[i].y)
          + ",\"head\":" + String(gwPose[i].head, 1) + ",\"mir\":" + String(gwPose[i].mir) + "}";
  }
  body += "]}";

  HTTPClient http;
  String url = "http://" + ipVPS.toString() + ":80/ingest";
  int code = -1;
  if (http.begin(url)) {
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(4000);
    code = http.POST(body);
    http.end();
  }
  if (code <= 0) return;               // Verbindungsfehler: Puffer behalten, naechster Flush wiederholt
                                       // (jeder HTTP-Status, auch 4xx/5xx, gilt als zugestellt -
                                       // sonst wuerden Events bei VPS-Fehlern endlos dupliziert)
  gwSettingsDirty = false;
  gwPosesDirty = false;
  gwEventN = gwDebugN = gwHbN = 0; gwDropped = 0;
}

// Ein vom VPS-Webinterface angefordertes Kommando auf den lokalen Bus geben. Der VPS ist
// vom lokalen 192.168.0.x-Netz aus nicht direkt an die Geraete adressierbar -> der Manager
// wirkt als Gateway. target 255 = Broadcast settingsRequest (alle melden ihre Einstellungen),
// target 254 = Broadcast poseRequest (alle melden ihre Welt-Pose -> Erfassungssektoren).
void gwInjectCommand(int target, int cmd, long info) {
  if (target == 255) { broadcastMsg(settingsRequest); return; }
  if (target == 254) { broadcastMsg(poseRequest); return; }
  if (target < 0 || target >= deviceCount) return;
  cmdPayload c; c.cmd = (uint8_t)cmd; c.info = (int32_t)info;
  if (target == ID) {                             // Kommando an den Manager selbst: ein UDP-
    xMsg m;                                       // Unicast an die eigene IP loopt nicht in den
    m.header.version    = XCOM_VERSION;           // eigenen Socket zurueck -> lokal ausfuehren
    m.header.sender     = ID;                     // (cmdSetSetting via handleCommonMsg; die
    m.header.msgCode    = commandMsg;             // VPS-Anzeige aktualisiert der naechste
    m.header.payloadLen = sizeof(c);              // gwFlush aus mySettings)
    memcpy(m.payload, &c, sizeof(c));
    handleCommonMsg(m);
    return;
  }
  unicastMsg(commandMsg, c, device[target].IP);   // sendet nichts, wenn IP noch 0 (nie gesehen)
}

// VPS nach anstehenden Kommandos fragen (GET /commands). Antwort = CSV-Zeilen "target,cmd,info".
void gwPollCommands() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://" + ipVPS.toString() + ":80/commands";
  if (!http.begin(url)) return;
  http.setTimeout(3000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    int start = 0;
    while (start < (int)body.length()) {
      int nl = body.indexOf('\n', start);
      String line = (nl < 0) ? body.substring(start) : body.substring(start, nl);
      line.trim();
      int c1 = line.indexOf(','), c2 = line.indexOf(',', c1 + 1);
      if (c1 > 0 && c2 > c1) {
        int  t  = line.substring(0, c1).toInt();
        int  cc = line.substring(c1 + 1, c2).toInt();
        long ii = line.substring(c2 + 1).toInt();
        gwInjectCommand(t, cc, ii);
        Serial << "inject cmd: target=" << t << " cmd=" << cc << " info=" << ii << endl;
      }
      if (nl < 0) break;
      start = nl + 1;
    }
  }
  http.end();
}

// Aus loop() aufrufen: periodisch flushen und Kommandos vom VPS abholen.
// Zusaetzlich alle GW_POSE_REQ_MS (und einmal kurz nach dem Boot) per Broadcast
// die Welt-Posen der Geraete erfragen — die Antworten (poseReport) sammelt der
// Manager via gwAddPose und pusht sie an den VPS.
void gwTick() {
  if (millis() - gwLastFlush >= GW_FLUSH_MS) { gwFlush(); gwPollCommands(); gwLastFlush = millis(); }
  if ((gwLastPoseReq == 0 && millis() > 15000) ||
      (gwLastPoseReq != 0 && millis() - gwLastPoseReq >= GW_POSE_REQ_MS)) {
    broadcastMsg(poseRequest);
    gwLastPoseReq = millis();
  }
}
