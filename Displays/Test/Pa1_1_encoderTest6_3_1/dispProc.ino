// Pa1_1_encoderTest6_3_1 - Display-/Encoder-Framework (CYD28)
// abgeleitet aus Udisp6_3_0 (initDisp + readEncoder), reduziert auf den Test.

void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

void initDisp() {
  gfx.init();
  gfx.setRotation(3);
  gfx.setBrightness(255);
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextColor(TFT_YELLOW, TFT_BLACK);
  // Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(taste, INPUT_PULLUP);
}

// Quadratur-Encoder am CYD28 (1:1 aus Udisp6_3_0 uebernommen)
bool readEncoder(int &encoderValue, int counterStep) {
  static int counter = counterInitialValue;
  static int lastCounter = counterInitialValue;

  lastCounter = counter;
  static uint8_t old_AB = 3;
  static int8_t encval = 0;
  static const int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

  old_AB <<= 2;
  if (digitalRead(ENC_A)) old_AB |= 0x02;
  if (digitalRead(ENC_B)) old_AB |= 0x01;
  encval += enc_states[(old_AB & 0x0f)];

  if (encval > 3)       { counter++; encval = 0; }
  else if (encval < -3) { counter--; encval = 0; }

  if (lastCounter != counter) {
    if (counter > lastCounter) encoderValue += counterStep;
    if (counter < lastCounter) encoderValue -= counterStep;
    return true;
  }
  return false;
}

// Taste gedrueckt? (entprellt, aktiv LOW)
bool taastePressed() {
  if (!digitalRead(taste)) { while (!digitalRead(taste)); return true; }
  return false;
}
