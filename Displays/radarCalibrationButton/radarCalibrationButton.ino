// Radar-Kalibrier-Fernbedienung (Touch) auf CYD35.
//
// Basiert auf Udisp6_3_0 (gleiches CYD35-LovyanGFX-Profil inkl. XPT2046-Touch),
// aber UI-reduziert auf zwei Touch-Buttons:
//   * KALIBRIEREN   -> commandMsg/cmdCalibrate  (startet Co-Observation-Kalibrierung)
//   * POSE LOESCHEN -> commandMsg/cmdClearPose  (gespeicherte Welt-Pose vergessen,
//                       validWorldPose=false -> erzwingt Neukalibrierung)
// Beide gehen per Unicast an das Radar "Dome". Statusmeldungen des Radars
// (Text-Multicast, "calib ...", "Pose geloescht ...") werden angezeigt.
//
// Geraet: CYD 3.5" (ID CYD35Z, classic ESP32), DHCP. Upload per USB (COM9).
// Ziel: device[Dome] (.37), dessen IP wird per HB gelernt.

#define CYD35

#include <xComDef6_3.h>

#define DEBUG true

#include <LXFX_CYD35_UBS_C.h>
LGFX gfx;                       // CYD35-Display + Touch (LovyanGFX-Profil aus MyLGFXConfigs)

#define ID            CYD35Z    // Geraet im Netz: CYD 3.5"
#define targetRadar   Dome      // Ziel-Radar (lernt IP via HB)
#define screenWidth   480
#define screenHight   320
#define CALIB_WINDOW_MS 45000   // Fensterdauer, die der Kalibrier-Knopf anfordert (ms)

cmdPayload    cmdToSend;
String        lastStatus = "bereit";
bool          btnDown = false;            // Touch-Entprellung (nur Press-Flanke feuert)
unsigned long btnFlashUntil = 0;          // bis wann der gedrueckte Button hervorgehoben wird

// Zwei Buttons (Bildschirm 480x320, Rotation 3)
const int bx = 40, bw = 400, bH = 88;
const int b1y = 58;             // KALIBRIEREN
const int b2y = 160;            // POSE LOESCHEN

#include <xComProc6_3.h>

// von xComProc6_3.h erwartet (Debug-Ausgabe)
void writelnComment(String c) { if (DEBUG) Serial << c << endl; }
void writeComment(String c)   { if (DEBUG) Serial << c; }

bool inBtn1(int x, int y) { return x >= bx && x < bx + bw && y >= b1y && y < b1y + bH; }
bool inBtn2(int x, int y) { return x >= bx && x < bx + bw && y >= b2y && y < b2y + bH; }

// pressed: 0 = keiner, 1 = KALIBRIEREN, 2 = POSE LOESCHEN (Hervorhebung orange)
void drawUI(int pressed) {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextDatum(textdatum_t::middle_center);

  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setTextSize(3);
  gfx.drawString("Radar-Kalibrierung", screenWidth / 2, 28);

  uint32_t c1 = (pressed == 1) ? TFT_ORANGE : 0x1f7a1fU;   // gruen / gedrueckt orange
  gfx.fillRoundRect(bx, b1y, bw, bH, 14, c1);
  gfx.drawRoundRect(bx, b1y, bw, bH, 14, TFT_WHITE);
  gfx.setTextColor(TFT_WHITE, c1); gfx.setTextSize(4);
  gfx.drawString("KALIBRIEREN", screenWidth / 2, b1y + bH / 2);

  uint32_t c2 = (pressed == 2) ? TFT_ORANGE : 0x7a1f1fU;   // dunkelrot / gedrueckt orange
  gfx.fillRoundRect(bx, b2y, bw, bH, 14, c2);
  gfx.drawRoundRect(bx, b2y, bw, bH, 14, TFT_WHITE);
  gfx.setTextColor(TFT_WHITE, c2); gfx.setTextSize(4);
  gfx.drawString("POSE LOESCHEN", screenWidth / 2, b2y + bH / 2);

  uint8_t ip = device[targetRadar].IP;
  gfx.setTextSize(2);
  gfx.setTextColor(ip ? TFT_GREEN : TFT_RED, TFT_BLACK);
  gfx.drawString(ip ? ("Ziel: Dome 192.168.0." + String(ip)) : "warte auf Dome (HB) ...",
                 screenWidth / 2, 270);
  gfx.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
  gfx.drawString(lastStatus, screenWidth / 2, 298);
}

// Kommando an das Radar senden (info nur fuer cmdCalibrate relevant).
void sendCmd(uint8_t code, const String& okMsg, int pressed) {
  uint8_t ip = device[targetRadar].IP;
  if (ip == 0) { lastStatus = "Dome noch nicht im Netz gesehen"; drawUI(0); return; }
  cmdToSend.cmd  = code;
  cmdToSend.info = (code == cmdCalibrate) ? CALIB_WINDOW_MS : 0;
  bool ok = unicastMsg(commandMsg, cmdToSend, ip);
  lastStatus = ok ? okMsg : "Senden fehlgeschlagen";
  btnFlashUntil = millis() + 500;
  drawUI(pressed);
}

void setup() {
  Serial.begin(115200);
  gfx.init();
  gfx.setRotation(3);
  gfx.setBrightness(220);
  drawUI(0);
  setUpWifi(device[ID].IP);                          // DHCP (IP-Byte 0)
  initMcUdp();                                       // lernt Geraete-IPs aus HB (auch Dome)
  initUnicast();
  initText2Udp();
  setUpOTA();
  drawUI(0);
}

void loop() {
  ArduinoOTA.handle();

  // Touch auswerten (nur Press-Flanke)
  uint16_t tx, ty;
  if (gfx.getTouch(&tx, &ty)) {
    if (!btnDown) {
      btnDown = true;
      if      (inBtn1(tx, ty)) sendCmd(cmdCalibrate, "Kalibrierung gestartet (45s)", 1);
      else if (inBtn2(tx, ty)) sendCmd(cmdClearPose, "Pose-Loeschung gesendet", 2);
    }
  } else {
    btnDown = false;
  }

  // Button-Highlight nach kurzer Zeit zuruecknehmen
  if (btnFlashUntil && millis() > btnFlashUntil) { btnFlashUntil = 0; drawUI(0); }

  // HB lernt IPs automatisch (initMcUdp-Callback); Flag abraeumen + ggf. neu zeichnen
  if (mcDataReceived) {
    mcDataReceived = false;
    static uint8_t lastIp = 0;
    if (device[targetRadar].IP != lastIp) { lastIp = device[targetRadar].IP; if (!btnFlashUntil) drawUI(0); }
  }

  // Status-Text des Radars anzeigen (Text-Multicast, Port 8300)
  if (udpTextReceived) {
    udpTextReceived = false;
    String m = udpTextMsg; m.trim();
    if (m.length()) { lastStatus = m; if (!btnFlashUntil) drawUI(0); }
  }
}
