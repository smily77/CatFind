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
  broadcastMsg(HB, hbState);  // Header (sender, timeStamp) füllt die Sendeprozedur
}

void assembleHBmsg(){
  hbState.readyToFire = false;
  hbState.hb.ip = getLastIpByte();
  hbState.hb.HBperiode = periodeForHB;
  loadHBFromVault();
}

void loadHBFromVault() {
  vault.begin("settings",true);
  hbState.limitsActive = vault.getBool("limitsActive",false);
  hbState.leftLimit = vault.getFloat("leftLimit",2048 - systemAngle);
  hbState.rightLimit = vault.getFloat("rightLimit",2048 + systemAngle);
  hbState.farLimit = vault.getFloat("farLimit",maxDist);
  hbState.nearLimit = vault.getFloat("nearLimit",initDeadZone);
  vault.end();
}

void saveHBToVault() {
  vault.begin("settings",false);
  vault.putBool("limitsActive",hbState.limitsActive);
  vault.putFloat("leftLimit",hbState.leftLimit);
  vault.putFloat("rightLimit",hbState.rightLimit);
  vault.putFloat("farLimit",hbState.farLimit);
  vault.putFloat("nearLimit",hbState.nearLimit);
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
      txPos.radius = laserDist()/2;
      txPos.angle = servoPos;
      int px, py;
      toPaKart(px,py,txPos.angle,txPos.radius);
      txPos.x = px;
      txPos.y = py;
      broadcastMsg(measurement, txPos);
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
