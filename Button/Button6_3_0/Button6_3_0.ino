//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      Kommandos laufen als commandMsg mit cmdPayload, sender steht im Header
//6_3 (2026-10-30) - Scharf-Schalter fuer ALLE Aktoren (Plan button-inhibit-plan):
//      cmdArmFire absolut (1 = scharf, 2 = sichern) statt Umschalten; ein Tastendruck
//      sichert alle, wenn irgendeiner scharf ist, sonst schaltet er alle scharf.
//      Zustand aus den Aktor-HBs; Wiederholung bis bestaetigt; eigener HB + OTA.
//#define Atom3S
#include <xComDef6_3.h>
#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true
byte ID = Schalter;

#include <xComProc6_3.h>

struct PaState {
  unsigned long hbAt;                // letzter HB (0 = nie)
  uint32_t period;                   // HB-Periode laut Payload
  bool ready;                        // readyToFire laut HB
};
PaState pa[deviceCount];

unsigned long HBtimer = 0;
unsigned long inhibitUntil = 0;      // Personensperre (fireInhibit) laeuft bis
unsigned long noteUntil = 0;         // Fehleranzeige bis
uint8_t  armWant = 0;                // laufendes Kommando: 0 = keins, 1 = scharf, 2 = sichern
uint8_t  armTries = 0;
unsigned long armSentAt = 0;
uint32_t shownColor = 1;             // 1 = noch nichts gezeigt

void setup() {
  M5.begin();
  Serial.begin(115200);
#ifdef Atom3S
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_YELLOW);
  M5.Lcd.print("Setup WiFi");
#else
  initPixel();
#endif
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  sendHB();
  showColor(colUnknown);
  Serial << "ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  M5.update();
#ifdef Atom3S
  if (M5.BtnA.wasPressed()) buttonPressed();
#else
  if (M5.Btn.wasPressed()) buttonPressed();
#endif
  xMsg m;
  while (mcQueuePop(m)) handleBusMsg(m);
  if (ucDataReceived) { ucDataReceived = false; handleCommonMsg(lastUcMsg); }
  armTick();
  if (millis() - HBtimer >= periodeForHB) { sendHB(); HBtimer = millis(); }
  updateDisplay();
}

void sendHB() {
  hbPayload hb;
  hb.ip = getLastIpByte();
  hb.HBperiode = periodeForHB;
  broadcastMsg(HB, hb);
}

void handleBusMsg(const xMsg& m) {
  if (handleCommonMsg(m)) return;
  uint8_t s = m.header.sender;
  if (m.header.msgCode == HB && device[s].type == PowerActor) {
    pa2HbPayload p;
    if (!getPayload(m, p)) return;
    pa[s].hbAt = millis();
    pa[s].period = p.hb.HBperiode;
    pa[s].ready = p.readyToFire;
  } else if (m.header.msgCode == fireInhibit) {
    fireInhibitPayload inh;
    if (!getPayload(m, inh)) return;
    unsigned long left = inhibited() ? inhibitUntil - millis() : 0;
    if (inh.holdMs > left) inhibitUntil = millis() + inh.holdMs;
  }
}

bool inhibited() { return (long)(inhibitUntil - millis()) > 0; }

bool paAlive(uint8_t i) {
  if (device[i].type != PowerActor || !pa[i].hbAt || !device[i].IP) return false;
  unsigned long stale = max((unsigned long)PA_STALE_MIN, (unsigned long)pa[i].period * PA_STALE_HBS);
  return millis() - pa[i].hbAt < stale;
}

// Zaehlt die erreichbaren Aktoren und wie viele davon scharf sind
void paCount(uint8_t& alive, uint8_t& armed) {
  alive = armed = 0;
  for (uint8_t i = 0; i < deviceCount; i++)
    if (paAlive(i)) { alive++; if (pa[i].ready) armed++; }
}

void buttonPressed() {
  uint8_t alive, armed;
  paCount(alive, armed);
  if (!alive) {
    noteUntil = millis() + NOTE_SHOW_MS;
    sendUdpTextln("Button: kein Aktor erreichbar");
    return;
  }
  armWant = armed ? 2 : 1;           // irgendeiner scharf -> alle sichern
  armTries = 0;
  sendArm();
  sendUdpTextln(armWant == 1 ? "Button: alle Aktoren SCHARF" : "Button: alle Aktoren sichern");
}

// Kommando an alle erreichbaren Aktoren, deren Zustand noch nicht stimmt
void sendArm() {
  cmdPayload c;
  c.cmd = cmdArmFire;
  c.info = armWant;
  for (uint8_t i = 0; i < deviceCount; i++)
    if (paAlive(i) && pa[i].ready != (armWant == 1)) unicastMsg(commandMsg, c, device[i].IP);
  armSentAt = millis();
  armTries++;
}

void armTick() {
  if (!armWant || millis() - armSentAt < ARM_RETRY_MS) return;
  bool done = true;
  for (uint8_t i = 0; i < deviceCount; i++)
    if (paAlive(i) && pa[i].ready != (armWant == 1)) done = false;
  if (done) { armWant = 0; return; }
  if (armTries >= ARM_TRIES) {
    armWant = 0;
    noteUntil = millis() + NOTE_SHOW_MS;
    sendUdpTextln("Button: Aktor hat nicht bestaetigt");
    return;
  }
  sendArm();
}

void updateDisplay() {
  uint8_t alive, armed;
  paCount(alive, armed);
  uint32_t c;
  if ((long)(noteUntil - millis()) > 0) c = colError;
  else if (!alive) c = colUnknown;
  else if (inhibited()) c = colInhibit;
  else if (armed == alive) c = colArmed;
  else if (!armed) c = colSafe;
  else c = colMixed;
  if (armWant && (millis() / 150) % 2) c = 0;   // blinkt, bis die Aktoren bestaetigen
  if (c != shownColor) showColor(c);
}

void writelnComment(String comment) {
  if (DEBUG) Serial << comment << endl;
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
}
