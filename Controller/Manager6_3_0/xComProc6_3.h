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
//bool loadNoShot(const char* path)                             - No-Shot-Polygon(e) aus LittleFS-CSV in RAM laden
//bool insideNoShot(int32_t xw, int32_t yw)                     - Welt-Punkt im schiessbaren Bereich? (Point-in-Polygon)
//bool acquireNoShot(const char* path, uint8_t mgrOctet, unsigned long waitMs)  - No-Shot-Karte vom Manager beziehen/cachen
//bool vpsLocalize(const uint16_t* scan,int n, ... )            - Scan an den VPS, Pose zurueck (nur mit USE_VPS_LOCALIZE)
//void resolvePose(vpsOk,vx,vy,vh,vmir,conf, hadNVS,plausMm,plausDeg)  - VPS-Pose vs. NVS-Pose -> myPose setzen+speichern
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
    Serial << ", mirror: " << wp.mirror;
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
// Welt = Translation + Rotation(heading) angewandt auf das (ggf. ueber mirror an der
// lokalen y-Achse gespiegelte) lokale System. mirror = +1 (normal) oder -1 (gespiegelt).
void localToWorld(int xl, int yl, const worldPose &p, int &xw, int &yw) {
  float a = p.heading * (2.0f * M_PI / 4096.0f);
  float c = cos(a), s = sin(a);
  float ym = (float)p.mirror * (float)yl;          // Drehsinn beruecksichtigen
  xw = p.worldX + lround(xl * c - ym * s);
  yw = p.worldY + lround(xl * s + ym * c);
}

void worldToLocal(int xw, int yw, const worldPose &p, int &xl, int &yl) {
  float a = p.heading * (2.0f * M_PI / 4096.0f);
  float c = cos(a), s = sin(a);
  float dx = xw - p.worldX;
  float dy = yw - p.worldY;
  xl = lround(  dx * c + dy * s);
  yl = lround(((float)p.mirror) * (-dx * s + dy * c));   // mirror ist sein eigenes Inverses
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
  posePrefs.putInt("m", p.mirror);
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
    p.mirror  = (int8_t)posePrefs.getInt("m", 1);
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
  delay(20);                                  // dem Empfaenger Zeit, mapInfo vor den Chunks zu verarbeiten

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

//---------------------------------------------------------------------------------------
// No-Shot-/Schusszonen-Karte: Polygon(e) in Welt-mm (CSV im LittleFS) in RAM laden und
// einen Welt-Punkt testen. Innerhalb = schiessbar. Mehrere Ringe (durch Leerzeile
// getrennt) -> even-odd-Test ueber alle Ringe (Loecher = No-Shot-Inseln).
//---------------------------------------------------------------------------------------
#ifndef MAX_NOSHOT_PTS
#define MAX_NOSHOT_PTS 300
#endif
#ifndef MAX_NOSHOT_RINGS
#define MAX_NOSHOT_RINGS 8
#endif
static int32_t  nsX[MAX_NOSHOT_PTS], nsY[MAX_NOSHOT_PTS];
static uint16_t nsRingStart[MAX_NOSHOT_RINGS + 1];
static uint8_t  nsRingCount = 0;
static uint16_t nsPtCount   = 0;
static int32_t  nsMinX, nsMaxX, nsMinY, nsMaxY;
static bool     noShotLoaded = false;

bool loadNoShot(const char* path) {
  noShotLoaded = false; nsPtCount = 0; nsRingCount = 0;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  nsRingStart[0] = 0;
  bool ringHasPts = false;
  nsMinX = nsMinY = 0x7FFFFFFF; nsMaxX = nsMaxY = -0x7FFFFFFF;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {                       // Leerzeile -> Ring abschliessen
      if (ringHasPts && nsRingCount < MAX_NOSHOT_RINGS) {
        nsRingCount++; nsRingStart[nsRingCount] = nsPtCount; ringHasPts = false;
      }
      continue;
    }
    if (line[0] == '#') continue;                   // Kommentar
    int comma = line.indexOf(',');
    if (comma < 0) continue;
    int32_t x = line.substring(0, comma).toInt();
    int32_t y = line.substring(comma + 1).toInt();
    if (nsPtCount < MAX_NOSHOT_PTS) {
      nsX[nsPtCount] = x; nsY[nsPtCount] = y; nsPtCount++; ringHasPts = true;
      if (x < nsMinX) nsMinX = x; if (x > nsMaxX) nsMaxX = x;
      if (y < nsMinY) nsMinY = y; if (y > nsMaxY) nsMaxY = y;
    }
  }
  f.close();
  if (ringHasPts && nsRingCount < MAX_NOSHOT_RINGS) {  // letzten (nicht leerzeilen-) Ring schliessen
    nsRingCount++; nsRingStart[nsRingCount] = nsPtCount;
  }
  noShotLoaded = (nsRingCount > 0 && nsPtCount >= 3);
  return noShotLoaded;
}

