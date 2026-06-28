// noShotProc.ino -- No-Shot-Karte beschaffen: lokal validieren, sonst vom Manager

// Wartet bis MAP_WAIT_MS auf einen passenden Unicast und liefert ihn in 'out'.
// codeWanted = erwarteter msgCode; gibt false bei Timeout.
static bool waitForUc(uint8_t codeWanted, xMsg& out, unsigned long budget) {
  unsigned long t0 = millis();
  while (millis() - t0 < budget) {
    ArduinoOTA.handle();
    if (ucDataReceived) {
      out = lastUcMsg;
      ucDataReceived = false;
      if (out.header.msgCode == codeWanted) return true;
    }
    delay(1);
  }
  return false;
}

// No-Shot-Karte sicherstellen: lokale Kopie laden; beim Manager die aktuelle
// Version erfragen; bei Abweichung neu herunterladen; sonst (oder bei
// Nichterreichbarkeit des Managers) die vorhandene alte Karte verwenden.
void acquireNoShot() {
  bool haveLocal = loadNoShot(NOSHOT_PATH);
  uint16_t lv = 0; uint32_t lcrc = 0, llen = 0;
  bool haveLocalInfo = mapFileInfo(NOSHOT_PATH, lv, lcrc, llen);

  uint8_t mgr = device[Manager].IP;               // statische .180
  if (mgr == 0 || !requestMap(mapNoShot, mgr)) {
    Serial << "No-Shot: Manager-Anfrage nicht moeglich -> lokale Karte." << endl;
    noShotOK = haveLocal; return;
  }

  xMsg uc; mapInfoPayload info;
  if (!waitForUc(mapInfo, uc, MAP_WAIT_MS) || !getPayload(uc, info) || info.mapType != mapNoShot) {
    Serial << "No-Shot: kein mapInfo vom Manager -> lokale Karte." << endl;
    noShotOK = haveLocal; return;
  }

  if (haveLocalInfo && info.version == lv && info.fileCrc == lcrc) {
    Serial << "No-Shot: lokale Karte ist aktuell (v" << lv << ")." << endl;
    noShotOK = haveLocal; return;
  }

  Serial << "No-Shot: lade Karte vom Manager (v" << info.version
         << ", " << info.totalLen << " B, " << info.chunkCount << " Chunks) ..." << endl;
  mapRxState rx;
  if (!mapBeginRx(rx, info, NOSHOT_PATH)) { noShotOK = haveLocal; return; }

  int result = 0;
  unsigned long t0 = millis();
  while (rx.active && millis() - t0 < MAP_WAIT_MS) {
    ArduinoOTA.handle();
    if (ucDataReceived) {
      xMsg c = lastUcMsg; ucDataReceived = false;
      if (c.header.msgCode == mapChunk) {
        result = mapFeedChunk(rx, c);
        t0 = millis();                            // Fortschritt -> Timeout zuruecksetzen
        if (result != 0) break;
      }
    }
    delay(1);
  }

  if (result == 1) Serial << "No-Shot: Download OK, CRC bestaetigt." << endl;
  else             Serial << "No-Shot: Download unvollstaendig -> alte Karte bleibt." << endl;

  noShotOK = loadNoShot(NOSHOT_PATH);             // neue (oder unveraenderte alte) Karte laden
}
