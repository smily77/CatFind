//boolean setUpWifi(int lastOctet)
//void initMcUdp()
//void initUnicast()
//bool parseXMsg(AsyncUDPPacket &packet, xMsg &m)
//bool getPayload(const xMsg &m, T &out)                          - typsicher, prüft payloadLen
//bool getHbPayload(const xMsg &m, hbPayload &out)                - Basisteil aller HB-Varianten
//bool broadcastMsg(uint8_t msgCode, const void* payload, uint8_t len)
//bool broadcastMsg(uint8_t msgCode, const T &payload)
//bool broadcastMsg(uint8_t msgCode)                              - ohne Payload
//void broadcastRawMsg(const xMsg &m)                             - Header unverändert (Simulator-Playback)
//bool unicastMsg(uint8_t msgCode, const void* payload, uint8_t len, uint8_t lastOctet)
//bool unicastMsg(uint8_t msgCode, const T &payload, uint8_t lastOctet)
//void setUpTime()
//void setUpOTA()
//void initPixel()
//void setPixel(byte led, uint32_t farbe)
//void allPixel(uint32_t farbe)
//void printSensorData(const xMsg &m)
//void printTimePreamble(const xMsg &m)
//void printCmdData(const xMsg &m)
//void toPol(int x, int y, float &phi,float &radius)
//void toKart(int &x, int &y, float phi, float radius)
//void toPaPol(int x, int y, float &phi,float &radius)
//void toPaKart(int &x, int &y, float phi, float radius)
//void localToWorld(int xl, int yl, const worldPose &p, int &xw, int &yw)  - lokal -> Welt (kartesisch)
//void worldToLocal(int xw, int yw, const worldPose &p, int &xl, int &yl)  - Welt -> lokal (kartesisch)
//void savePose(const worldPose &p)                              - Ist-Pose ins NVS schreiben
//bool loadPose(worldPose &p)                                    - Ist-Pose aus NVS lesen (validWorldPose bleibt false)
//uint32_t crc32Bytes(const uint8_t* d, size_t n)                - CRC32 (zlib-kompatibel)
//bool mapFileInfo(const char* path, uint16_t& v, uint32_t& crc, uint32_t& len)  - LittleFS-Karte: Version/CRC/Laenge
//bool serveMap(uint8_t mapType, const char* path, uint8_t reqOctet)  - Karte gechunkt an Anforderer senden (Manager)
//bool requestMap(uint8_t mapType, uint8_t managerOctet)         - Karte beim Manager anfordern (Sensor)
//bool mapBeginRx(mapRxState&, const mapInfoPayload&, const char* path)  - Empfang starten
//int  mapFeedChunk(mapRxState&, const xMsg&)                    - Chunk verarbeiten (0=weiter,1=fertig,-1=Fehler)
//uint8_t getLastIpByte()
//void initText2Udp()
//size_t sendUdpText(const String& text)
//size_t sendUdpTextln(const String& text)

// Ausserhalb
void writelnComment(String comment);
void writeComment(String comment);

// UDP-Text
size_t sendUdpText(const String& text);
size_t sendUdpTextln(const String& text);

// Netz / Hilfsfunktionen
uint8_t getLastIpByte();

// Zeit & WiFi
void    setUpTime();
boolean setUpWifi(int lastOctet);

// UDP: Init & Text-MC
void    initMcUdp();
void    initText2Udp();
void    initUnicast();

// UDP: Senden (Broadcast und Unicast im selben Format)
bool    broadcastMsg(uint8_t msgCode, const void* payload, uint8_t len);
bool    unicastMsg(uint8_t msgCode, const void* payload, uint8_t len, uint8_t lastOctet);
void    broadcastRawMsg(const xMsg &m);

// OTA
void    setUpOTA();

// Ausgabe / Debug
void    printTimePreamble(const xMsg &m);
void    printSensorData(const xMsg &m);
void    printCmdData(const xMsg &m);

