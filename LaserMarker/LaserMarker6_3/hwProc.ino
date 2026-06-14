// LaserMarker6_3 - geraetespezifische Hilfsfunktionen

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}

void initHw() {
  Serial.begin(115200);
  pinMode(mainLaserPin, OUTPUT);
  pinMode(subLaserPin,  OUTPUT);
  pinMode(auxPin,       OUTPUT);
  digitalWrite(mainLaserPin, LOW);
  digitalWrite(subLaserPin,  LOW);
  digitalWrite(auxPin,       LOW);

  pixels.begin();
  pixels.clear();
  pixels.show();
}