// true = Welt-Punkt liegt im schiessbaren Bereich. Ohne geladene Karte: true
// (fail-open: Detektion wird gemeldet; der Aktor prueft spaeter zusaetzlich selbst).
bool insideNoShot(int32_t px, int32_t py) {
  if (!noShotLoaded) return true;
  if (px < nsMinX || px > nsMaxX || py < nsMinY || py > nsMaxY) return false;
  bool inside = false;
  for (uint8_t r = 0; r < nsRingCount; r++) {
    uint16_t s = nsRingStart[r], e = nsRingStart[r + 1];
    if (e - s < 3) continue;
    int j = e - 1;
    for (int i = s; i < (int)e; i++) {
      const int32_t yi = nsY[i], yj = nsY[j];
      if ((yi > py) != (yj > py)) {
        const double xint = (double)(nsX[j] - nsX[i]) * (double)(py - yi) /
                            (double)(yj - yi) + (double)nsX[i];
        if ((double)px < xint) inside = !inside;
      }
      j = i;
    }
  }
  return inside;
}

//---------------------------------------------------------------------------------------
// No-Shot-Karte beim Manager beschaffen (generisch fuer jedes welt-faehige Geraet):
// lokale Kopie laden, beim Manager die aktuelle Version erfragen, bei Abweichung neu
// herunterladen; sonst / bei Manager-Ausfall die alte Karte verwenden. true = Karte nutzbar.
//---------------------------------------------------------------------------------------
static bool waitForUc(uint8_t codeWanted, xMsg& out, unsigned long budget) {
  unsigned long t0 = millis();
  while (millis() - t0 < budget) {
    ArduinoOTA.handle();
    if (ucDataReceived) {
      out = lastUcMsg; ucDataReceived = false;
      if (out.header.msgCode == codeWanted) return true;
    }
    delay(1);
  }
  return false;
}

bool acquireNoShot(const char* path, uint8_t managerOctet, unsigned long waitMs) {
  bool haveLocal = loadNoShot(path);
  uint16_t lv = 0; uint32_t lcrc = 0, llen = 0;
  bool haveLocalInfo = mapFileInfo(path, lv, lcrc, llen);

  if (managerOctet == 0 || !requestMap(mapNoShot, managerOctet)) {
    Serial << "No-Shot: Manager-Anfrage nicht moeglich -> lokale Karte." << endl;
    return haveLocal;
  }
  xMsg uc; mapInfoPayload info;
  if (!waitForUc(mapInfo, uc, waitMs) || !getPayload(uc, info) || info.mapType != mapNoShot) {
    Serial << "No-Shot: kein mapInfo -> lokale Karte." << endl;
    return haveLocal;
  }
  if (haveLocalInfo && info.version == lv && info.fileCrc == lcrc) {
    Serial << "No-Shot: lokale Karte aktuell (v" << lv << ")." << endl;
    return haveLocal;
  }
  Serial << "No-Shot: lade Karte vom Manager (v" << info.version << ", " << info.totalLen
         << " B, " << info.chunkCount << " Chunks) ..." << endl;
  mapRxState rx;
  if (!mapBeginRx(rx, info, path)) return haveLocal;
  int result = 0; unsigned long t0 = millis();
  while (rx.active && millis() - t0 < waitMs) {
    ArduinoOTA.handle();
    if (ucDataReceived) {
      xMsg c = lastUcMsg; ucDataReceived = false;
      if (c.header.msgCode == mapChunk) { result = mapFeedChunk(rx, c); t0 = millis(); if (result != 0) break; }
    }
    delay(1);
  }
  Serial << (result == 1 ? "No-Shot: Download OK." : "No-Shot: Download unvollstaendig -> alte Karte.") << endl;
  return loadNoShot(path);
}

