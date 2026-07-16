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
struct GwHb { uint8_t sender, ip; uint16_t dz; };   // dz = Totzone (mm) aus radarHbPayload, 0 = keine
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
void gwAddHb(uint8_t sender, uint8_t ip, uint16_t dz) {
  for (int i = 0; i < gwHbN; i++) if (gwHb[i].sender == sender) { gwHb[i].ip = ip; gwHb[i].dz = dz; return; }
  if (gwHbN < GW_MAX_HB) { gwHb[gwHbN].sender = sender; gwHb[gwHbN].ip = ip; gwHb[gwHbN].dz = dz; gwHbN++; }
}

//---------------------------------------------------------------------------------------
// Karten-Sync mit dem VPS (MapConcept.md). Der Manager bleibt Master im LittleFS;
// der VPS-Editor ist der reguläre Weg fürs ÄNDERN (Annahme-Code hier, Pull über
// /commands+HTTP GET, siehe gwFetchMap). Notweg ohne VPS: Map/<typ>.csv im Repo von
// Hand anpassen + Firmware/LittleFS-Image neu flashen (kein Netzwerk-Endpunkt nötig).
//---------------------------------------------------------------------------------------

// Version/CRC-Cache, damit gwFlush() sie GUENSTIG (ohne LittleFS-Lesen) mitschicken
// kann. Wird beim Boot (ensureMaps -> gwRefreshMapStatus) und nach jeder angenommenen
// Kartenaenderung aktualisiert - dazwischen aendert sich die Datei nicht.
static uint16_t gwMapVer[3] = { 0, 0, 0 };   // Index mapNoShot(1)/mapRasen(2), 0 ungenutzt
static uint32_t gwMapCrc[3] = { 0, 0, 0 };

static const char* gwMapTypeName(uint8_t mapType) { return (mapType == mapRasen) ? "rasen" : "noshot"; }
static const char* gwMapTypePath(uint8_t mapType) { return (mapType == mapRasen) ? RASEN_PATH : NOSHOT_PATH; }

// Aus ensureMaps() (Boot) und nach jeder angenommenen Kartenaenderung aufgerufen.
void gwRefreshMapStatus() {
  uint16_t v; uint32_t crc, len;
  gwMapVer[mapNoShot] = mapFileInfo(NOSHOT_PATH, v, crc, len) ? v : 0;
  gwMapCrc[mapNoShot] = gwMapVer[mapNoShot] ? crc : 0;
  gwMapVer[mapRasen]  = mapFileInfo(RASEN_PATH,  v, crc, len) ? v : 0;
  gwMapCrc[mapRasen]  = gwMapVer[mapRasen]  ? crc : 0;
  if (gwMapVer[mapNoShot] == 0) Serial << "noshot: keine Karte im LittleFS - warte auf VPS/Notweg" << endl;
  else Serial << "noshot map: v" << gwMapVer[mapNoShot] << " crc=0x" << String(gwMapCrc[mapNoShot], HEX) << endl;
  if (gwMapVer[mapRasen] == 0) Serial << "rasen: keine Karte im LittleFS - warte auf VPS/Notweg" << endl;
  else Serial << "rasen map: v" << gwMapVer[mapRasen] << " crc=0x" << String(gwMapCrc[mapRasen], HEX) << endl;
}

static mapInfoPayload gwMapInfoFor(uint8_t mapType) {
  mapInfoPayload info{};
  uint16_t v; uint32_t crc, len;
  if (mapFileInfo(gwMapTypePath(mapType), v, crc, len)) {
    info.mapType = mapType; info.version = v; info.fileCrc = crc; info.totalLen = len;
    info.chunkSize = mapChunkBytes;
    info.chunkCount = len ? (uint16_t)((len + mapChunkBytes - 1) / mapChunkBytes) : 0;
  }
  return info;
}

