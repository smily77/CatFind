void initUdp(){
  udp.beginMulticast(multiCastIP,PORT);
}

void sendMcData(comMsgStruct x) {
  udp.beginPacket(multiCastIP,PORT);
  udp.write((uint8_t*)&x, sizeof(x));
  udp.endPacket();
}

boolean receivedMcData(comMsgStruct &x) {
  int packetSize = udp.parsePacket();
  boolean dataReceived = false;
  if (packetSize) {
    udp.read((uint8_t*)&x, sizeof(x));
    dataReceived = true;
  }
  return dataReceived;
}

void printTimePreamble(comMsgStruct inData) {
  time_t t= inData.timeStamp;
  struct tm *tmstruct = localtime(&t);
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmstruct);
  Serial << "Time: " << buf << ", ";
}

void printSensorData(comMsgStruct inData) {
  printTimePreamble(inData);
  Serial << "Sender: " << inData.sender << ", Sensor: " << inData.sensor << ", ";
  Serial << "Radius: " << inData.radius << ", phi: " << inData.angle << ", x: " << inData.x << ", y: " << inData.y;
  //if ((inData.sender == glasBoxCode) || (inData.sender == radarDome2Code)) Serial << ", speed: " << inData.targetSpeed << ", res: " << inData.res;
  Serial << endl;
}
