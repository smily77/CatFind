// =============================================================================
//  encoderTest6_3_1 - Turm-Seite des Encoder-/AS5600-Umbautests
//  Framework: PA1_1_6_3_0 (gleiche Stepper-/AS5600-/Netz-Basis)
//
//  Funktion: empfaengt einen Zielwinkel (cmdRichtung, PA-Winkel 0..4096) vom
//  CYD28-Display, faehrt den Drehturm dorthin und meldet laufend seine Position
//  (Schritt-Position UND AS5600-Position) als measurement-posPayload zurueck.
//
//  Zweck: Vergleich Schritt- vs. AS5600-Winkel beim Umbau des AS5600 von der
//  Stepper-Welle an den Drehturm. Montage per #define AS5600_ON_TOWER in hwDef.h.
// =============================================================================
#include <xComDef6_3.h>
#include "hwDef.h"
#include <FastLED.h>
#include <Wire.h>
#include <PCF8574.h>

#include "AS5600.h"

#define DEBUG true
#define containLed
#define ID PA1_1

CRGB leds[pixelNum];
TwoWire I2Cone = TwoWire(0);
PCF8574 rotor(&I2Cone, motorAdr);
AS5600  winkel(&I2Cone);

long globalPosition = 0;
bool homed = false;
unsigned long reportTimer;

#include <xComProc6_3.h>

void handleCmd(const xMsg &m) {
  if (m.header.msgCode != commandMsg) return;
  cmdPayload c;
  if (!getPayload(m, c)) return;
  if (c.cmd == cmdRichtung) {     // absoluten PA-Winkel anfahren
    aimAt((float)c.info);
    sendPos();
  }
}

void setup() {
  initHw();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  setUpOTA();
  initStepper();
  initWinkel();
  homeTurret();
  reportTimer = millis();
  sendPos();
  Serial << "encoderTest6_3_1 (Turm) ready" << endl;
}

void loop() {
  ArduinoOTA.handle();

  // Zielwinkel vom Display: per Unicast (gezielt) ODER Multicast (Broadcast)
  if (ucDataReceived) { ucDataReceived = false; handleCmd(lastUcMsg); }
  if (mcDataReceived) { mcDataReceived = false; handleCmd(lastMcMsg); }

  // Position regelmaessig melden
  if (millis() - reportTimer >= reportPeriode) {
    sendPos();
    reportTimer = millis();
  }
}