// Annahme-Code: Rohdaten (Ring-Punkte je Zeile "x,y", Leerzeile = Ringende; eine
// evtl. mitgeschickte erste Kommentarzeile wird verworfen - Version/Header vergibt
// AUSSCHLIESSLICH der Manager) validieren, Version hochzaehlen, atomar ins LittleFS
// schreiben. Bei Fehler: ablehnen, alte Karte bleibt unveraendert aktiv (es gibt
// keinen Zustand "kaputte Karte").
static bool gwAcceptMap(uint8_t mapType, const String& rawCsv) {
  const char* tag  = gwMapTypeName(mapType);
  const char* path = gwMapTypePath(mapType);
  String pendingPath = String(path) + ".pending";

  LittleFS.remove(pendingPath.c_str());
  { File f = LittleFS.open(pendingPath.c_str(), "w");
    if (!f) { sendUdpTextln("Karte " + String(tag) + ": Temp-Datei fehlgeschlagen"); return false; }
    f.print(rawCsv);
    f.close(); }

  // Validierung ueber den vorhandenen Ring-Parser (jeder Ring >= 3 Punkte, Datei
  // parsebar) - derselbe Code, den die Sensoren zum Laden nutzen (loadNoShot ist
  // generisch fuer No-Shot UND Rasen, siehe Dokumentation_6_3.md).
  if (!loadNoShot(pendingPath.c_str())) {
    LittleFS.remove(pendingPath.c_str());
    sendUdpTextln("Karte " + String(tag) + ": Validierung fehlgeschlagen - abgelehnt, alte Karte bleibt aktiv");
    return false;
  }

  uint16_t newVersion = gwMapVer[mapType] + 1;
  String body; body.reserve(rawCsv.length() + 8);
  { File f = LittleFS.open(pendingPath.c_str(), "r");
    bool first = true;
    while (f && f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();                                                      // CR/Whitespace weg (wie loadNoShot)
      if (first && line.startsWith("#")) { first = false; continue; }   // fremden Header verwerfen
      first = false;
      body += line; body += '\n';
    }
    if (f) f.close(); }
  LittleFS.remove(pendingPath.c_str());

  // Das "crc=" im Header ist rein informativ (fuers menschliche Lesen der CSV) -
  // NICHT identisch mit dem fileCrc aus mapFileInfo/mapInfoPayload (der deckt die
  // ganze Datei INKL. Header ab, waere also selbstreferenziell). Hier bewusst nur
  // die CRC ueber die Ring-Daten OHNE Header.
  String header = "# " + String(mapType == mapRasen ? "RasenKarte" : "NoShotZone") +
                  " v" + String(newVersion) + "  crc=0x" +
                  String(crc32Bytes((const uint8_t*)body.c_str(), body.length()), HEX) +
                  "  units=mm  frame=world\n";

  String finalContent = header + body;
  if (finalContent.length() > MAP_RX_BUF_BYTES) {   // dasselbe Limit wie beim UDP-Kartenempfang
    sendUdpTextln("Karte " + String(tag) + ": zu gross (" + String(finalContent.length()) +
                  " > " + String(MAP_RX_BUF_BYTES) + " B) - abgelehnt");
    return false;
  }

  String finalTmp = String(path) + ".tmp";
  LittleFS.remove(finalTmp.c_str());
  { File f = LittleFS.open(finalTmp.c_str(), "w");
    if (!f || f.print(finalContent) != (int)finalContent.length()) {
      if (f) f.close();
      LittleFS.remove(finalTmp.c_str());
      sendUdpTextln("Karte " + String(tag) + ": LittleFS-Schreiben fehlgeschlagen");
      return false;
    }
    f.close(); }
  LittleFS.remove(path);
  LittleFS.rename(finalTmp.c_str(), path);

  gwRefreshMapStatus();
  sendUdpTextln("Karte " + String(tag) + ": uebernommen als v" + String(gwMapVer[mapType]));
  broadcastMsg(mapInfo, gwMapInfoFor(mapType));   // Announce: Sensoren vergleichen + laden bei Abweichung neu
  return true;
}

