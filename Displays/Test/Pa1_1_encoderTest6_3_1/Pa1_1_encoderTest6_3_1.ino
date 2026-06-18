// =============================================================================
//  Pa1_1_encoderTest6_3_1 - Display-/Encoder-Seite des AS5600-Umbautests
//  Framework: Udisp6_3_0 (CYD28-Treiber dispDevLoGFX.h + Encoder readEncoder)
//
//  Funktion: Am CYD28-Drehknopf drehen -> Zielwinkel wird per Unicast
//  (cmdRichtung, PA-Winkel 0..4096) an den Turm (encoderTest6_3_1 / PA1_1)
//  geschickt. Der Turm meldet seine Position zurueck (measurement-posPayload:
//  angle = Schritt-Position, radius = AS5600-Position) und das Display zeigt
//  Sollwinkel, Schritt-Istwinkel, AS5600-Istwinkel und deren Differenz.
//
//  Gegenstueck: PowerActor1_1/Tests/encoderTest6_3_1
// =============================================================================
#define CYD28
#include <xComDef6_3.h>

#define DEBUG true

#include "dispDevLoGFX.h"     // LGFX gfx (CYD28, LovyanGFX)
#include "dispDef.h"          // CYD28: ID, Encoder-Pins, Schirmgroesse

#define targetSystem PA1_1    // Zielgeraet (Turm)
#define encStep 32            // PA-Einheiten pro Encoder-Detent (~2.8 Grad)

int targetAngle = 2048;       // Sollwinkel, Start = geradeaus
cmdPayload cmdToSend;

float rxStepAngle = 2048;     // gemeldete Schritt-Position (PA)
float rxAsAngle   = 2048;     // gemeldete AS5600-Position (PA)
int   rxMount     = -1;       // 0 = AS5600 am Stepper, 1 = am Turm
bool  haveReport  = false;

#include <xComProc6_3.h>

float paToDeg(float pa) { return (pa - 2048.0f) * 360.0f / 4096.0f; }

void drawScreen() {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextSize(2);
  gfx.setCursor(6, 6);
  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.print("PA1_1 Encoder-Test");

  gfx.setTextSize(2);
  gfx.setTextColor(TFT_WHITE, TFT_BLACK);

  gfx.setCursor(6, 40);
  gfx.printf("Soll : %4d  %6.1f", targetAngle, paToDeg(targetAngle));

  if (haveReport) {
    gfx.setCursor(6, 70);
    gfx.printf("Schr.: %4d  %6.1f", (int)rxStepAngle, paToDeg(rxStepAngle));

    gfx.setCursor(6, 100);
    gfx.printf("AS56 : %4d  %6.1f", (int)rxAsAngle, paToDeg(rxAsAngle));

    gfx.setCursor(6, 130);
    gfx.setTextColor(TFT_ORANGE, TFT_BLACK);
    gfx.printf("Diff : %6.1f deg", paToDeg(rxStepAngle) - paToDeg(rxAsAngle));

    gfx.setCursor(6, 165);
    gfx.setTextColor(TFT_GREEN, TFT_BLACK);
    gfx.print("AS5600 @ ");
    gfx.print(rxMount == 1 ? "Turm" : (rxMount == 0 ? "Stepper" : "?"));
  } else {
    gfx.setCursor(6, 90);
    gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
    gfx.print("warte auf Turm ...");
  }
}

void setup() {
  Serial.begin(115200);
  initDisp();
  gfx.setCursor(6, 6); gfx.setTextColor(TFT_YELLOW); gfx.print("WiFi ...");
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  drawScreen();
  Serial << "Pa1_1_encoderTest6_3_1 (Display) ready" << endl;
}

void loop() {
  // Encoder -> Sollwinkel -> an den Turm senden
  if (readEncoder(targetAngle, encStep)) {
    if (targetAngle < 0)    targetAngle = 0;
    if (targetAngle > 4096) targetAngle = 4096;
    cmdToSend.cmd  = cmdRichtung;
    cmdToSend.info = targetAngle;
    unicastMsg(commandMsg, cmdToSend, device[targetSystem].IP);
    drawScreen();
  }

  // Rueckmeldung des Turms
  if (mcDataReceived) {
    mcDataReceived = false;
    if (lastMcMsg.header.msgCode == measurement && lastMcMsg.header.sender == targetSystem) {
      posPayload p;
      if (getPayload(lastMcMsg, p)) {
        rxStepAngle = p.angle;
        rxAsAngle   = p.radius;
        rxMount     = p.sensor;
        haveReport  = true;
        drawScreen();
      }
    }
  }
}
