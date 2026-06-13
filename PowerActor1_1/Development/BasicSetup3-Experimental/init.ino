void setUpWifi() {
  if (DEBUG) Serial.println("Start Wifi");
//  digitalWrite(gelb,HIGH);
  WiFi.mode(WIFI_AP_STA);
//  esp_wifi_set_mac(WIFI_IF_STA, ownMac);
  WiFi.begin(ssid, password);
  do {
    if (DEBUG) Serial.print(".");
    delay(500);
  } while (WiFi.status() != WL_CONNECTED);
  if (DEBUG) Serial.println();
//  digitalWrite(gelb,LOW);
/*  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  } */
}

void setUpOTA() {
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
         type = "filesystem";
      Serial.println("Start updating " + type);
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

void rotorInit() {
  for (uint8_t p=0; p<8; p++) {
    rotor.pinMode(p, OUTPUT);
  }
  
  rotor.digitalWrite(EN, LOW);
 // rotor.digitalWrite(SLP, LOW);
  rotor.digitalWrite(SLP, HIGH);
  rotor.digitalWrite(RST, LOW);
  delay(100);
  rotor.digitalWrite(RST, HIGH);

  rotor.digitalWrite(STEP, LOW);
  rotor.digitalWrite(DIR, LOW);
}

void initPaControls() {
  pinMode(water,OUTPUT);
  digitalWrite(water,LOW);
  pinMode(extPower,OUTPUT);
  digitalWrite(extPower,LOW);
  pinMode(wirelessPower,OUTPUT);
  digitalWrite(wirelessPower,LOW);
  pinMode(taster,INPUT_PULLUP);
  pinMode(reedLeft,INPUT_PULLUP);
  pinMode(reedRight,INPUT_PULLUP);
  pinMode(laser,OUTPUT);
}

void initPixels() {
  FastLED.addLeds<WS2812, pixelPin, RGB>(leds, pixelCount);  // GRB ordering is typical
  FastLED.setBrightness(helligkeit);
}

void initWinkelMesser(){
  winkel.setDirection();
  winkel.resetCumulativePosition();
  if (false) {
    Serial << "magnet too strong:  ";
    if (winkel.magnetTooStrong()) Serial << "yes"  << endl;
      else Serial << "no" << endl;
    Serial << "magnet too weak:  ";
    if (winkel.magnetTooWeak()) Serial << "yes"  << endl;
      else Serial << "no" << endl;
  } 
}

void rotorZeroPos() {
  int leftStopp, initialPos;
    while (!leftReed()) {
//    rotorStep(left,fullStep);
    rotorStep(left,sixteenthStep);
  }
  for(int i=0; i < 10;i++) {
    rotorStep(left,sixteenthStep);
  }
  while (leftReed()) {
    rotorStep(left,sixteenthStep);
  }
  leftStopp = globalPosition;
  for(int i=0; i < 10;i++) {
    rotorStep(right,sixteenthStep);
  }
  while (leftReed()) {
    rotorStep(right,sixteenthStep);
  }
  initialPos = globalPosition + (leftStopp - globalPosition)/2;
  while (globalPosition > initialPos) {
    rotorStep(left,sixteenthStep);
  }
  globalPosition = 0;
}