// LED-Helfer (nur wenn containLed definiert ist)
#ifdef containLed
void    initPixel();
void    setPixel(byte led, uint32_t farbe);
void    allPixel(uint32_t farbe);
#endif

// Koordinaten-Umrechnung
void    toPol(int x, int y, float &phi, float &radius);
void    toKart(int &x, int &y, float phi, float radius);
void    toPaPol(int x, int y, float &phi, float &radius);
void    toPaKart(int &x, int &y, float phi, float radius);

// Welt-/Lokal-Transformation und Pose-Persistenz
void    localToWorld(int xl, int yl, const worldPose &p, int &xw, int &yw);
void    worldToLocal(int xw, int yw, const worldPose &p, int &xl, int &yl);
void    savePose(const worldPose &p);
bool    loadPose(worldPose &p);


size_t sendUdpText(const String& text) {
  return udpText.writeTo(
    reinterpret_cast<const uint8_t*>(text.c_str()),
    text.length(),
    multiCastIP, MC_Text_PORT
  );
}

size_t sendUdpTextln(const String& text) {
  String line = text; line += "\r\n";
  return sendUdpText(line);
}

uint8_t getLastIpByte() {
  IPAddress ip = WiFi.localIP();
  return ip[3];
}

void setUpTime() {
  writelnComment("set up Time");
  #ifdef containLed
   if (maxPix > pixelNum) allPixel(0xFFFF00);
     else setPixel(minPix,0xFFFF00);
  #endif
  configTzTime(time_zone, ntpServer1, ntpServer2);
  while(!getLocalTime(&timeinfo)){
    delay(50);
  }
  #ifdef containLed
    if (maxPix > pixelNum) allPixel(0x000000);
      else setPixel(minPix,0x000000);
  #endif
}
boolean setUpWifi(int lastOctet) {
  writelnComment("start WiFi");
  WiFi.mode(WIFI_STA);
  if (lastOctet >= 150 && lastOctet <= 195) {
    IPAddress localIP(192, 168, 0, lastOctet);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    if (!WiFi.config(localIP, gateway, subnet))
      if (DEBUG) Serial << "Fehler: Statische IP-Konfiguration fehlgeschlagen. Fallback auf DHCP." << endl;
  }
   WiFi.begin(ssid, password);
  do {
    #ifdef containLed
      if (maxPix > pixelNum) allPixel(0xFF0000);
        else setPixel(minPix,0xFF0000);
    #endif
    writeComment(".");
    delay(250);
     #ifdef containLed
       if (maxPix > pixelNum) allPixel(0x000000);
         else setPixel(minPix,0x000000);
    #endif
    writeComment("-");
    delay(250);
  } while (WiFi.status() != WL_CONNECTED);
  writelnComment(" ");
  writelnComment(WiFi.localIP().toString());
  #ifdef containLed
    setPixel(minPix,0x000000);
  #endif
  return true;
}

//---------------------------------------------------------------------------------------
// Protokoll 6.3: Empfangen
//---------------------------------------------------------------------------------------
// Paket prüfen (Version, Länge) und in xMsg kopieren
bool parseXMsg(AsyncUDPPacket &packet, xMsg &m) {
  if (packet.length() < sizeof(msgHeader)) return false;
  memcpy(&m.header, packet.data(), sizeof(msgHeader));
  if (m.header.version != XCOM_VERSION) return false;
  if (m.header.payloadLen > maxPayloadLen) return false;
  if (packet.length() != sizeof(msgHeader) + m.header.payloadLen) return false;
  if (m.header.payloadLen) memcpy(m.payload, packet.data() + sizeof(msgHeader), m.header.payloadLen);
  return true;
}

// Payload typsicher auslesen - false wenn die Länge nicht zum Struct passt
template <typename T>
bool getPayload(const xMsg &m, T &out) {
  if (m.header.payloadLen != sizeof(T)) return false;
  memcpy(&out, m.payload, sizeof(T));
  return true;
}

