// Radar-Kalibrier-Fernbedienung (Touch-Button) auf CYD35.
//
// Basiert auf Udisp6_3_0 (gleiches CYD35-LovyanGFX-Profil inkl. XPT2046-Touch),
// aber UI-reduziert auf einen einzigen Vollflaechen-Touch-Button. Ein Tippen sendet
// dem Radar "Dome" per Unicast eine commandMsg mit cmdCalibrate und startet dort den
// Co-Observation-Kalibriermodus (siehe GesamtKonzeptCatFinder.md, Aufgabe "Welt-Pose
// per Co-Observation kalibrieren"). Statusmeldungen des Radars (Text-Multicast,
// "calib OK/FAIL ...") werden auf dem Display angezeigt.
//
// Geraet: CYD 3.5" (ID CYD35Z), DHCP. Upload per USB (COM9). Ziel: device[Dome] (.37),
// dessen IP wird per HB gelernt.

#define CYD35

#include <xComDef6_3.h>

#define DEBUG true

#include <LXFX_CYD35_UBS_C.h>
LGFX gfx;                       // CYD35-Display + Touch (LovyanGFX-Profil aus MyLGFXConfigs)

#define ID            CYD35Z    // Geraet im Netz: CYD 3.5"
#define targetRadar   Dome      // Ziel-Radar (lernt IP via HB)
#define screenWidth   480
#define screenHight   320
#define CALIB_WINDOW_MS 45000   // Fensterdauer, die der Knopf anfordert (ms)

cmdPayload cmdToSend;
String        lastStatus = "bereit";
bool          btnDown = false;            // Touch-Entprellung (nur Press-Flanke feuert)
unsigned long btnFlashUntil = 0;          // bis wann der Button "gedrueckt" gezeichnet wird

// Button-Rechteck (Bildschirm 480x320, Rotation 3)
const int bx = 50, by = 105, bw = 380, bh = 150;

#include <xComProc6_3.h>

// von xComProc6_3.h erwartet (Debug-Ausgabe)
void writelnComment(String c) { if (DEBUG) Serial << c << endl; }
void writeComment(String c)   { if (DEBUG) Serial << c; }

bool inButton(int x, int y) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

void drawUI(bool pressed) {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextDatum(textdatum_t::middle_center);

  // Titel
  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setTextSize(3);
  gfx.drawString("Radar-Kalibrierung", screenWidth / 2, 45);

  // Button
  uint32_t fill = pressed ? TFT_ORANGE : 0x1f7a1fU;   // gedrueckt: orange, sonst gruen
  gfx.fillRoundRect(bx, by, bw, bh, 16, fill);
  gfx.drawRoundRect(bx, by, bw, bh, 16, TFT_WHITE);
  gfx.setTextColor(TFT_WHITE, fill);
  gfx.setTextSize(4);
  gfx.drawString("KALIBRIEREN", screenWidth / 2, by + bh / 2);

  // Ziel-/Statuszeile
  uint8_t ip = device[targetRadar].IP;
  gfx.setTextSize(2);
  gfx.setTextColor(ip ? TFT_GREEN : TFT_RED, TFT_BLACK);
  gfx.drawString(ip ? ("Ziel: Dome .192.168.0." + String(ip)) : "warte auf Dome (HB) ...",
                 screenWidth / 2, by + bh + 28);
  gfx.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
  gfx.drawString(lastStatus, screenWidth / 2, by + bh + 56);
}

void onButtonPress() {
  uint8_t ip = device[targetRadar].IP;
  if (ip == 0) { lastStatus = "Dome noch nicht im Netz gesehen"; drawUI(false); return; }
  cmdToSend.cmd  = cmdCalibrate;
  cmdToSend.info = CALIB_WINDOW_MS;                  // 0 = Radar-Default; hier explizit 45 s
  bool ok = unicastMsg(commandMsg, cmdToSend, ip);
  lastStatus = ok ? ("gesendet -> Fenster " + String(CALIB_WINDOW_MS / 1000) + "s")
                  : "Senden fehlgeschlagen";
  btnFlashUntil = millis() + 500;
  drawUI(true);
}

void setup() {
  Serial.begin(115200);
  gfx.init();
  gfx.setRotation(3);
  gfx.setBrightness(220);
  drawUI(false);
  setUpWifi(device[ID].IP);                          // DHCP (IP-Byte 0)
  initMcUdp();                                       // lernt Geraete-IPs aus HB (auch Dome)
  initUnicast();
  initText2Udp();
  setUpOTA();
  drawUI(false);
}

void loop() {
  ArduinoOTA.handle();

  // Touch auswerten (nur Press-Flanke)
  uint16_t tx, ty;
  if (gfx.getTouch(&tx, &ty)) {
    if (!btnDown) {
      btnDown = true;
      if (inButton(tx, ty)) onButtonPress();
    }
  } else {
    btnDown = false;
  }

  // Button-Highlight nach kurzer Zeit zuruecknehmen
  if (btnFlashUntil && millis() > btnFlashUntil) { btnFlashUntil = 0; drawUI(false); }

  // HB lernt IPs automatisch (initMcUdp-Callback); Flag nur abraeumen + ggf. neu zeichnen
  if (mcDataReceived) {
    mcDataReceived = false;
    static uint8_t lastIp = 0;
    if (device[targetRadar].IP != lastIp) { lastIp = device[targetRadar].IP; if (!btnFlashUntil) drawUI(false); }
  }

  // Status-Text des Radars anzeigen (Text-Multicast, Port 8300)
  if (udpTextReceived) {
    udpTextReceived = false;
    String m = udpTextMsg; m.trim();
    if (m.length()) { lastStatus = m; if (!btnFlashUntil) drawUI(false); }
  }
}
