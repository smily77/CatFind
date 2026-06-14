#include <Adafruit_NeoPixel.h>

#define mainLaser 3
#define subLaser 21
#define pixelPin 10
#define SDA 0
#define SCL 1
#define TxD 20
#define RxD 4
#define Aux 7

Adafruit_NeoPixel pixels(1, pixelPin, NEO_RGB + NEO_KHZ800);

void setup() {
  pinMode(mainLaser, OUTPUT);
  pinMode(subLaser, OUTPUT);
  digitalWrite(mainLaser,LOW);
  digitalWrite(subLaser,LOW);

  pixels.begin();
  pixels.clear();
}

void loop() {
  pixels.setPixelColor(0, pixels.Color(50, 0, 0));
  pixels.show();
  delay(1000);
  pixels.setPixelColor(0, pixels.Color(0, 50, 0));
  pixels.show();
  delay(1000);
  pixels.setPixelColor(0, pixels.Color(0, 0, 50));
  pixels.show();
  delay(1000);
  pixels.setPixelColor(0, pixels.Color(255, 255, 255));
  pixels.show();
  delay(1000);
  digitalWrite(mainLaser,HIGH);
  delay(200);
  digitalWrite(mainLaser,LOW);
  digitalWrite(subLaser,HIGH);
  delay(1000);
  digitalWrite(subLaser,LOW);
}
