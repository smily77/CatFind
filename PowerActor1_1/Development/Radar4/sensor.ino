//BLE
void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    
  static byte radarBuffer[30];
  static int gi = 0; 
  boolean recordComplete = false; 
  for (int i = 0; i < length; i++) {
    radarBuffer[gi] = pData[i];
    gi++;
    if ((gi == 1) && (radarBuffer[0] != 0xAA)) gi=0;
    if ((gi == 2) && (radarBuffer[1] != 0xFF)) gi=0;
    if ((gi == 3) && (radarBuffer[2] != 0x03)) gi=0;
    if ((gi == 4) && (radarBuffer[3] != 0x00)) gi=0;
    if (gi >= 30) {
      gi = 0;
      recordComplete = true;
      break;
    }    
  }
  if ((recordComplete) && (radarBuffer[28] == 0x55) && (radarBuffer[29] == 0xCC)) {
    conversion(radarBuffer,BLEtarget);
//    for (int j=0; j< 29 ;j++) {
//      Serial.printf("%02X ", radarBuffer[j]);
//    }
//    Serial.println();
//    for (int k=0; k < 3;k++) {
//      Serial.print(BLEtarget[k].phi);
//      Serial.print(" ");
//    }
//    Serial.println();
    newBleData = true;
  } 
}
void conversion(byte* frame,targetData* target) {
  for (int i = 0; i < 3; i++) {
    target[i].x = (frame[i * 8 + 5] << 8) | frame[i * 8 + 4];
    if (frame[i * 8 + 5] & 0x80) target[i].x = -(target[i].x - 0x8000);
    target[i].y = (frame[i * 8 + 7] << 8) | frame[i * 8 + 6];
    if (frame[i * 8 + 7] & 0x80) target[i].y = -(target[i].y - 0x8000);
    target[i].geschw = (frame[i * 8 + 9] << 8) | frame[i * 8 + 8];
    if ((frame[i * 8 + 9] & 0x80) != 0) target[i].geschw = -(target[i].geschw - 0x8000);
            
    target[i].active = (target[i].x != 0 || target[i].y != 0);
    if (target[i].active) {
      target[i].l = sqrt(target[i].x * target[i].x + target[i].y * target[i].y);
      target[i].phi = atan2(target[i].y, target[i].x) * 180.0 / M_PI;
    } else {
      target[i].l = 0;
      target[i].phi = 0;
    }
  }
}
bool connectToSensor() {
  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
  }

  Serial.println("Versuche, eine Verbindung herzustellen...");
  if (pClient->connect(sensorAddress)) {
    Serial.println("Verbunden mit dem Sensor");

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService != nullptr) {
      pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
      if (pRemoteCharacteristic != nullptr && pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        connected = true;
        return true;
      }
    }
  } else {
    Serial.println("Verbindung zum Sensor fehlgeschlagen");
    connected = false;
  }
  return false;
}

void reConnect() {
  if (doConnect || !connected) {
    
  }
  if (doConnect || (waitToReconnect && (millis() >= reConnectTimer + 5000))) {
    doConnect = false;
    if (connectToSensor()) {
      outputDevice << "Wieder verbunden" << endl;
      waitToReconnect = false;
    } else {
      waitToReconnect = true;
      reConnectTimer = millis();
    }
  }
}
