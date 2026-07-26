// =============================================================================
//  PA1_1_6_3_0 - PowerActor1_1 in der CatFind-6_3-Familie
//
//  Funktion wie PowerActor2 (PA2i): empfaengt catObserved, richtet den Aktor
//  auf das Ziel und feuert Wasser, wenn scharfgeschaltet und das Ziel innerhalb
//  der Limits liegt. Zustand (readyToFire + Limits) wird als pa2HbPayload
//  gebroadcastet -> drop-in kompatibel mit Display und Button.
//
//  Unterschied zu PA2: Drehturm ueber Schrittmotor (A4988 an PCF8574) statt
//  Bus-Servo; Winkelgeber AS5600; Riemen 20:130; Reed-Kontakte am Turm;
//  Feuer ueber Wasserventil-Relais. Zusaetzliche Relais wirelessPower/extPower.
//
//  Eigenstaendiger 2. PowerActor: ID PA1_1 (16), feste IP .183. Display/Button
//  zeigen weiterhin auf PA2i, bis deren targetSystem/Ziel umgestellt wird.
// =============================================================================
#include <xComDef6_3.h>
#include "hwDef.h"
#include <FastLED.h>
#include <Wire.h>
#include <PCF8574.h>
#include "AS5600.h"
#include <Preferences.h>

#define DEBUG true
#define containLed
#define ID PA1_1

CRGB leds[pixelNum];

TwoWire I2Cone = TwoWire(0);
PCF8574 rotor(&I2Cone, motorAdr);
AS5600  winkel(&I2Cone);

pa2HbPayload hbState;          // gemeinsamer PowerActor-Zustand (drop-in zu PA2)
posPayload   txPos;

Preferences vault;

const int systemAngle = maxAngle * 4096 / 360;          // PA-Einheiten je Halbwinkel
const long microstepsPerTurretRev =
    (long)stepsPerRev * defaultMicrostep * ringTeeth / pulleyTeeth;   // = 20800

long globalPosition = 0;       // Mikroschritte ab Nullpunkt (geradeaus = 0)
bool homed = false;

unsigned long HBtimer;
unsigned long fireTimer;
bool fireOn = false;

#include <xComProc6_3.h>

void setup() {
  initHw();
  setUpWifi(device[ID].IP);
  assembleHBmsg();
  initMcUdp();
  initUnicast();
  setUpOTA();
  //setUpTime();
  initStepper();
  initWinkel();
  homeTurret();               // Drehturm referenzieren (Geometrie: siehe hwProc.ino)

  HBtimer = millis();
  sendHBmsg();
  Serial << "PA1_1 ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();

  // --- Kommandos (Unicast) ---
  if (ucDataReceived) {
    ucDataReceived = false;
    cmdPayload cmd;
    if (getPayload(lastUcMsg, cmd)) {
      switch (cmd.cmd) {
        case cmdReboot:                 // Fernwartung: sofortiger Neustart
          sendUdpTextln("Neustart per Kommando (cmdReboot) ...");
          delay(200);
          ESP.restart();
        break;
        case cmdArmFire:
          hbState.readyToFire = !hbState.readyToFire;
        break;
        case cmdSetLeftLimit:
          hbState.leftLimit = cmd.info;  hbState.limitsActive = true;  saveHBToVault();
        break;
        case cmdSetRightLimit:
          hbState.rightLimit = cmd.info; hbState.limitsActive = true;  saveHBToVault();
        break;
        case cmdSetFarLimit:
          hbState.farLimit = cmd.info;   hbState.limitsActive = true;  saveHBToVault();
        break;
        case cmdSetNearLimit:
          hbState.nearLimit = cmd.info;  hbState.limitsActive = true;  saveHBToVault();
        break;
        case cmdChangeLimitActivation:
          hbState.limitsActive = !hbState.limitsActive;                saveHBToVault();
        break;
        // PA1_1-Relais
        case cmdWirelessPower:
          digitalWrite(wirelessPower, cmd.info ? HIGH : LOW);
        break;
        case cmdExtPower:
          digitalWrite(extPower, cmd.info ? HIGH : LOW);
        break;
      } //switch
      sendHBmsg();
      sendHBmsg();   // doppelt als schnelle Bestaetigung (UDP-Verlustabsicherung)
    }
  } //ucDataReceived

  // --- Zielmeldungen (Multicast) ---
  if (mcDataReceived) {
    mcDataReceived = false;
    if (lastMcMsg.header.msgCode == catObserved) {
      posPayload obs;
      if (getPayload(lastMcMsg, obs)) {
        if (hbState.readyToFire && targetWithinLimits(obs)) {
          digitalWrite(laser, HIGH);
          aimAt(obs.angle);            // Turm auf Ziel drehen (blockierend)
          fireWater();                 // Wasserstoss ausloesen
          broadcastMsg(catHit, obs);   // Treffer melden
        }
      }
    }
  } //mcDataReceived

  // --- Feuer-/Laser-Timeout ---
  if (fireOn && (millis() - fireTimer >= fireDuration)) {
    digitalWrite(water, LOW);
    digitalWrite(laser, LOW);
    fireOn = false;
  }
}