// HB-Basisteil auslesen - funktioniert für hbPayload, pa2HbPayload und radarHbPayload,
// weil der gemeinsame Teil bei allen HB-Varianten am Anfang steht
bool getHbPayload(const xMsg &m, hbPayload &out) {
  if (m.header.msgCode != HB) return false;
  if (m.header.payloadLen < sizeof(hbPayload)) return false;
  memcpy(&out, m.payload, sizeof(hbPayload));
  return true;
}

//Udp
void initMcUdp() {
  writelnComment("init UdP MC");
  if (udpMc.listenMulticast(multiCastIP, MC_PORT)) {
    // Callback registrieren: wird automatisch aufgerufen, sobald ein Paket eintrifft
    udpMc.onPacket([](AsyncUDPPacket packet) {
      xMsg m;
      if (!parseXMsg(packet, m)) return; // falsche Version oder Länge -> ignorieren
      if (m.header.msgCode == HB) {
        hbPayload hb;
        if (getHbPayload(m, hb)) device[m.header.sender].IP = hb.ip;
      }
      lastMcMsg = m;
      // Flag setzen, damit der Sketch das abrufen kann
      mcDataReceived = true;
    });
  } else {
    // Falls listenMulticast fehlgeschlagen ist, kann man hier Debug machen
    if (DEBUG) Serial << "AsyncUDP: Konnte Multicast nicht anfangen zu lauschen" << endl;
  }
}
//-------------------------------------------------------------------------------------------
void initText2Udp() {
  writelnComment("init UdP Text");

  if (udpText.listenMulticast(multiCastIP, MC_Text_PORT)) {
    udpText.onPacket([](AsyncUDPPacket packet) {
      udpTextMsg = String((char*)packet.data(), packet.length());
      udpTextReceived = true;
    });
  } else {
    if (DEBUG) Serial << "Udp Text failed" << endl;
  }
}
//------------------------------------------------------------------------------------------------
void initUnicast() {
  writelnComment("init UdP UC");
  if (udpUc.listen(UC_PORT)) {
    // Callback: wird aufgerufen, wenn ein UDP-Paket (jeder Absender) auf Port UC_PORT eintrifft
    udpUc.onPacket([](AsyncUDPPacket packet) {
      xMsg m;
      if (!parseXMsg(packet, m)) return;
      lastUcMsg = m;
      lastUcSenderOctet = packet.remoteIP()[3];  // fuer gezielte Antwort
      ucDataReceived = true;  // Flag setzen
    });
  } else {
    if (DEBUG) Serial.println("AsyncUDP: Konnte Unicast-Socket nicht öffnen!");
  }
}

//---------------------------------------------------------------------------------------
// Protokoll 6.3: Senden - Header wird hier automatisch gefüllt
//---------------------------------------------------------------------------------------
bool sendXMsg(AsyncUDP &sock, uint8_t msgCode, const void* payload, uint8_t len, const IPAddress &ip, uint16_t port) {
  if (len > maxPayloadLen) return false;
  uint8_t buf[sizeof(msgHeader) + maxPayloadLen];
  msgHeader h;
  h.version    = XCOM_VERSION;
  h.sender     = ID;
  h.msgCode    = msgCode;
  h.payloadLen = len;
  time_t t;
  time(&t);
  h.timeStamp  = t;
  memcpy(buf, &h, sizeof(msgHeader));
  if (len) memcpy(buf + sizeof(msgHeader), payload, len);
  sock.writeTo(buf, sizeof(msgHeader) + len, ip, port);
  return true;
}

// Broadcast: Multicast an alle Geräte
bool broadcastMsg(uint8_t msgCode, const void* payload, uint8_t len) {
  return sendXMsg(udpMc, msgCode, payload, len, multiCastIP, MC_PORT);
}

