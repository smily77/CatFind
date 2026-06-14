// LaserMarker6_3 - geraetespezifische Hilfsfunktionen

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}

void initHw() {
  Serial.begin(115200);
  pinMode(mainLaser, OUTPUT);
  pinMode(subLaser,  OUTPUT);
  pinMode(Aux,       OUTPUT);
  digitalWrite(mainLaser, LOW);
  digitalWrite(subLaser,  LOW);
  digitalWrite(Aux,       LOW);

  pixels.begin();
  pixels.clear();
  pixels.show();
}
