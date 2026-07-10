// Basis-Prozeduren des Cat Identifiers: HB (Muster wie beim Radar, aber mit der
// Onboard-LED statt Pixeln) und die von xComProc erwarteten Kommentar-Ausgaben.

void heardBeat() {
  // millis()-Differenz statt Summenvergleich: wrap-sicher (Ueberlauf nach 49,7 Tagen)
  if (millis() - timer >= periodeForHB) {
    hbPayload hb;
    hb.ip = getLastIpByte();
    hb.HBperiode = periodeForHB;
    broadcastMsg(HB, hb);
    statusLightOn = true;
    if (settingOn(stgHbLed)) digitalWrite(CATID_LED, CATID_LED_ON);   // HB-Blitz schaltbar
    timer = millis();
  }
  if (statusLightOn && (millis() - timer >= HB_FLASH_MS)) {
    statusLightOn = false;
    if (!detLedOffMs) digitalWrite(CATID_LED, CATID_LED_OFF);           // Det-Blitz nicht abwuergen
    timer = millis();
  }
}

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}
