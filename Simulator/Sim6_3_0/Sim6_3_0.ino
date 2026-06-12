//Sim_5_4 - letzte Version mit eigener udpDef/udpProc
//6_3 - analog zu den übrigen Programmen auf die gemeinsamen xComDef6_3/xComProc6_3 umgestellt:
//      * Nachrichten = fixer Header + variabler Payload, Empfang über initMcUdp Callback
//      * Aufzeichnung auf SD speichert Header + payloadLen Bytes (variable Recordlänge)
//      * Playback sendet mit broadcastRawMsg, originaler sender/timeStamp bleibt erhalten
//      * WLAN-Zugangsdaten aus Credentials.h, Simulator ist als Sim in der device dB
#include <xComDef6_3.h>
#include "hwDef.h"
#include <SPI.h>
#include <SD.h>
#include <M5Cardputer.h>

#define DEBUG true
byte ID = Sim;

M5Canvas canvas(&M5Cardputer.Display);

int records = 0;
bool doRecord = false;

#include <xComProc6_3.h>

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Display.setRotation(1);
  canvas.setColorDepth(1);  // mono color
  canvas.createSprite(M5Cardputer.Display.width(),
                        M5Cardputer.Display.height());
  canvas.setPaletteColor(1, GREEN);
  canvas.setTextSize(2);
  canvas.setTextScroll(true);

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    println_log("Card failed, or not present");
    while (1);
  }
  if (SD.exists("/data.bin")) records = countRecords();
  println_log("SD_Card ready");

  setUpWifi(device[ID].IP);
  initMcUdp();
  setUpTime();

  println_log("Receiver Ready");
  writeMenue();
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isKeyPressed('1')) {
      doRecord = !doRecord;
      canvas.clear();
      canvas.setCursor(0,0);
      writeMenue();
    }

    if (M5Cardputer.Keyboard.isKeyPressed('2')) {
      sendData();
    }
    if (M5Cardputer.Keyboard.isKeyPressed('3')) {
      records =0;
      canvas.clear();
      canvas.setCursor(0,0);
      deleteFile();
      writeMenue();
    }
  }

  if (doRecord) {
    if (mcDataReceived) {
      mcDataReceived = false;
      if (lastMcMsg.header.msgCode == catObserved) {
        saveRecord(lastMcMsg);
        records++;
        canvas << records << " Records" << endl;
        canvas.pushSprite(0, 0);
      }
    }
  }
}