// Aktuelle Karte (gleich welcher Herkunft - VPS-Weg oder Notweg-Flash) an /mapsync
// melden. Der VPS committet sie bei Versions-/CRC-Abweichung automatisch ins
// Git-Repo (Map/<typ>.csv) - siehe MapConcept.md.
void gwPushMap(uint8_t mapType) {
  if (WiFi.status() != WL_CONNECTED) return;
  const char* tag  = gwMapTypeName(mapType);
  const char* path = gwMapTypePath(mapType);
  uint16_t v; uint32_t crc, len;
  if (!mapFileInfo(path, v, crc, len)) return;      // keine Karte vorhanden -> nichts zu melden
  File f = LittleFS.open(path, "r");
  if (!f) return;
  String content; content.reserve(len + 1);
  while (f.available()) content += (char)f.read();
  f.close();

  HTTPClient http;
  String url = "http://" + ipVPS.toString() + ":80/mapsync?type=" + String(tag) +
               "&version=" + String(v) + "&crc=" + String(crc);
  if (http.begin(url)) {
    http.addHeader("Content-Type", "text/plain");
    http.setTimeout(5000);
    http.POST(content);
    http.end();
  }
}

// Pending Karte vom VPS abholen (regulärer Weg: VPS-Editor -> /commands -> hier).
// Ausgelöst durch cmdMapFetch aus der /commands-Queue (siehe gwInjectCommand).
void gwFetchMap(uint8_t mapType) {
  if (WiFi.status() != WL_CONNECTED) return;
  const char* tag = gwMapTypeName(mapType);
  HTTPClient http;
  String url = "http://" + ipVPS.toString() + ":80/maps/" + String(tag) + "?pending=1";
  if (!http.begin(url)) return;
  http.setTimeout(5000);
  int code = http.GET();
  if (code == 200) {
    String csvBody = http.getString();
    http.end();
    if (csvBody.length() == 0) return;              // leer -> nichts zu tun
    gwAcceptMap(mapType, csvBody);                   // meldet Erfolg/Ablehnung selbst per sendUdpTextln
    gwPushMap(mapType);                               // Annahme (bzw. unveraenderte alte Karte) zurueckmelden
  } else {
    http.end();
    if (code != 204 && code != 404)                  // 204/404 = nichts pending, kein Fehler
      Serial << "gwFetchMap " << tag << ": HTTP " << code << endl;
  }
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
  for (int i = 0; i < gwHbN; i++) { if (i) body += ','; body += "{\"sender\":" + String(gwHb[i].sender) + ",\"ip\":" + String(gwHb[i].ip) + ",\"dz\":" + String(gwHb[i].dz) + "}"; }
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
  // Leichter Kartenstand (Version/CRC, kein Body) - aus dem Cache, kein LittleFS-
  // Zugriff im Hot Path. Der VPS erkennt daran Abweichungen (z.B. nach einem
  // Notweg-Flash) und stoesst per cmdMapPush ueber /commands einen Re-Sync an.
  body += "],\"maps\":[";
  body += "{\"type\":\"noshot\",\"version\":" + String(gwMapVer[mapNoShot]) + ",\"crc\":" + String(gwMapCrc[mapNoShot]) + "}";
  body += ",{\"type\":\"rasen\",\"version\":" + String(gwMapVer[mapRasen]) + ",\"crc\":" + String(gwMapCrc[mapRasen]) + "}";
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
  // Karten-Sync: kein Bus-Kommando, sondern ein direkter HTTP-Aufruf des Managers
  // zum VPS (info = Kartentyp mapNoShot/mapRasen) - siehe cmdMapFetch/cmdMapPush.
  if (cmd == cmdMapFetch) { gwFetchMap((uint8_t)info); return; }
  if (cmd == cmdMapPush)  { gwPushMap((uint8_t)info); return; }
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
