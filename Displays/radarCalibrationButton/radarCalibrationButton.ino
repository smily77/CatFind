// Radar-Kalibrier-Fernbedienung (Touch) auf CYD35.
//
// Basiert auf Udisp6_3_0 (gleiches CYD35-LovyanGFX-Profil inkl. XPT2046-Touch),
// aber UI-reduziert auf Zielauswahl + zwei Touch-Buttons:
//   * Ziel-Radar     -> Auswahl-Chips oben (alle HLK-Radare aus der Geraetetabelle)
//   * KALIBRIEREN    -> commandMsg/cmdCalibrate  (startet Co-Observation-Kalibrierung)
//   * POSE LOESCHEN  -> commandMsg/cmdClearPose  (gespeicherte Welt-Pose vergessen,
//                       validWorldPose=false -> erzwingt Neukalibrierung)
// Beide gehen per Unicast an das GEWAEHLTE Radar. Statusmeldungen der Radare
// (Text-Multicast, "calib ...", "Pose geloescht ...") werden angezeigt.
//
// Geraet: CYD 3.5" (ID CYD35Z, classic ESP32), DHCP. Upload per USB (COM9) oder OTA.
// Die Radar-Liste kommt aus device[] (Typ HLK) - neue Radare erscheinen ohne
// Code-Aenderung; die IPs werden per HB gelernt.

#define CYD35

#include <xComDef6_3.h>

#define DEBUG true

#include <LXFX_CYD35_UBS_C.h>
LGFX gfx;                       // CYD35-Display + Touch (LovyanGFX-Profil aus MyLGFXConfigs)

#define ID            CYD35Z    // Geraet im Netz: CYD 3.5"
#define screenWidth   480
#define screenHight   320
#define CALIB_WINDOW_MS 45000   // Fensterdauer, die der Kalibrier-Knopf anfordert (ms)
#define MAX_RADARS    6

cmdPayload    cmdToSend;
String        lastStatus = "bereit";
bool          btnDown = false;            // Touch-Entprellung (nur Press-Flanke feuert)
unsigned long btnFlashUntil = 0;          // bis wann der gedrueckte Button hervorgehoben wird

// Ziel-Radare: alle HLK-Geraete aus device[] (in setup() gefuellt)
uint8_t radarIdx[MAX_RADARS];             // device[]-Indizes der Radare
uint8_t radarLastIp[MAX_RADARS];          // zuletzt gezeichnete IP (fuer Redraw-Erkennung)
int     nRadars = 0;
int     selRadar = 0;                     // Auswahl (Index in radarIdx)

// Layout (Bildschirm 480x320, Rotation 3): Chips oben, zwei Buttons, Statuszeilen
const int chY = 40, chH = 56, chGap = 7;  // Ziel-Chips
const int bx = 40, bw = 400, bH = 78;
const int b1y = 112;            // KALIBRIEREN
const int b2y = 198;            // POSE LOESCHEN

#include <xComProc6_3.h>

// von xComProc6_3.h erwartet (Debug-Ausgabe)
void writelnComment(String c) { if (DEBUG) Serial << c << endl; }
void writeComment(String c)   { if (DEBUG) Serial << c; }

int chipW() { return (screenWidth - 20 - chGap * (nRadars - 1)) / (nRadars > 0 ? nRadars : 1); }
int chipX(int i) { return 10 + i * (chipW() + chGap); }
bool inChip(int i, int x, int y) {
  return x >= chipX(i) && x < chipX(i) + chipW() && y >= chY && y < chY + chH;
}
bool inBtn1(int x, int y) { return x >= bx && x < bx + bw && y >= b1y && y < b1y + bH; }
bool inBtn2(int x, int y) { return x >= bx && x < bx + bw && y >= b2y && y < b2y + bH; }

