// =============================================================================
//  LaserMarker6_3 - netzwerkgesteuerter Laser-Marker (ESP32-C3 Super Mini)
//
//  Gehoert zur CatFind-6_3-Familie (fixer Header + variabler Payload).
//  Steuerbare Funktionen (jeweils ueber ein commandMsg, typisch per Unicast):
//      - mainLaser  an/aus   (GPIO3,  TTL-Laser-Modul)
//      - subLaser   an/aus   (GPIO21, BS170 -> Laserdiode)
//      - Aux        an/aus   (GPIO7)
//      - Pixelfarbe RGB      (GPIO10, 1x WS2812)
//
//  Der vollstaendige aktuelle Zustand wird zyklisch (periodeForHB) sowie sofort
//  nach jedem Kommando als Heartbeat gebroadcastet -> jederzeit von aussen
//  ablesbar.
//
//  Das EXAKTE Drahtformat (auch fuer Fremdprogramme ausserhalb der CatFind-
//  Serie) ist in ../API_LaserMarker6_3.md beschrieben.
// =============================================================================
#include <xComDef6_3.h>
#include "hwDef.h"
#include <Adafruit_NeoPixel.h>

#define DEBUG true
#define ID LaserMarker

Adafruit_NeoPixel pixels(1, pixelPin, NEO_RGB + NEO_KHZ800);

// Aktueller Zustand aller Ausgaenge. Dient gleichzeitig als HB-Payload, sodass
// ein Empfaenger den Geraetezustand 1:1 zurueckliest.
markerHbPayload state;

unsigned long HBtimer;

#include <xComProc6_3.h>

// Wendet den Inhalt von 'state' auf die Hardware an
void applyOutputs() {
  digitalWrite(mainLaserPin, state.mainLaser ? HIGH : LOW);
  digitalWrite(subLaserPin,  state.subLaser  ? HIGH : LOW);
  digitalWrite(auxPin,       state.aux       ? HIGH : LOW);
  pixels.setPixelColor(0, pixels.Color(state.r, state.g, state.b));
  pixels.show();
}

// Heartbeat mit komplettem Zustand senden (Header fuellt sendXMsg automatisch)
void sendHBmsg() {
  state.hb.ip = getLastIpByte();
  broadcastMsg(HB, state);
}

void assembleHBmsg() {
  state.hb.ip        = getLastIpByte();
  state.hb.HBperiode = periodeForHB;
  // Definierter Startzustand: alles aus, Pixel dunkel
  state.mainLaser = 0;
  state.subLaser  = 0;
  state.aux       = 0;
  state.r = 0; state.g = 0; state.b = 0;
  applyOutputs();
}

void heardBeat() {
  if (millis() - HBtimer >= periodeForHB) {   // ueberlaufsicher
    sendHBmsg();
    HBtimer = millis();
  }
}

// Ein commandMsg auswerten und auf die Ausgaenge anwenden
void handleCommand(const xMsg &m) {
  if (m.header.msgCode != commandMsg) return;
  cmdPayload c;
  if (!getPayload(m, c)) return;

  bool known = true;
  switch (c.cmd) {
    case cmdMainLaser: state.mainLaser = c.info ? 1 : 0; break;
    case cmdSubLaser:  state.subLaser  = c.info ? 1 : 0; break;
    case cmdAux:       state.aux       = c.info ? 1 : 0; break;
    case cmdPixelColor:
      state.r = (c.info >> 16) & 0xFF;
      state.g = (c.info >>  8) & 0xFF;
      state.b = (c.info      ) & 0xFF;
      break;
    case cmdMarkerState:                       // nur Zustand abfragen
      break;
    default:
      known = false;
      break;
  }
  if (known) {
    applyOutputs();
    sendHBmsg();    // sofortige Bestaetigung des neuen Zustands
  }
}

void setup() {
  initHw();
  setUpWifi(device[ID].IP);
  assembleHBmsg();
  initMcUdp();        // fuer das Senden der HB-Broadcasts noetig
  initUnicast();      // Empfang der Kommandos
  setUpOTA();
  // setUpTime();     // optional: sonst bleibt der Header-Timestamp ~0

  HBtimer = millis();
  sendHBmsg();        // einmal beim Start, damit die IP sofort gelernt wird
  Serial << "LaserMarker ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();

  // Kommandos per Unicast (gezielt an dieses Geraet)
  if (ucDataReceived) {
    ucDataReceived = false;
    handleCommand(lastUcMsg);
  }
  // Kommandos per Multicast (z.B. "alle Marker aus") werden ebenfalls akzeptiert;
  // andere Multicast-Nachrichten (HB, catObserved ...) ignoriert handleCommand.
  if (mcDataReceived) {
    mcDataReceived = false;
    handleCommand(lastMcMsg);
  }
}
