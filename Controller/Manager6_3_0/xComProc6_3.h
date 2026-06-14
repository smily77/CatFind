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
    Serial << endl;
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
