//boolean setUpWifi(int lastOctet)
//void initMcUdp()
//void initUnicast()
//void sendMcData(const mcDataStruct &x)
//void sendUcData(const ucDataStruct &msg, uint8_t lastOctet)
//void setUpTime()
//void setUpOTA()
//void initPixel()
//void setPixel(byte led, uint32_t farbe)
//void allPixel(uint32_t farbe)
//void printSensorData(mcDataStruct inData)
//void printTimePreamble(mcDataStruct inData)
//void toPol(int x, int y, float &phi,float &radius)
//void toKart(int &x, int &y, float phi, float radius)
//void toPaPol(int x, int y, float &phi,float &radius)
//void toPaKart(int &x, int &y, float phi, float radius)
//void sendHBmsg(mcDataStruct mcMsg)
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
//void    sendHBmsg(mcDataStruct mcMsg);

// Zeit & WiFi
void    setUpTime();
boolean setUpWifi(int lastOctet);

// UDP: Init & Text-MC
void    initMcUdp();
void    initText2Udp();
void    initUnicast();

// UDP: Senden
void    sendMcData(const mcDataStruct &x);
bool    sendUcData(const ucDataStruct &msg, uint8_t lastOctet);

// OTA
void    setUpOTA();

// Ausgabe / Debug
void    printTimePreamble(mcDataStruct inData);
void    printSensorData(mcDataStruct inData);
void    printUcData(ucDataStruct inData);

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
/*
void sendHBmsg(mcDataStruct mcMsg){
  mcMsg.sender = ID;
  mcMsg.msgCode = HB;
  mcMsg.dataHB.ip = getLastIpByte();
#ifdef periodeForHB
  mcMsg.dataHB.HBperiode = periodeForHB;
#endif
  sendMcData(mcMsg); 
}
*/
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

//Udp
void initMcUdp() {
  writelnComment("init UdP MC");
  if (udpMc.listenMulticast(multiCastIP, MC_PORT)) {
    // Callback registrieren: wird automatisch aufgerufen, sobald ein Paket eintrifft
    udpMc.onPacket([](AsyncUDPPacket packet) {
      // Prüfen, ob die Paketgröße exakt mit sizeof(comMsgStruct) übereinstimmt
      if (packet.length() == sizeof(mcDataStruct)) {
        // Rohdaten ins Struct kopieren
        memcpy((uint8_t*)&lastMcMsg, packet.data(), sizeof(mcDataStruct));
        // Flag setzen, damit receivedMcData() das abrufen kann
        if (lastMcMsg.msgCode == HB) {
          device[lastMcMsg.sender].IP = lastMcMsg.dataHB.ip;
        }
        mcDataReceived = true;
      }
      // Sonst Paket einfach ignorieren (oder optional Debug-Ausgabe)
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
      // Prüfen, ob die Paketgröße korrekt ist
      if (packet.length() == sizeof(ucDataStruct)) {
        memcpy((uint8_t*)&lastUcMsg, packet.data(), sizeof(ucDataStruct));
        ucDataReceived = true;  // Flag setzen
      }
      // Sonst Paket einfach ignorieren (oder optional Debug-Ausgabe)
    });         
  } else {
    if (DEBUG) Serial.println("AsyncUDP: Konnte Unicast-Socket nicht öffnen!");
  }
}

void sendMcData(const mcDataStruct &x) {
  udpMc.writeTo(reinterpret_cast<const uint8_t*>(&x),
              sizeof(mcDataStruct),
              multiCastIP,
              MC_PORT);
}

bool sendUcData(const ucDataStruct &msg, uint8_t lastOctet) {
  if (lastOctet == 0) return false;
  IPAddress destIP(192, 168, 0, lastOctet);

  udpUc.writeTo(
    reinterpret_cast<const uint8_t*>(&msg),
    sizeof(ucDataStruct),
    destIP,
    UC_PORT
  );
  return true;
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

void printTimePreamble(mcDataStruct inData) {
  time_t t= inData.timeStamp;
  struct tm *tmstruct = localtime(&t);
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmstruct);
  Serial << "Time: " << buf << ", ";
}

void printSensorData(mcDataStruct inData) {
  printTimePreamble(inData);
  if (inData.msgCode == HB) {
    Serial << " HB, Sender: " << inData.sender;
    Serial << " readyToFire: " << inData.dataHB.pa2HB.readyToFire; 
    Serial << " limitsActive: " << inData.dataHB.pa2HB.limitsActive; 
    Serial << " leftLimit: " << inData.dataHB.pa2HB.leftLimit; 
    Serial << " rightLimit: " << inData.dataHB.pa2HB.rightLimit; 
    Serial << " farLimit: " << inData.dataHB.pa2HB.farLimit; 
    Serial << " nearLimit: " << inData.dataHB.pa2HB.nearLimit << endl; 
  }
  else if (inData.msgCode == catObserved) {
    Serial << "Observation, ";
    Serial << "Sender: " << inData.sender << ", Sensor: " << inData.sensor << ", ";
    Serial << "Radius: " << inData.radius << ", phi: " << inData.angle << ", x: " << inData.x << ", y: " << inData.y;
    Serial << ", speed: " << inData.targetSpeed << ", res: " << inData.res;
    Serial << endl;
  }
}

void printUcData(ucDataStruct inData) {
  Serial << "Sender: " << inData.sender;
  Serial << " CMD: " << inData.cmd;
  Serial << " Info: " << inData.info << endl;

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