// Unicast an ein einzelnes Gerät (letztes IP-Oktett, z.B. device[PA2i].IP)
bool unicastMsg(uint8_t msgCode, const void* payload, uint8_t len, uint8_t lastOctet) {
  if (lastOctet == 0) return false;
  IPAddress destIP(192, 168, 0, lastOctet);
  return sendXMsg(udpUc, msgCode, payload, len, destIP, UC_PORT);
}

// Komfort: Payload-Struct direkt übergeben
template <typename T>
bool broadcastMsg(uint8_t msgCode, const T &payload) {
  return broadcastMsg(msgCode, &payload, sizeof(T));
}

template <typename T>
bool unicastMsg(uint8_t msgCode, const T &payload, uint8_t lastOctet) {
  return unicastMsg(msgCode, &payload, sizeof(T), lastOctet);
}

// Nachricht ohne Payload
bool broadcastMsg(uint8_t msgCode) {
  return broadcastMsg(msgCode, nullptr, 0);
}

// Empfangene/gespeicherte Nachricht unverändert weitersenden, Header bleibt erhalten
// (z.B. Simulator-Playback mit originalem sender/timeStamp)
void broadcastRawMsg(const xMsg &m) {
  uint8_t buf[sizeof(msgHeader) + maxPayloadLen];
  memcpy(buf, &m.header, sizeof(msgHeader));
  if (m.header.payloadLen) memcpy(buf + sizeof(msgHeader), m.payload, m.header.payloadLen);
  udpMc.writeTo(buf, sizeof(msgHeader) + m.header.payloadLen, multiCastIP, MC_PORT);
}

void setUpOTA() {
  writelnComment("set up OTA");
  ArduinoOTA.setHostname(device[ID].Name.c_str());
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
         type = "filesystem";
      Serial.println("Start updating " + type);
      #ifdef containLed
        setPixel(minPix,0x0000FF);
        setPixel(maxPix,0x0000FF);
      #endif
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();
}

void printTimePreamble(const xMsg &m) {
  time_t t= m.header.timeStamp;
  struct tm *tmstruct = localtime(&t);
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmstruct);
  Serial << "Time: " << buf << ", ";
}

void printSensorData(const xMsg &m) {
  printTimePreamble(m);
  if (m.header.msgCode == HB) {
    Serial << " HB, Sender: " << m.header.sender;
    hbPayload hb;
    if (getHbPayload(m, hb)) Serial << " IP: " << hb.ip << " Periode: " << hb.HBperiode;
    pa2HbPayload pa2;
    if (getPayload(m, pa2)) {
      Serial << " readyToFire: " << pa2.readyToFire;
      Serial << " limitsActive: " << pa2.limitsActive;
      Serial << " leftLimit: " << pa2.leftLimit;
      Serial << " rightLimit: " << pa2.rightLimit;
      Serial << " farLimit: " << pa2.farLimit;
      Serial << " nearLimit: " << pa2.nearLimit;
    }
    radarHbPayload rhb;
    if (getPayload(m, rhb)) Serial << " deadZone: " << rhb.deadZoneDist;
    markerHbPayload mhb;
    if (getPayload(m, mhb)) {
      Serial << " mainLaser: " << mhb.mainLaser;
      Serial << " subLaser: " << mhb.subLaser;
      Serial << " aux: " << mhb.aux;
      Serial << " RGB: " << mhb.r << "," << mhb.g << "," << mhb.b;
    }
    Serial << endl;
  }
  else if ((m.header.msgCode == catObserved) || (m.header.msgCode == measurement) || (m.header.msgCode == catHit)) {
    posPayload pos;
    if (!getPayload(m, pos)) return;
    if (m.header.msgCode == catObserved) Serial << "Observation, ";
      else if (m.header.msgCode == measurement) Serial << "Measurement, ";
        else Serial << "Hit, ";
    Serial << "Sender: " << m.header.sender << ", Sensor: " << pos.sensor << ", ";
    Serial << "Radius: " << pos.radius << ", phi: " << pos.angle << ", x: " << pos.x << ", y: " << pos.y;
    Serial << ", speed: " << pos.targetSpeed << ", res: " << pos.res;
    Serial << ", worldValid: " << pos.worldValid << ", worldX: " << pos.worldX << ", worldY: " << pos.worldY;
    Serial << endl;
  }
  else if (m.header.msgCode == poseReport) {
    worldPosePayload wp;
    if (!getPayload(m, wp)) return;
    Serial << "PoseReport, Sender: " << m.header.sender;
    Serial << ", validWorldPose: " << wp.validWorldPose;
    Serial << ", worldX: " << wp.worldX << ", worldY: " << wp.worldY << ", heading: " << wp.heading;
    Serial << endl;
  }
  else if (m.header.msgCode == poseRequest) {
    Serial << "PoseRequest, Sender: " << m.header.sender << endl;
  }
}

