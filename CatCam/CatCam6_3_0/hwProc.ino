// Basis-Prozeduren der Cat Cam: HB (der XIAO ESP32-C3 hat keine User-LED) und
// die von xComProc erwarteten Kommentar-Ausgaben.

void heardBeat() {
  if ((timer + periodeForHB) < millis()) {
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
