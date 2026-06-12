#include <FastLED.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Streaming.h>
#include "udpDef.h"
#include "hw.h"
#include <SPI.h>
#include <SD.h>
#include <M5Cardputer.h>
#include "time.h"

const char* ssid = "Rohan";
const char* password = "Good Morning... Darling! @224477%";

WiFiUDP udp;
comMsgStruct comTXMsg,comRXMsg;

struct tm timeinfo;
time_t now;

M5Canvas canvas(&M5Cardputer.Display);

int records = 0;
bool doRecord = false;

int countRecords() {
  canvas << "count records" << endl;
  canvas.pushSprite(0, 0);
  
  File file = SD.open("/data.bin", FILE_READ);
  if (!file) {
    println_log("Fehler beim Öffnen der Datei zum Lesen");
    return 0;
  }
  int record = 1;
  while (file.available() >= sizeof(comMsgStruct)) {
    comMsgStruct x;
    file.read((uint8_t*)&x, sizeof(comMsgStruct));
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
  while (file.available() >= sizeof(comMsgStruct)) {
    comMsgStruct x;
    file.read((uint8_t*)&x, sizeof(comMsgStruct));
    sendMcData(x);
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

void println_log(const char *str) {
    Serial.println(str);
    canvas.println(str);
    canvas.pushSprite(0, 0);
}

void deleteFile(){
  if (SD.exists("/data.bin")) {
    SD.remove("/data.bin");
  }
    canvas << "Daten gelöscht"  << endl;
    canvas.pushSprite(0, 0);
}

void saveRecord(comMsgStruct x) {
  File file = SD.open("/data.bin", FILE_APPEND);
  if (!file) {
    println_log("Datei konnte nicht geöffnet werden");
    return;
  }
  file.write((uint8_t*)&x, sizeof(comMsgStruct));
  file.close();
}
  
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
  
  WiFi.begin(ssid, password);
  canvas << "Verbindung zum WiFi" << endl;
  canvas.pushSprite(0, 0);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial << ".";
    canvas.print(".");
    canvas.pushSprite(0, 0);
  }
  canvas << WiFi.localIP() << endl;
  canvas.pushSprite(0, 0);
  initUdp(); 
  
  configTzTime(time_zone, ntpServer1, ntpServer2);
  while(!getLocalTime(&timeinfo));  

  canvas << "Receiver Ready" << endl;
  writeMenue();
  canvas.pushSprite(0, 0);
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isKeyPressed('1')) {
      doRecord = !doRecord;
      if (!doRecord);
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
    if (receivedMcData(comRXMsg)) {
      if (comRXMsg.msgCode == catObserved) {
        saveRecord(comRXMsg);
        records++;
        canvas << records << " Records" << endl;
        canvas.pushSprite(0, 0);
      }
    }
  }
}