void printCmdData(const xMsg &m) {
  Serial << "Sender: " << m.header.sender;
  cmdPayload c;
  if (getPayload(m, c)) Serial << " CMD: " << c.cmd << " Info: " << c.info;
  Serial << endl;
}

#ifdef containLed
  void initPixel(){
    FastLED.addLeds<LED_TYPE, pixelPin, COLOR_ORDER>(leds, pixelNum);
    FastLED.setBrightness(BRIGHTNESS);
  }

  void setPixel(byte led, uint32_t farbe) {
    leds[led] = farbe;
    FastLED.show();
  }

  void allPixel(uint32_t farbe) {
    for(int i= 0; i < pixelNum; i++) {
      leds[i] = farbe;
      //setPixel(i,farbe);
    }
    FastLED.show();
  }
#endif

void toPol(int x, int y, float &phi,float &radius) {
  radius = sqrt(x*x+y*y);
  phi = atan2(x, y) *180 / M_PI;
}

void toKart(int &x, int &y, float phi, float radius) {
  phi = phi*M_PI /180;
  x = sin(phi)* radius;
  y = cos(phi)* radius;
}

void toPaPol(int x, int y, float &phi,float &radius){
  radius = sqrt(x*x+y*y);
  phi = (atan2(x, y) * 2048 / M_PI) + 2048;
}

void toPaKart(int &x, int &y, float phi, float radius) {
  phi = (phi-2048)*M_PI /2048;
  x = sin(phi)* radius;
  y = cos(phi)* radius;
}

//---------------------------------------------------------------------------------------
// Welt-/Lokal-Transformation (reine 2D-Starrkoerper-Transformation, mm)
// heading in PA-Einheiten (0..4096 = 360 Grad), 0 = lokale Achsen welt-ausgerichtet.
//   Welt = Rot(heading) * lokal + (worldX, worldY)
//---------------------------------------------------------------------------------------
void localToWorld(int xl, int yl, const worldPose &p, int &xw, int &yw) {
  float a = p.heading * (2.0f * M_PI / 4096.0f);
  float c = cos(a), s = sin(a);
  xw = p.worldX + lround(xl * c - yl * s);
  yw = p.worldY + lround(xl * s + yl * c);
}

void worldToLocal(int xw, int yw, const worldPose &p, int &xl, int &yl) {
  float a = p.heading * (2.0f * M_PI / 4096.0f);
  float c = cos(a), s = sin(a);
  float dx = xw - p.worldX;
  float dy = yw - p.worldY;
  xl = lround( dx * c + dy * s);
  yl = lround(-dx * s + dy * c);
}

