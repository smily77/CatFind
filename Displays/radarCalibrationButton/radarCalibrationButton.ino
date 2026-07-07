// Radar-Kalibrier-Fernbedienung (Touch) auf CYD35.
//
// Basiert auf Udisp6_3_0 (gleiches CYD35-LovyanGFX-Profil inkl. XPT2046-Touch),
// aber UI-reduziert auf Zielauswahl + zwei Touch-Buttons:
//   * Ziel-Radar     -> Auswahl-Chips oben (alle HLK-Radare aus der Geraetetabelle)
//   * KALIBRIEREN    -> commandMsg/cmdCalibrate  (startet Co-Observation-Kalibrierung;
//                       der Button zaehlt danach das 45-s-Fenster sichtbar herunter)
//   * POSE LOESCHEN  -> commandMsg/cmdClearPose  (gespeicherte Welt-Pose vergessen,
//                       validWorldPose=false -> erzwingt Neukalibrierung)
// Beide gehen per Unicast an das GEWAEHLTE Radar. Statusmeldungen der Radare
// (Text-Multicast, "calib ...", "Pose geloescht ...") werden angezeigt.
//
// POSE-STATUS: Der Knopf fragt die Radare zyklisch per poseRequest ab (unicast)
// und faerbt den Ziel-Chip GELB, sobald das Radar eine gueltige Welt-Pose meldet
// (sonst dunkel). So sieht man nach einer Kalibrierung direkt, ob sie geklappt
// hat. Nach dem Ausloesen wird das gewaehlte Radar ~30 s gezielt beobachtet.
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
unsigned long calibUntil = 0;             // laufendes Kalibrierfenster: Countdown-Ende (0 = keins)
int           lastCountSec = -1;          // zuletzt gezeichnete Restsekunden (nur bei Wechsel malen)

// Ziel-Radare: alle HLK-Geraete aus device[] (in setup() gefuellt)
uint8_t radarIdx[MAX_RADARS];             // device[]-Indizes der Radare
uint8_t radarLastIp[MAX_RADARS];          // zuletzt gezeichnete IP (fuer Redraw-Erkennung)
int8_t  radarPoseValid[MAX_RADARS];       // Welt-Pose je Radar: -1 unbekannt, 0 keine, 1 gueltig
int     nRadars = 0;
int     selRadar = 0;                     // Auswahl (Index in radarIdx)

// Pose-Abfrage: zyklisch poseRequest je Radar; nach Kalibrierung gezielt beobachten
#define POSE_POLL_MS  1500                // Abstand zwischen zwei poseRequests
unsigned long lastPosePoll = 0;
int           posePollIdx = 0;            // Round-Robin-Zeiger ueber die Radare
unsigned long poseWatchUntil = 0;         // bis dahin gezielt das gewaehlte Radar abfragen (nach Kalibrierung)

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

// KALIBRIEREN-Button zeichnen. Waehrend ein Kalibrierfenster laeuft, zaehlt er die
// Restsekunden herunter ("laeuft ... 37s") - nur dieser Button wird dann sekuendlich
// neu gemalt, der Rest des Bildschirms bleibt stehen (kein Flackern).
void drawBtn1(bool pressed) {
  long remainMs = (calibUntil != 0) ? (long)(calibUntil - millis()) : 0;
  int  sec = (remainMs > 0) ? (int)((remainMs + 999) / 1000) : 0;
  uint32_t c1 = pressed ? TFT_ORANGE : (sec > 0 ? 0xb8860bU : 0x1f7a1fU);  // laeuft = ocker
  gfx.fillRoundRect(bx, b1y, bw, bH, 14, c1);
  gfx.drawRoundRect(bx, b1y, bw, bH, 14, TFT_WHITE);
  gfx.setTextColor(TFT_WHITE, c1); gfx.setTextSize(4);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(sec > 0 ? ("laeuft ... " + String(sec) + "s") : "KALIBRIEREN",
                 screenWidth / 2, b1y + bH / 2);
  lastCountSec = sec;
}