// pressed: 0 = keiner, 1 = KALIBRIEREN, 2 = POSE LOESCHEN (Hervorhebung orange)
void drawUI(int pressed) {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextDatum(textdatum_t::middle_center);

  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setTextSize(2);
  gfx.drawString("Radar-Kalibrierung - Ziel antippen", screenWidth / 2, 18);

  // Ziel-Chips: gewaehlt = blau gefuellt, sonst dunkel; IP gruen wenn per HB gelernt
  for (int i = 0; i < nRadars; i++) {
    uint8_t di = radarIdx[i];
    uint8_t ip = device[di].IP;
    radarLastIp[i] = ip;
    uint32_t fill = (i == selRadar) ? 0x1a5276U : 0x22262cU;
    gfx.fillRoundRect(chipX(i), chY, chipW(), chH, 10, fill);
    gfx.drawRoundRect(chipX(i), chY, chipW(), chH, 10,
                      (i == selRadar) ? TFT_CYAN : 0x555555U);
    gfx.setTextColor(TFT_WHITE, fill);
    gfx.setTextSize(2);
    gfx.drawString(device[di].Name, chipX(i) + chipW() / 2, chY + 20);
    gfx.setTextColor(ip ? TFT_GREEN : TFT_DARKGRAY, fill);
    gfx.drawString(ip ? ("." + String(ip)) : "kein HB", chipX(i) + chipW() / 2, chY + 42);
  }

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

  uint8_t di = radarIdx[selRadar];
  uint8_t ip = device[di].IP;
  gfx.setTextSize(2);
  gfx.setTextColor(ip ? TFT_GREEN : TFT_RED, TFT_BLACK);
  gfx.drawString(ip ? ("Ziel: " + device[di].Name + " 192.168.0." + String(ip))
                    : ("warte auf " + device[di].Name + " (HB) ..."),
                 screenWidth / 2, 290);
  gfx.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
  gfx.drawString(lastStatus, screenWidth / 2, 310);
}

// Kommando an das gewaehlte Radar senden (info nur fuer cmdCalibrate relevant).
void sendCmd(uint8_t code, const String& okMsg, int pressed) {
  uint8_t di = radarIdx[selRadar];
  uint8_t ip = device[di].IP;
  if (ip == 0) { lastStatus = device[di].Name + " noch nicht im Netz gesehen"; drawUI(0); return; }
  cmdToSend.cmd  = code;
  cmdToSend.info = (code == cmdCalibrate) ? CALIB_WINDOW_MS : 0;
  bool ok = unicastMsg(commandMsg, cmdToSend, ip);
  lastStatus = ok ? (device[di].Name + ": " + okMsg) : "Senden fehlgeschlagen";
  btnFlashUntil = millis() + 500;
  drawUI(pressed);
}

void setup() {
  Serial.begin(115200);
  // Radar-Liste aus der Geraetetabelle (alle HLK-Typen)
  for (int i = 0; i < 18 && nRadars < MAX_RADARS; i++)
    if (device[i].type == HLK) radarIdx[nRadars++] = (uint8_t)i;
  gfx.init();
  gfx.setRotation(3);
  gfx.setBrightness(220);
  drawUI(0);
  setUpWifi(device[ID].IP);                          // DHCP (IP-Byte 0)
  initMcUdp();                                       // lernt Geraete-IPs aus HB (auch Radare)
  initUnicast();
  initText2Udp();
  setUpOTA();
  drawUI(0);
}

void loop() {
  ArduinoOTA.handle();

  // Touch auswerten (nur Press-Flanke): erst Ziel-Chips, dann Aktions-Buttons
  uint16_t tx, ty;
  if (gfx.getTouch(&tx, &ty)) {
    if (!btnDown) {
      btnDown = true;
      bool hit = false;
      for (int i = 0; i < nRadars && !hit; i++)
        if (inChip(i, tx, ty)) { selRadar = i; hit = true; drawUI(0); }
      if (!hit) {
        if      (inBtn1(tx, ty)) sendCmd(cmdCalibrate, "Kalibrierung gestartet (45s)", 1);
        else if (inBtn2(tx, ty)) sendCmd(cmdClearPose, "Pose-Loeschung gesendet", 2);
      }
    }
  } else {
    btnDown = false;
  }

  // Button-Highlight nach kurzer Zeit zuruecknehmen
  if (btnFlashUntil && millis() > btnFlashUntil) { btnFlashUntil = 0; drawUI(0); }

  // HB lernt IPs automatisch (initMcUdp-Callback); neu zeichnen, wenn sich die IP
  // eines Radars geaendert hat (Chip-Anzeige "kein HB" -> ".xx")
  if (mcDataReceived) {
    mcDataReceived = false;
    for (int i = 0; i < nRadars; i++)
      if (device[radarIdx[i]].IP != radarLastIp[i]) { if (!btnFlashUntil) drawUI(0); break; }
  }

  // Status-Text der Radare anzeigen (Text-Multicast, Port 8300)
  if (udpTextReceived) {
    udpTextReceived = false;
    String m = udpTextMsg; m.trim();
    if (m.length()) { lastStatus = m; if (!btnFlashUntil) drawUI(0); }
  }
}
