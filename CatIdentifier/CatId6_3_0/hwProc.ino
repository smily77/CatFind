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

// No-Shot-Karte aktuell halten: Erstbezug, danach alle MAP_RETRY_MS (ohne Karte) bzw.
// MAP_RECHECK_MS (Fangnetz, stuendlich) neu pruefen, plus sofort bei passendem
// mapInfo-Announce (mapRecheckNow, siehe handleBusMsg in CatId6_3_0.ino).
void serviceNoShotMap() {
  if (mapRecheckNow) {
    if ((long)(millis() - mapRecheckAt) < 0) return;   // Announce-Versatz laeuft noch
  } else {
    // Nur eine ABGEGLICHENE Karte darf bis zum stuendlichen Fangnetz warten - sonst
    // filtert das Modell womoeglich eine Stunde lang mit einer veralteten Karte.
    unsigned long dueMs = (noShotOK && noShotSynced) ? MAP_RECHECK_MS : MAP_RETRY_MS;
    if (lastNoShotTry != 0 && millis() - lastNoShotTry < dueMs) return;
  }
  lastNoShotTry = millis();
  mapRecheckNow = false;
  noShotOK = acquireNoShot(NOSHOT_PATH, device[Manager].IP, MAP_WAIT_MS);
  noShotSynced = mapLastSynced;
  sendUdpTextln(noShotOK ? "No-Shot-Karte aktuell (Modellfilter aktiv)"
                         : "No-Shot-Karte nicht verfuegbar (Modell ungefiltert)");
}

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}
