void writelnComment(String comment) {
  if (DEBUG) Serial << comment  <<  endl;
  canvas << comment << endl;
  canvas.pushSprite(0, 0);
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
  canvas << comment;
  canvas.pushSprite(0, 0);
}

void println_log(const char *str) {
  Serial.println(str);
  canvas.println(str);
  canvas.pushSprite(0, 0);
}

// einen Record (Header + Payload) aus der Datei lesen - Recordlänge ist variabel
bool readRecord(File &file, xMsg &m) {
  if (file.read((uint8_t*)&m.header, sizeof(msgHeader)) != sizeof(msgHeader)) return false;
  if (m.header.payloadLen > maxPayloadLen) return false;
  if (file.read(m.payload, m.header.payloadLen) != m.header.payloadLen) return false;
  return true;
}

void saveRecord(const xMsg &m) {
  File file = SD.open("/data.bin", FILE_APPEND);
  if (!file) {
    println_log("Datei konnte nicht geöffnet werden");
    return;
  }
  file.write((uint8_t*)&m.header, sizeof(msgHeader));
  if (m.header.payloadLen) file.write(m.payload, m.header.payloadLen);
  file.close();
}

int countRecords() {
  canvas << "count records" << endl;
  canvas.pushSprite(0, 0);

  File file = SD.open("/data.bin", FILE_READ);
  if (!file) {
    println_log("Fehler beim Öffnen der Datei zum Lesen");
    return 0;
  }
  int record = 0;
  xMsg m;
  while (file.available() >= sizeof(msgHeader)) {
    if (!readRecord(file, m)) break;
    record++;
  }
  file.close();
  return record;
}

void sendData(){
  File file = SD.open("/data.bin", FILE_READ);
  if (!file) {
    println_log("Fehler beim Öffnen der Datei zum Lesen");
    return;
  }
  int record = 1;
  xMsg m;
  while (file.available() >= sizeof(msgHeader)) {
    if (!readRecord(file, m)) break;
    broadcastRawMsg(m);   // Header unverändert: originaler sender/timeStamp
    canvas << "rec " << record << " sent" << endl;
    canvas.pushSprite(0, 0);
    record++;
    delay (20);
  }
  file.close();
  canvas.clear();
  canvas.setCursor(0,0);
  writeMenue();
}

void writeMenue() {
  canvas << records << " records" << endl;
  canvas << "1 - Record" << endl;
  canvas << "2 - Play Back" << endl;
  canvas << "3 - Del. File" << endl;
  if (doRecord) canvas << "recording" << endl;
  canvas.pushSprite(0, 0);
}

void deleteFile(){
  if (SD.exists("/data.bin")) {
    SD.remove("/data.bin");
  }
  canvas << "Daten gelöscht"  << endl;
  canvas.pushSprite(0, 0);
}
