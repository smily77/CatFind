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
