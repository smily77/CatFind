void writelnComment(String comment) {
  if (DEBUG) Serial << comment  <<  endl;
  //tft << comment << endl;
  //gfx -> println(comment);
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
  //tft << comment;
  //gfx -> print(comment);
}

void  heardBeat() {
  if ((HBtimer + periodeForHB) < millis()) {
    sendHBmsg();
    HBtimer = millis();
  }
}

void sendHBmsg(){
  time(&now);
  msgHBStorage.timeStamp = now;
  sendMcData(msgHBStorage); 
}

void assembleHBmsg(){
  msgHBStorage.dataHB.pa2HB.readyToFire = false;
  
  msgHBStorage.sender = ID;
  msgHBStorage.msgCode = HB;
  msgHBStorage.dataHB.ip = getLastIpByte();
  msgHBStorage.dataHB.HBperiode = periodeForHB;
  loadHBFromVault();
}

void loadHBFromVault() {
  vault.begin("settings",true);
  msgHBStorage.dataHB.pa2HB.limitsActive = vault.getBool("limitsActive",false);
  msgHBStorage.dataHB.pa2HB.leftLimit = vault.getFloat("leftLimit",2048 - systemAngle);
  msgHBStorage.dataHB.pa2HB.rightLimit = vault.getFloat("rightLimit",2048 + systemAngle);
  msgHBStorage.dataHB.pa2HB.farLimit = vault.getFloat("farLimit",maxDist);
  msgHBStorage.dataHB.pa2HB.nearLimit = vault.getFloat("nearLimit",initDeadZone);
  vault.end();
}

void saveHBToVault() {
  vault.begin("settings",false);
  vault.putBool("limitsActive",msgHBStorage.dataHB.pa2HB.limitsActive);
  vault.putFloat("leftLimit",msgHBStorage.dataHB.pa2HB.leftLimit);
  vault.putFloat("rightLimit",msgHBStorage.dataHB.pa2HB.rightLimit);
  vault.putFloat("farLimit",msgHBStorage.dataHB.pa2HB.farLimit);
  vault.putFloat("nearLimit",msgHBStorage.dataHB.pa2HB.nearLimit);
  vault.end();
}
  
void switchLaser() {
  laserOn=!laserOn;
  digitalWrite(laserPwr,laserOn);
}

void scanWithLidar(){
  int erfassungsWinkel = leftStopp;
  for (int i = 1365; i<2732;i++) {
      servoGoTo(i);
      comTXMsg.msgCode = measurement;
      comTXMsg.radius = laserDist()/2;
      comTXMsg.angle = servoPos;  
      toPaKart(comTXMsg.x,comTXMsg.y,comTXMsg.angle,comTXMsg.radius);
      sendMcData(comTXMsg);    
  }
}

float laserDist() {
  LP40B_MeasurementData messung = lidar.getSingleMeasurement();
  return float(messung.distance_mm);
}

void servoGoTo(int x) {
  if (x >= rightStopp) x = rightStopp;
  if (x <= leftStopp)  x= leftStopp;
  st.WritePosEx(1, x, 60000, 255); 
//   servoBlockAsLongItMoves();
  servoPos = x;
}

void initHw() {
  Serial.begin(115200);
  pinMode(valve, OUTPUT);
  pinMode(LD06Pwr, OUTPUT);
  digitalWrite(LD06Pwr,LOW);
  pinMode(laserPwr, OUTPUT);
  pinMode(hlkPwr, OUTPUT);
  pinMode(intLed, OUTPUT);

  Serial1.begin(1000000, SERIAL_8N1, S_RXD, S_TXD);
  st.pSerial = &Serial1;
}

void initLidar() {
  Serial2.begin(115200, SERIAL_8N1, S2_RX, S2_TX);
  if (lidar.begin()) Serial << "Lidar started"  << endl;
  lidar.setDataFormat(LP40B_FORMAT_BYTE);
  lidar.setMeasurementMode(LP40B_MODE_SINGLE);
  // Get device information
  LP40B_DeviceInfo info;
  if (lidar.getDeviceInfo(info)) {
      Serial.println("Device Information:");
      Serial.print("  Model: 0x");
      Serial.println(info.model, HEX);
      Serial.print("  Firmware: 0x");
      Serial.println(info.firmwareVersion, HEX);
      Serial.print("  Data Format: ");
      Serial.println(info.dataFormat == LP40B_FORMAT_BYTE ? "Byte" : "Pixhawk");
      Serial.print("  Measurement Mode: ");
      switch(info.measurementMode) {
        case LP40B_MODE_CONTINUOUS_STARTUP: Serial.println("Continuous-Startup"); break;
        case LP40B_MODE_SINGLE: Serial.println("Single"); break;
        case LP40B_MODE_CONTINUOUS_MANUAL: Serial.println("Continuous-Manual"); break;
        case LP40B_MODE_BURST: Serial.println("Burst"); break;
        default: Serial.println("Unknown");
      }
      Serial.print("  Frequency: ");
      Serial.print(info.frequency);
      Serial.println(" Hz");
  }
}

void servoBlockAsLongItMoves() {
unsigned long blTimer = millis();
  while(!st.ReadMove(1)) {
    if (millis()> (blTimer + 10000)) break;
  }
  while(st.ReadMove(1)) {
    if (millis()> (blTimer + 10000)) break;
  }
}

void initServo() {
  Serial1.begin(1000000, SERIAL_8N1, S_RXD, S_TXD);
  st.pSerial = &Serial1;
  delay(1000);
  st.WritePosEx(1, 2048, 500, 50); 
  servoBlockAsLongItMoves();
  digitalWrite(laserPwr,HIGH);
  delay(100);
  digitalWrite(laserPwr,LOW);
  servoPos = 2024;
}
