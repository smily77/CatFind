// Basis-Prozeduren der Cat Cam: HB (der XIAO ESP32-C3 hat keine User-LED) und
// die von xComProc erwarteten Kommentar-Ausgaben.

void heardBeat() {
  // millis()-Differenz statt Summenvergleich: wrap-sicher (Ueberlauf nach 49,7 Tagen)
  if (millis() - timer >= periodeForHB) {
    hbPayload hb;
    hb.ip = getLastIpByte();
    hb.HBperiode = periodeForHB;
    broadcastMsg(HB, hb);
    timer = millis();
  }
}

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}
