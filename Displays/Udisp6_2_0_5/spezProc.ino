bool readEncoder(int &encoderValue,int counterStep) {
#if defined(CYD28)
  static int counter = counterInitialValue;
  static int lastCounter = counterInitialValue;
  
  lastCounter = counter;
  static uint8_t old_AB = 3;  // Lookup table index
  static int8_t encval = 0;   // Encoder value  
  static const int8_t enc_states[]  = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0}; // Lookup table
 
  old_AB <<=2;  // Remember previous state  
 
  if (digitalRead(ENC_A)) old_AB |= 0x02; // Add current state of pin A
  if (digitalRead(ENC_B)) old_AB |= 0x01; // Add current state of pin B
   
  encval += enc_states[( old_AB & 0x0f )];
 
  // Update counter if encoder has rotated a full indent, that is at least 4 steps
  if( encval > 3 ) {        // Four steps forward
    counter = counter + 1;              // Increase counter
    encval = 0;
  }
  else if( encval < -3 ) {  // Four steps backwards
   counter = counter - 1;                 // Decrease counter
   encval = 0;
  }
//  encoderValue = counter;
  if (lastCounter != counter) {
    if (counter > lastCounter) encoderValue = encoderValue + counterStep;
    if (counter < lastCounter) encoderValue = encoderValue - counterStep;
    return true;
  }
    else return false;
#elif defined(Sunton5) || defined(M5Core2)
  int counter;
  static int lastCounter = int(terminal.getEncoderValue(0));
  counter = int(terminal.getEncoderValue(0)/2)*2;
  if (lastCounter != counter) {
    if (counter > lastCounter) encoderValue = encoderValue + counterStep;
    if (counter < lastCounter) encoderValue = encoderValue - counterStep;
    lastCounter = counter;
    return true;
  }
    else return false;
#else 
static int counter;
  encoderValue = counter;
  return false;
#endif
}

bool checkSwitch() {
#if defined(Sunton5) || defined(M5Core2)
  if (!terminal.getButtonStatus(0)) {
    while (!terminal.getButtonStatus(0));
    return true;
  }
  else return false;
#elif defined(CYD28)
  if (!digitalRead(taste)) {
    while (!digitalRead(taste));
    return true;
  }
  else return false;

#else 
  return false;
#endif
}

void switchActions(){    // Wenn Button gedrückt wird
  switch (menuPage) {
    case mainMenue:
      menuPage = encoderPos[menuPage]+1;
      menueSelector();
    break;
    case leftLimitPage:
      layer[limitLayer].fillScreen(0);
      drawVectorLine(layer[limitLayer],PX(limitLayer,0),PY(limitLayer,0),encoderPos[menuPage],3);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      ucDataToSend.sender = ID;
      ucDataToSend.cmd = setLeftLimit;
      ucDataToSend.info = encoderPos[menuPage];
      sendUcData(ucDataToSend, device[targetSystem].IP);
      pushScreens(dispAngel,dispShift,true);
    break;
    case rightLimitPage:
      layer[limitLayer].fillScreen(0);
      drawVectorLine(layer[limitLayer],PX(limitLayer,0),PY(limitLayer,0),encoderPos[menuPage],3);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      ucDataToSend.sender = ID;
      ucDataToSend.cmd = setRightLimit;
      ucDataToSend.info = encoderPos[menuPage];
      sendUcData(ucDataToSend, device[targetSystem].IP);
      pushScreens(dispAngel,dispShift,true);
    break;
    case farLimitPage:
      layer[limitLayer].fillScreen(0);
      layer[limitLayer].drawArc(PX(limitLayer,0),PY(limitLayer,0), int(encoderPos[menuPage]/xScale)-1, int(encoderPos[menuPage]/xScale),-180,180,3);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      ucDataToSend.sender = ID;
      ucDataToSend.cmd = setFarLimit;
      ucDataToSend.info = encoderPos[menuPage];
      sendUcData(ucDataToSend, device[targetSystem].IP);
      pushScreens(dispAngel,dispShift,true);
    break;
    case nearLimitPage:
      layer[limitLayer].fillScreen(0);
      layer[limitLayer].drawArc(PX(limitLayer,0),PY(limitLayer,0), int(encoderPos[menuPage]/xScale)-1, int(encoderPos[menuPage]/xScale),-180,180,3);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      ucDataToSend.sender = ID;
      ucDataToSend.cmd = setNearLimit;
      ucDataToSend.info = encoderPos[menuPage];
      sendUcData(ucDataToSend, device[targetSystem].IP);
      pushScreens(dispAngel,dispShift,true);
    break;
    case displayShiftPage:
      layer[limitLayer].fillScreen(0);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      pushScreens(dispAngel,dispShift,true);
    break;
    case displayAnglePage:
      layer[limitLayer].fillScreen(0);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      pushScreens(dispAngel,dispShift,true);
    break;
    default:
      layer[limitLayer].fillScreen(0);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      pushScreens(dispAngel,dispShift,true);
    break; 
  }// case

}
void checkAction() {
  if (readEncoder(encoderPos[menuPage],encoderStep[menuPage])) menueSelector();
}

void menueSelector() {
  switch (menuPage) {
    case mainMenue:
      if (encoderPos[menuPage] < 0) encoderPos[menuPage] = pages-1;
      if (encoderPos[menuPage] >= pages) encoderPos[menuPage] = 0;
      drawMenue(encoderPos[menuPage]);
    break;
    case leftLimitPage:
      layer[limitLayer].fillScreen(0);
      drawVectorLine(layer[limitLayer],PX(limitLayer,0),PY(limitLayer,0),encoderPos[menuPage],1);
      pushScreens(dispAngel,dispShift,true);
    break;
    case rightLimitPage:
      layer[limitLayer].fillScreen(0);
      drawVectorLine(layer[limitLayer],PX(limitLayer,0),PY(limitLayer,0),encoderPos[menuPage],1);
      pushScreens(dispAngel,dispShift,true);
    break;
    case farLimitPage:
      layer[limitLayer].fillScreen(0);
      layer[limitLayer].drawArc(screenWidth,screenHight-1, int(encoderPos[menuPage]/xScale)-1,int(encoderPos[menuPage]/xScale),-180,180,1);
      pushScreens(dispAngel,dispShift,true);
    break;
    case nearLimitPage:
      layer[limitLayer].fillScreen(0);
      layer[limitLayer].drawArc(screenWidth,screenHight-1, int(encoderPos[menuPage]/xScale)-1,int(encoderPos[menuPage]/xScale),-180,180,1);
      pushScreens(dispAngel,dispShift,true);
    break;
    case displayShiftPage:
      layer[limitLayer].fillScreen(0);
      dispShift = encoderPos[menuPage];
      pushScreens(dispAngel,dispShift,true);
    break;
    case displayAnglePage:
      layer[limitLayer].fillScreen(0);
      dispAngel = encoderPos[menuPage];
      pushScreens(dispAngel,dispShift,true);
    break;
    case eraseLimitsPage:
      layer[limitLayer].fillScreen(0);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      ucDataToSend.sender = ID;
      ucDataToSend.cmd = changeLimitActivation;
      sendUcData(ucDataToSend, device[targetSystem].IP);
      pushScreens(dispAngel,dispShift,true);
    break;
    default:
      layer[limitLayer].fillScreen(0);
      if (stEnable && !hasPSRAM) gfx.fillScreen(SCHWARZ);
      actionsActive = false;
      pushScreens(dispAngel,dispShift,true);
    break; 
    
  } // switch
}
