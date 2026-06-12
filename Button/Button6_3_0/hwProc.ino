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
#ifdef Atom3S
void undefFireState(){
  M5.Lcd.fillScreen(TFT_WHITE);
}

void fireReady() {
  M5.Lcd.fillScreen(TFT_RED);
}

void fireNotReady() {
  M5.Lcd.fillScreen(TFT_GREEN);
}

#else
void undefFireState(){
  allPixel(0xFFFFFF);
}

void fireReady() {
  allPixel(0xFF0000);
}

void fireNotReady() {
  allPixel(0x00FF00);
}
#endif
