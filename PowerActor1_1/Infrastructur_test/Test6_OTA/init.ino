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
