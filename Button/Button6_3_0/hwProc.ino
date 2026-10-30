#ifdef Atom3S
void showColor(uint32_t c) {
  shownColor = c;
  M5.Lcd.fillScreen(M5.Lcd.color565(c >> 16, (c >> 8) & 0xFF, c & 0xFF));
}
#else
void showColor(uint32_t c) {
  shownColor = c;
  allPixel(c);
}
#endif
