// Gibt Datensatz so aus wie es sein sollt :-)
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

static BLEAddress sensorAddress("ae:02:8b:83:68:78"); // Ersetzen Sie dies mit der MAC-Adresse Ihres Sensors
static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID("0000fff1-0000-1000-8000-00805f9b34fb");

BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;

void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    Serial.print("Erhaltene Daten: ");
    for (int i = 0; i < length; i++) {
      Serial.printf("%02X ", pData[i]);   
    }
    Serial.println();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starte Arduino BLE Client Anwendung...");
  BLEDevice::init("");

  pClient = BLEDevice::createClient();

  if (pClient->connect(sensorAddress)) {
    Serial.println("Verbunden mit dem Sensor");

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService != nullptr) {
      pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
      if (pRemoteCharacteristic != nullptr && pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
      }
    }
  } else {
    Serial.println("Verbindung zum Sensor fehlgeschlagen");
  }
}

void loop() {
  // Der Loop bleibt leer, da alle Aktionen im Callback ausgeführt werden
}
