#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
//only LD2450
int scanTime = 10; // In seconds

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      String testString = "0000af30-0000-1000-8000-00805f9b34fb";
      if (testString.equals(advertisedDevice.getServiceUUID().toString().c_str())) {
        Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
        if (advertisedDevice.haveServiceUUID()) {
          Serial.print("Device has Service UUID: ");
          Serial.println(advertisedDevice.getServiceUUID().toString().c_str());
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  BLEScan* pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
}

void loop() {
  Serial.println("Scanning...");
  BLEScan* pBLEScan = BLEDevice::getScan();
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  Serial.print("Devices found: ");
  Serial.println(foundDevices.getCount());
  Serial.println("Scan done!");
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
  delay(2000);
}