//---------------------------------------------------------------------------------------
// Pose-Persistenz im nichtfluechtigen Speicher (NVS, Namespace "pose").
// loadPose laedt nur die Koordinaten/Ausrichtung; validWorldPose bleibt nach dem
// Booten bewusst false und wird erst durch einen spaeteren Quickcheck bestaetigt.
//---------------------------------------------------------------------------------------
void savePose(const worldPose &p) {
  Preferences posePrefs;
  posePrefs.begin("pose", false);
  posePrefs.putInt("x", p.worldX);
  posePrefs.putInt("y", p.worldY);
  posePrefs.putFloat("h", p.heading);
  posePrefs.putBool("v", p.validWorldPose);
  posePrefs.end();
}

bool loadPose(worldPose &p) {
  Preferences posePrefs;
  posePrefs.begin("pose", true);
  bool exists = posePrefs.isKey("x");
  if (exists) {
    p.worldX  = posePrefs.getInt("x", 0);
    p.worldY  = posePrefs.getInt("y", 0);
    p.heading = posePrefs.getFloat("h", 0.0f);
  }
  posePrefs.end();
  p.validWorldPose = false;   // nach Boot immer ungueltig -> Quickcheck bestaetigt spaeter
  return exists;
}

//---------------------------------------------------------------------------------------
// Karten-Verteilung (No-Shot u.a.): Manager haelt die Karte im LittleFS, Geraete laden
// sie gechunkt ueber das UDP-xCom-Protokoll (mapRequest -> mapInfo -> mapChunk...).
//---------------------------------------------------------------------------------------

// CRC32 (zlib-kompatibel, reflektiertes Polynom 0xEDB88320) - Manager und
// Empfaenger nutzen dieselbe Funktion.
static inline uint32_t crc32Update(uint32_t crc, uint8_t b) {
  crc ^= b;
  for (int k = 0; k < 8; k++)
    crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
  return crc;
}
uint32_t crc32Bytes(const uint8_t* d, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) crc = crc32Update(crc, d[i]);
  return crc ^ 0xFFFFFFFFu;
}

// Version aus dem CSV-Header lesen ("... vN ...") - Default 1, wenn nicht gefunden.
static uint16_t parseMapVersion(const String& firstLine) {
  int i = firstLine.indexOf(" v");
  if (i < 0) return 1;
  i += 2;
  uint16_t v = 0; bool any = false;
  while (i < (int)firstLine.length() && isDigit(firstLine[i])) {
    v = v * 10 + (firstLine[i] - '0'); any = true; i++;
  }
  return any ? v : 1;
}

// Karte im LittleFS analysieren: Version (Header), CRC32 (ganze Datei), Laenge.
bool mapFileInfo(const char* path, uint16_t& version, uint32_t& fileCrc, uint32_t& totalLen) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  totalLen = f.size();
  uint32_t crc = 0xFFFFFFFFu;
  String first; bool inFirst = true;
  while (f.available()) {
    uint8_t b = (uint8_t)f.read();
    crc = crc32Update(crc, b);
    if (inFirst) {
      if (b == '\n') inFirst = false;
      else if (b != '\r') first += (char)b;
    }
  }
  f.close();
  fileCrc = crc ^ 0xFFFFFFFFu;
  version = parseMapVersion(first);
  return true;
}

