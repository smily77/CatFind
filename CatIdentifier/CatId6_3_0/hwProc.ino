// Basis-Prozeduren des Cat Identifiers: HB (Muster wie beim Radar, aber mit der
// Onboard-LED statt Pixeln) und die von xComProc erwarteten Kommentar-Ausgaben.

void heardBeat() {
  if ((timer + periodeForHB) < millis()) {
    hbPayload hb;
    hb.ip = getLastIpByte();
    hb.HBperiode = periodeForHB;
    broadcastMsg(HB, hb);
    statusLightOn = true;
    if (settingOn(stgHbLed)) digitalWrite(CATID_LED, HIGH);   // HB-Blitz schaltbar
    timer = millis();
  }
  if (statusLightOn && ((timer + HB_FLASH_MS) < millis())) {
    statusLightOn = false;
    if (!detLedOffMs) digitalWrite(CATID_LED, LOW);           // Det-Blitz nicht abwuergen
    timer = millis();
  }
}

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}