//---------------------------------------------------------------------------------------
// Welt-Pose per VPS bestimmen. wrap360f/angDiff180 + resolvePose sind generisch; der
// eigentliche HTTP-Aufruf vpsLocalize zieht HTTPClient und wird nur kompiliert, wenn
// das Geraet vor dem Include USE_VPS_LOCALIZE definiert (sonst kein Code-Bloat).
//---------------------------------------------------------------------------------------
#ifndef VPS_LOC_PORT
#define VPS_LOC_PORT 8080
#endif
#ifndef VPS_LOC_PATH
#define VPS_LOC_PATH "/localize"
#endif
#ifndef VPS_LOC_TIMEOUT_MS
#define VPS_LOC_TIMEOUT_MS 20000
#endif

static float wrap360f(float d){ while (d < 0) d += 360.0f; while (d >= 360.0f) d -= 360.0f; return d; }
static float angDiff180(float a, float b){ float d = a - b; while (d > 180.0f) d -= 360.0f; while (d < -180.0f) d += 360.0f; return d; }

#ifdef USE_VPS_LOCALIZE
#include <HTTPClient.h>

static float jsonNum(const String& s, const char* key) {
  int i = s.indexOf(key); if (i < 0) return NAN;
  i = s.indexOf(':', i);  if (i < 0) return NAN;
  return s.substring(i + 1).toFloat();
}
static String jsonStr(const String& s, const char* key) {
  int i = s.indexOf(key); if (i < 0) return "";
  i = s.indexOf(':', i);  if (i < 0) return "";
  int a = s.indexOf('"', i); if (a < 0) return "";
  int b = s.indexOf('"', a + 1); if (b < 0) return "";
  return s.substring(a + 1, b);
}

// Scan (n Bins, mm; 0 = ungueltig) an den VPS posten, Pose zurueck. true bei Erfolg.
bool vpsLocalize(const uint16_t* scan, int n, int32_t& xmm, int32_t& ymm,
                 float& headDeg, int& mir, String& conf) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String body; body.reserve(n * 6 + 16);
  body = "{\"scan\":[";
  for (int i = 0; i < n; i++) { body += scan[i]; if (i < n - 1) body += ','; }
  body += "]}";
  HTTPClient http;
  String url = "http://" + ipVPS.toString() + ":" + String(VPS_LOC_PORT) + VPS_LOC_PATH;
  if (!http.begin(url)) return false;
  http.setTimeout(VPS_LOC_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  bool ok = false;
  if (code == 200) {
    String r = http.getString();
    float fx = jsonNum(r, "\"x_mm\""), fy = jsonNum(r, "\"y_mm\"");
    headDeg = jsonNum(r, "\"heading_deg\""); mir = (int)jsonNum(r, "\"mirror\"");
    conf = jsonStr(r, "\"confidence\"");
    if (!isnan(fx) && !isnan(fy) && !isnan(headDeg) && (mir == 1 || mir == -1)) {
      xmm = (int32_t)lroundf(fx); ymm = (int32_t)lroundf(fy); ok = true;
    }
  } else Serial << "VPS HTTP " << code << endl;
  http.end();
  return ok;
}
#endif  // USE_VPS_LOCALIZE

// Pose-Entscheidung (generisch): VPS-Ergebnis gegen die in myPose geladene NVS-Pose
// plausibilisieren -> passend: NVS behalten; sonst VPS-Pose uebernehmen + speichern.
// myPose muss vorher mit der NVS-Pose gefuellt sein (loadPose); setzt validWorldPose.
void resolvePose(bool vpsOk, int32_t vx, int32_t vy, float vh, int vmir, const String& conf,
                 bool hadNVS, float plausMm, float plausDeg) {
  if (vpsOk) {
    float nvsHeadDeg = myPose.heading * 360.0f / 4096.0f;
    float dx = (float)(myPose.worldX - vx), dy = (float)(myPose.worldY - vy);
    bool plausible = hadNVS && (sqrtf(dx * dx + dy * dy) < plausMm)
                   && (fabsf(angDiff180(nvsHeadDeg, vh)) < plausDeg) && (conf != "NIEDRIG");
    if (plausible) {
      myPose.validWorldPose = true;                 // NVS-Pose bestaetigt -> behalten
    } else if (conf == "HOCH" || conf == "MITTEL") {
      myPose.worldX = vx; myPose.worldY = vy;
      myPose.heading = wrap360f(vh) * 4096.0f / 360.0f; myPose.mirror = (int8_t)vmir;
      myPose.validWorldPose = true; savePose(myPose);
    } else {
      myPose.validWorldPose = false;
    }
  } else if (hadNVS) {
    myPose.validWorldPose = true;                   // VPS weg -> NVS-Pose (unbestaetigt) nutzen
  } else {
    myPose.validWorldPose = false;
  }
}