// pressed: 0 = keiner, 1 = KALIBRIEREN, 2 = POSE LOESCHEN (Hervorhebung orange)
void drawUI(int pressed) {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextDatum(textdatum_t::middle_center);

  gfx.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx.setTextSize(2);
  gfx.drawString("Radar-Kalibrierung - Ziel antippen", screenWidth / 2, 18);

  // Ziel-Chips: GELB gefuellt = gueltige Welt-Pose, sonst dunkel (blau = gewaehlt ohne Pose).
  // Auswahl = dicker cyan Rahmen. Zeilen: Name, IP (gruen wenn per HB gelernt), Pose-Status.
  for (int i = 0; i < nRadars; i++) {
    uint8_t di = radarIdx[i];
    uint8_t ip = device[di].IP;
    radarLastIp[i] = ip;
    bool poseOk = (radarPoseValid[i] == 1);
    uint32_t fill = poseOk ? 0xB8860BU : ((i == selRadar) ? 0x1a5276U : 0x22262cU);  // ocker/gelb = Pose
    gfx.fillRoundRect(chipX(i), chY, chipW(), chH, 10, fill);
    uint32_t border = (i == selRadar) ? TFT_CYAN : (poseOk ? TFT_YELLOW : 0x555555U);
    gfx.drawRoundRect(chipX(i), chY, chipW(), chH, 10, border);
    if (i == selRadar) gfx.drawRoundRect(chipX(i) + 1, chY + 1, chipW() - 2, chH - 2, 9, border);  // doppelt = dicker
    uint32_t txt = poseOk ? TFT_BLACK : TFT_WHITE;                 // auf Gelb schwarze Schrift
    gfx.setTextColor(txt, fill);
    gfx.setTextSize(2);
    gfx.drawString(device[di].Name, chipX(i) + chipW() / 2, chY + 15);
    gfx.setTextColor(poseOk ? TFT_BLACK : (ip ? TFT_GREEN : TFT_DARKGRAY), fill);
    gfx.drawString(ip ? ("." + String(ip)) : "kein HB", chipX(i) + chipW() / 2, chY + 33);
    const char* ps = (radarPoseValid[i] == 1) ? "Pose OK"
                   : (radarPoseValid[i] == 0) ? "keine Pose" : "Pose ?";
    gfx.setTextColor(poseOk ? TFT_BLACK : (radarPoseValid[i] == 0 ? 0xC04040U : 0x888888U), fill);
    gfx.setTextSize(1);
    gfx.drawString(ps, chipX(i) + chipW() / 2, chY + 48);
  }

  drawBtn1(pressed == 1);

  uint32_t c2 = (pressed == 2) ? TFT_ORANGE : 0x7a1f1fU;   // dunkelrot / gedrueckt orange
  gfx.fillRoundRect(bx, b2y, bw, bH, 14, c2);
  gfx.drawRoundRect(bx, b2y, bw, bH, 14, TFT_WHITE);
  gfx.setTextColor(TFT_WHITE, c2); gfx.setTextSize(4);
  gfx.drawString("POSE LOESCHEN", screenWidth / 2, b2y + bH / 2);

  uint8_t di = radarIdx[selRadar];
  uint8_t ip = device[di].IP;
  const char* pstat = (radarPoseValid[selRadar] == 1) ? " - Pose OK"
                    : (radarPoseValid[selRadar] == 0) ? " - keine Pose" : " - Pose ?";
  gfx.setTextSize(2);
  gfx.setTextColor(ip ? (radarPoseValid[selRadar] == 1 ? TFT_YELLOW : TFT_GREEN) : TFT_RED, TFT_BLACK);
  gfx.drawString(ip ? ("Ziel: " + device[di].Name + " ." + String(ip) + pstat)
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
  if (ok && code == cmdCalibrate) calibUntil = millis() + CALIB_WINDOW_MS;  // Countdown starten
  btnFlashUntil = millis() + 500;
  drawUI(pressed);
}

// poseRequest (ohne Payload) per Unicast an ein Radar schicken -> es broadcastet
// seinen poseReport, den wir in loop() auffangen (Pose-Gueltigkeit fuer den Chip).
void pollRadar(int idx) {
  if (idx < 0 || idx >= nRadars) return;
  uint8_t ip = device[radarIdx[idx]].IP;
  if (ip) unicastMsg(poseRequest, (const void*)nullptr, (uint8_t)0, ip);
}

void setup() {
  Serial.begin(115200);
  // Radar-Liste aus der Geraetetabelle (alle HLK-Typen)
  for (int i = 0; i < deviceCount && nRadars < MAX_RADARS; i++)
    if (device[i].type == HLK) radarIdx[nRadars++] = (uint8_t)i;
  for (int i = 0; i < MAX_RADARS; i++) radarPoseValid[i] = -1;   // Pose zunaechst unbekannt
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
        if (inChip(i, tx, ty)) { selRadar = i; hit = true; drawUI(0); pollRadar(selRadar); }
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

  // Kalibrier-Countdown: sekuendlich nur den KALIBRIEREN-Button nachzeichnen; am Ende
  // einmal zuruecksetzen und das gewaehlte Radar ~30 s gezielt nach seiner Pose fragen
  // (Radar rechnet nach dem Fenster noch ueber den VPS -> Pose kommt etwas verzoegert).
  if (calibUntil && !btnFlashUntil) {
    long remain = (long)(calibUntil - millis());
    if (remain <= 0) { calibUntil = 0; drawBtn1(false); poseWatchUntil = millis() + 30000; }
    else if ((int)((remain + 999) / 1000) != lastCountSec) drawBtn1(false);
  }

  // HB lernt IPs automatisch (initMcUdp-Callback); poseReport der Radare auswerten
  // (Pose-Gueltigkeit fuer die gelbe Chip-Faerbung). Neu zeichnen bei IP- oder Pose-Aenderung.
  if (mcDataReceived) {
    xMsg m = lastMcMsg;
    mcDataReceived = false;
    bool redraw = false;
    for (int i = 0; i < nRadars; i++)
      if (device[radarIdx[i]].IP != radarLastIp[i]) { redraw = true; break; }
    if (m.header.msgCode == poseReport) {
      worldPosePayload wp;
      if (getPayload(m, wp))
        for (int i = 0; i < nRadars; i++)
          if (radarIdx[i] == m.header.sender) {
            int8_t v = wp.validWorldPose ? 1 : 0;
            if (radarPoseValid[i] != v) { radarPoseValid[i] = v; redraw = true; }
            break;
          }
    }
    if (redraw && !btnFlashUntil) drawUI(0);
  }

  // Pose-Status zyklisch abfragen: nach einer Kalibrierung gezielt das gewaehlte Radar,
  // sonst Round-Robin ueber alle Radare mit bekannter IP (unicast poseRequest).
  if (millis() - lastPosePoll > POSE_POLL_MS) {
    lastPosePoll = millis();
    if (poseWatchUntil && (long)(millis() - poseWatchUntil) < 0) {
      pollRadar(selRadar);
    } else {
      poseWatchUntil = 0;
      for (int k = 0; k < nRadars; k++) {
        posePollIdx = (posePollIdx + 1) % nRadars;
        if (device[radarIdx[posePollIdx]].IP) { pollRadar(posePollIdx); break; }
      }
    }
  }

  // Status-Text der Radare anzeigen (Text-Multicast, Port 8300)
  if (udpTextReceived) {
    udpTextReceived = false;
    String m = udpTextMsg; m.trim();
    if (m.length()) { lastStatus = m; if (!btnFlashUntil) drawUI(0); }
  }
}
