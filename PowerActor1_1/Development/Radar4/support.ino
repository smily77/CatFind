void led(String input, int ledNr) {
  uint32_t outputColor = 0x000000;
  if (input.indexOf('R') != -1) outputColor += 0xFF0000; // Rot
  if (input.indexOf('G') != -1) outputColor += 0x00FF00; // Grün
  if (input.indexOf('B') != -1) outputColor += 0x0000FF; // Blau
  if (input.indexOf('Y') != -1) outputColor += 0xFFFF00; // Gelb (Rot + Grün)
  if (input.indexOf('C') != -1) outputColor += 0x00FFFF; // Cyan (Grün + Blau)
  if (input.indexOf('M') != -1) outputColor += 0xFF00FF; // Magenta (Rot + Blau)
  if (input.indexOf('W') != -1) outputColor = 0xFFFFFF; // Weiß (alle Farben)
  leds[ledNr] = outputColor;
  FastLED.show();
}
void allLedOff(){
  for(int i=0;i<pixelCount;i++) {
    led("",i);
  }
}
void oneTimeLedOffHelper() {
  if (!allLedOffAfter1000.isFirstIteration()) allLedOff();
}
void sendHB() {
  if (sendPeriodicHB.isFirstIteration()) return;
  led("G",1);
  broadcastHB();
  led("",1);
}
void broadcastCatAlarm(targetData target){
  broadcastMsg(catObserved, target);
}
void broadcastHB() {
  targetData dummy;
  broadcastMsg(HB, dummy);
}
void broadcastMsg(byte typ, targetData target) {
udpMsgStruct udpMsg;
  time_t now;
  time(&now);
  udpMsg.msgCode = typ;
  udpMsg.x = target.x;
  udpMsg.y = target.y;
  udpMsg.radius = target.l;
  udpMsg.angle = target.phi;
  udpMsg.targetSpeed = target.geschw;
  udpMsg.timeStamp = now;
  udpMsg.sender = broadcastId;
  udp.writeTo((uint8_t*)&udpMsg, sizeof(udpMsg), multiCastIP, multiCastPort);
}
void handleTelnet() {
  if (TelnetServer.hasClient()) {
    if (!Telnet || !Telnet.connected()) {
      if (Telnet) Telnet.stop();
      Telnet = TelnetServer.available();
    } else {
      TelnetServer.available().stop();
    }
  }
}