// (Manager) Karte gechunkt an einen Anforderer senden: mapInfo + alle mapChunks (Unicast).
bool serveMap(uint8_t mapType, const char* path, uint8_t reqOctet) {
  if (reqOctet == 0) return false;
  uint16_t version; uint32_t fileCrc, totalLen;
  if (!mapFileInfo(path, version, fileCrc, totalLen)) return false;

  uint16_t chunkCount = (uint16_t)((totalLen + mapChunkBytes - 1) / mapChunkBytes);
  if (totalLen == 0) chunkCount = 0;

  mapInfoPayload info;
  info.mapType = mapType; info.version = version; info.fileCrc = fileCrc;
  info.totalLen = totalLen; info.chunkSize = mapChunkBytes; info.chunkCount = chunkCount;
  unicastMsg(mapInfo, info, reqOctet);

  File f = LittleFS.open(path, "r");
  if (!f) return false;
  uint8_t buf[sizeof(mapChunkMeta) + mapChunkBytes];
  uint16_t idx = 0;
  while (true) {
    int n = f.read(buf + sizeof(mapChunkMeta), mapChunkBytes);
    if (n <= 0) break;
    mapChunkMeta meta;
    meta.mapType = mapType; meta.version = version;
    meta.chunkIndex = idx; meta.dataLen = (uint16_t)n;
    memcpy(buf, &meta, sizeof(meta));
    unicastMsg(mapChunk, buf, (uint8_t)(sizeof(meta) + n), reqOctet);
    idx++;
    delay(4);   // dem Empfaenger Zeit lassen (UDP, kein Flow-Control)
  }
  f.close();
  return true;
}

// (Sensor) Karte beim Manager anfordern.
bool requestMap(uint8_t mapType, uint8_t managerOctet) {
  mapReqPayload r; r.mapType = mapType;
  return unicastMsg(mapRequest, r, managerOctet);
}

// Empfangszustand fuer eine eingehende Karte (Sensor-Seite).
struct mapRxState {
  bool     active = false;
  bool     done = false;
  bool     ok = false;
  uint8_t  mapType = 0;
  uint16_t version = 0;
  uint32_t fileCrc = 0;
  uint32_t totalLen = 0;
  uint16_t chunkCount = 0;
  uint16_t nextIndex = 0;
  File     f;
  String   path;
};

// Empfang nach erhaltenem mapInfo starten (schreibt in <path>.tmp).
bool mapBeginRx(mapRxState& s, const mapInfoPayload& info, const char* path) {
  s = mapRxState();
  s.mapType = info.mapType; s.version = info.version; s.fileCrc = info.fileCrc;
  s.totalLen = info.totalLen; s.chunkCount = info.chunkCount;
  s.path = path;
  s.f = LittleFS.open((s.path + ".tmp").c_str(), "w");
  if (!s.f) return false;
  s.active = true;
  if (s.chunkCount == 0) {                 // leere Karte: sofort fertig
    s.f.close();
    LittleFS.remove(s.path.c_str());
    LittleFS.rename((s.path + ".tmp").c_str(), s.path.c_str());
    s.active = false; s.done = true; s.ok = (s.totalLen == 0);
  }
  return true;
}

// Einen mapChunk verarbeiten. Rueckgabe: 0 = weiter, 1 = fertig+OK, -1 = Fehler.
int mapFeedChunk(mapRxState& s, const xMsg& m) {
  if (!s.active) return -1;
  if (m.header.payloadLen < sizeof(mapChunkMeta)) return -1;
  mapChunkMeta meta; memcpy(&meta, m.payload, sizeof(meta));
  if (meta.mapType != s.mapType || meta.version != s.version) return 0;  // fremder Chunk
  if (meta.chunkIndex != s.nextIndex) return 0;                          // nicht in Reihenfolge
  if ((size_t)sizeof(mapChunkMeta) + meta.dataLen > m.header.payloadLen) return -1;

  s.f.write(m.payload + sizeof(mapChunkMeta), meta.dataLen);
  s.nextIndex++;
  if (s.nextIndex >= s.chunkCount) {
    s.f.close();
    String tmp = s.path + ".tmp";
    uint16_t v; uint32_t crc, len;
    bool good = mapFileInfo(tmp.c_str(), v, crc, len) && crc == s.fileCrc && len == s.totalLen;
    if (good) {
      LittleFS.remove(s.path.c_str());
      LittleFS.rename(tmp.c_str(), s.path.c_str());
      s.active = false; s.done = true; s.ok = true;
      return 1;
    }
    LittleFS.remove(tmp.c_str());
    s.active = false; s.done = true; s.ok = false;
    return -1;
  }
  return 0;
}
