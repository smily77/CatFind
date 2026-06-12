void  heardBeat() {
  if ((timer + periodeForHB) < millis()) {
    comTXMsg.msgCode = HB;
    time(&now);
    comTXMsg.timeStamp = now;
    sendMcData(comTXMsg);
    statusLightOn = true;
    setPixel(minPix,0x00FF00);
    timer = millis();
  }
  if (statusLightOn && ((timer + statusLightDuration) < millis())) {
    statusLightOn = false;
    setPixel(minPix,0x000000);
    timer = millis();
  }
}

void readRadar(){
  radarBuffer[gi] = Serial2.read();
  gi++;
  if ((gi == 1) && (radarBuffer[0] != 0xAA)) gi=0;
  if ((gi == 2) && (radarBuffer[1] != 0xFF)) gi=0;
  if ((gi == 3) && (radarBuffer[2] != 0x03)) gi=0;
  if ((gi == 4) && (radarBuffer[3] != 0x00)) gi=0;
  if (gi >= 30) {
    gi = 0;
    calculateRadarPositions();
    newDataReady = true;
  }
}
void calculateRadarPositions(){
  for (int i=0; i < 3; i++) {
    target[i].x = (radarBuffer[i*8+5] << 8) | radarBuffer[i*8+4];
    if (radarBuffer[i*8+5] & 0x80)  target[i].x = -(target[i].x - 0x8000);
    target[i].y = (radarBuffer[i*8+7] << 8) | radarBuffer[i*8+6];
    if (radarBuffer[i*8+7] & 0x80)  target[i].y = -(target[i].y - 0x8000);
    
    target[i].y = -target[i].y;
    
    target[i].geschw = (int16_t)(radarBuffer[i*8+8] | (radarBuffer[i*8+9] << 8));
    if (radarBuffer[i*8+9] & 0x80)target[i].geschw += 0x8000;
      else target[i].geschw = -target[i].geschw;

    target[i].res = (uint16_t)(radarBuffer[i*8+10] | (radarBuffer[i*8+11] << 8));
    
    //target[i].geschw = (radarBuffer[i*8+9] << 8) | radarBuffer[i*8+8];
    //if (!(radarBuffer[i*8+9] & 0x80))  target[i].geschw = -(target[i].geschw - 0x8000);
    if ((target[i].x == 0) && (target[i].y == 0)) {
      target[i].active = false;
    }
    else {
      target[i].active = true;
      toPaPol(target[i].x,target[i].y,target[i].phi,target[i].l);
//      target[i].l = sqrt(target[i].x * target[i].x + target[i].y * target[i].y);
//      target[i].phi = atan2(target[i].y, target[i].x) * 180 / M_PI;
    }   
  }
}

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
