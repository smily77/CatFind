// =====================================================================
//  Bedienung_Einstellungen -- Touch-Bedienoberflaeche fuer die CatFinder-Elemente
// ---------------------------------------------------------------------
//  Vollstaendige Kopie des Display-Prototyps (Udisp6_3_0): dieselbe Display-
//  Auswahl per #define, dieselben LovyanGFX-Profile (dispDevLoGFX.h) und
//  Bildschirm-Definitionen (dispDef.h). Das Haupt-/Betriebsprogramm (Karte,
//  Menue, Sprite-Layer) wurde entfernt und durch eine Touch-Bedienung ersetzt:
//
//    Seite 1  Geraeteauswahl   -> aktive/steuerbare Geraete (die per settingsReport
//                                 ihre Faehigkeiten gemeldet haben)
//    Seite 2  Geraetedetails   -> Anzeige-/Automatik-Einstellungen ein-/ausschalten
//                                 (cmdSetSetting) und Aktionen ausloesen (z.B. Radar
//                                 kalibrieren, Welt-Pose kopieren)
//
//  Alles wird auf die jeweilige Aufloesung autoskaliert. Bedienung per Touch
//  (gfx.getTouch); Ziel-IPs werden wie ueblich per HB gelernt.
// =====================================================================
//#define CYD28
//#define CYD35
//#define Sunton7
#define Sunton5
//#define M5Core2
//#define M5Tab5
//#define Wave7

// Flash Size 16MB / PSRAM "OPI PSRAM" (wie beim Prototyp, je nach Board)

#include <xComDef6_3.h>

#define DEBUG true

#include "dispDevLoGFX.h"          // LovyanGFX-Profil des gewaehlten Displays (Objekt gfx)
#include "dispDef.h"               // screenWidth/screenHight/ID des gewaehlten Displays

// --- Zustand der bekannten Geraete (aus settingsReport gelernt) -----------------------
struct DevState {
  bool          known = false;     // hat schon einmal settingsReport gesendet
  uint16_t      sup = 0, val = 0, act = 0;
  unsigned long seen = 0;          // letzter settingsReport (ms)
};
DevState devSt[deviceCount];

// Beschriftungen (Index = stg*/act* aus xComDef6_3.h)
const char* stgLabel[STG_COUNT] = {
  "HB-Anzeige", "Cat-Anzeige", "Auto Pose kop.", "Auto-Kalib.", "Lidar-Motor", "CatDet-Anzeige", "Kamera-KI",
  "Radar Vollmodus"
};
const char* actLabel[ACT_COUNT] = { "Pose kopieren", "Kalibrieren", "Pose loeschen", "Param. laden", "Foto jetzt" };
const uint8_t actCmd[ACT_COUNT] = { cmdCopyPose,     cmdCalibrate, cmdClearPose, cmdReloadParams, cmdTakePhoto };

// --- UI-Zustand -----------------------------------------------------------------------
enum { PAGE_LIST, PAGE_DETAIL };
uint8_t       page = PAGE_LIST;
int           sel  = -1;           // aktuell gewaehltes Geraet
bool          touchDown = false;   // Entprellung: nur Press-Flanke feuert
unsigned long lastReq = 0;         // letzter settingsRequest-Broadcast
String        statusLine = "";     // kurze Rueckmeldung / Radar-Statustext

cmdPayload    cmdToSend;

// Autoskalierung: Textgroesse und Raender aus der Aufloesung
int TS, MARGIN, HEADER_H;

// Touch-Trefferzonen (bei jedem Zeichnen neu aufgebaut)
#define HOT_DEVICE 1
#define HOT_TOGGLE 2
#define HOT_ACTION 3
#define HOT_BACK   4
#define HOT_REFRESH 5
struct Hot { int x, y, w, h; uint8_t type; int arg; };
Hot     hots[48];
int     nHot = 0;

#include <xComProc6_3.h>

// von xComProc6_3.h erwartet (Debug nur auf Serial; das Display zeigt die UI)
void writelnComment(String c) { if (DEBUG) Serial << c << endl; }
void writeComment(String c)   { if (DEBUG) Serial << c; }

// --------------------------------------------------------------------------------------
void addHot(int x, int y, int w, int h, uint8_t type, int arg) {
  if (nHot < 48) { hots[nHot++] = { x, y, w, h, type, arg }; }
}

// Einen Button zeichnen (autoskaliert, zentrierter Text).
void button(int x, int y, int w, int h, const String& label, uint32_t fill, uint32_t txt) {
  gfx.fillRoundRect(x, y, w, h, h / 6, fill);
  gfx.drawRoundRect(x, y, w, h, h / 6, TFT_WHITE);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(txt, fill);
  gfx.setTextSize(TS);
  gfx.drawString(label, x + w / 2, y + h / 2);
}

// Kopfzeile + Statuszeile.
void drawHeader(const String& title) {
  gfx.fillRect(0, 0, screenWidth, HEADER_H, 0x18C3);           // dunkelblau
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(TFT_CYAN, 0x18C3);
  gfx.setTextSize(TS);
  gfx.drawString(title, screenWidth / 2, HEADER_H / 2);
  if (statusLine.length()) {
    gfx.setTextDatum(textdatum_t::bottom_center);
    gfx.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    gfx.setTextSize(max(1, TS - 1));
    gfx.drawString(statusLine, screenWidth / 2, screenHight - 2);
  }
}

// ------------------------ Seite 1: Geraeteauswahl -------------------------------------
void drawList() {
  nHot = 0;
  gfx.fillScreen(TFT_BLACK);
  drawHeader("CatFinder  Einstellungen");

  // Aktualisieren-Button oben rechts
  int rbw = screenWidth / 4, rbh = HEADER_H - 4;
  button(screenWidth - rbw - 2, 2, rbw, rbh, "Aktual.", 0x4228, TFT_WHITE);
  addHot(screenWidth - rbw - 2, 2, rbw, rbh, HOT_REFRESH, 0);

  // steuerbare Geraete sammeln (haben settingsReport gesendet)
  int ids[deviceCount], n = 0;
  for (int i = 0; i < deviceCount; i++)
    if (devSt[i].known && (devSt[i].sup || devSt[i].act)) ids[n++] = i;

  int areaY = HEADER_H + MARGIN;
  int areaH = screenHight - areaY - MARGIN;
  if (n == 0) {
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(TFT_DARKGREY, TFT_BLACK);
    gfx.setTextSize(TS);
    gfx.drawString("warte auf Geraete ...", screenWidth / 2, screenHight / 2);
    return;
  }
  int rows = n;
  int rowH = areaH / rows;
  if (rowH > screenHight / 5) rowH = screenHight / 5;
  int bh = rowH - MARGIN;
  for (int k = 0; k < n; k++) {
    int i = ids[k];
    int y = areaY + k * rowH;
    bool alive = (millis() - devSt[i].seen) < 60000UL || device[i].IP != 0;
    uint32_t col = alive ? 0x1F6F : 0x3186;                  // gruen-blau / grau
    button(MARGIN, y, screenWidth - 2 * MARGIN, bh,
           device[i].Name + "  #" + String(i), col, TFT_WHITE);
    addHot(MARGIN, y, screenWidth - 2 * MARGIN, bh, HOT_DEVICE, i);
  }
}

// ------------------------ Seite 2: Geraetedetails -------------------------------------
void drawDetail() {
  nHot = 0;
  gfx.fillScreen(TFT_BLACK);
  drawHeader(device[sel].Name + "  #" + String(sel));

  // Zurueck-Button (oben links)
  int bbw = screenWidth / 5, bbh = HEADER_H - 4;
  button(2, 2, bbw, bbh, "< zur.", 0x7A00, TFT_WHITE);
  addHot(2, 2, bbw, bbh, HOT_BACK, 0);

  uint16_t sup = devSt[sel].sup, val = devSt[sel].val, act = devSt[sel].act;

  // Zeilen zaehlen (Settings + Aktionen)
  int nRows = 0;
  for (int i = 0; i < STG_COUNT; i++) if (sup & (1u << i)) nRows++;
  for (int i = 0; i < ACT_COUNT; i++) if (act & (1u << i)) nRows++;
  if (nRows == 0) nRows = 1;

  int areaY = HEADER_H + MARGIN;
  int areaH = screenHight - areaY - MARGIN;
  int rowH  = areaH / nRows;
  if (rowH > screenHight / 5) rowH = screenHight / 5;
  int bh = rowH - MARGIN;
  int row = 0;

  bool ipOk = device[sel].IP != 0;

  // Einstellungen: Label + AN/AUS-Schalter
  for (int i = 0; i < STG_COUNT; i++) {
    if (!(sup & (1u << i))) continue;
    int y = areaY + row * rowH;
    gfx.setTextDatum(textdatum_t::middle_left);
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.setTextSize(TS);
    gfx.drawString(stgLabel[i], MARGIN, y + bh / 2);
    bool on = val & (1u << i);
    int tw = screenWidth / 4;
    button(screenWidth - tw - MARGIN, y, tw, bh, on ? "AN" : "AUS",
           on ? 0x2609 : 0x8000, TFT_WHITE);                // gruen / dunkelrot
    addHot(screenWidth - tw - MARGIN, y, tw, bh, HOT_TOGGLE, i);
    row++;
  }
  // Aktionen: ganze Zeile als Button
  for (int i = 0; i < ACT_COUNT; i++) {
    if (!(act & (1u << i))) continue;
    int y = areaY + row * rowH;
    button(MARGIN, y, screenWidth - 2 * MARGIN, bh, String("> ") + actLabel[i],
           0x02B5, TFT_WHITE);                              // cyan-blau
    addHot(MARGIN, y, screenWidth - 2 * MARGIN, bh, HOT_ACTION, i);
    row++;
  }
  if (!ipOk) {
    statusLine = "IP unbekannt (warte auf HB) - Senden evtl. nicht moeglich";
    gfx.setTextDatum(textdatum_t::bottom_center);
    gfx.setTextColor(TFT_ORANGE, TFT_BLACK);
    gfx.setTextSize(max(1, TS - 1));
    gfx.drawString(statusLine, screenWidth / 2, screenHight - 2);
  }
}

void redraw() { if (page == PAGE_LIST) drawList(); else drawDetail(); }

// --------------------------------------------------------------------------------------
// Kommando an das gewaehlte Geraet senden (Unicast, IP aus der per HB gelernten device dB).
bool sendCmd(uint8_t code, int32_t info) {
  uint8_t ip = device[sel].IP;
  if (ip == 0) { statusLine = "Ziel-IP unbekannt"; return false; }
  cmdToSend.cmd = code; cmdToSend.info = info;
  bool ok = unicastMsg(commandMsg, cmdToSend, ip);
  statusLine = ok ? "gesendet" : "Senden fehlgeschlagen";
  return ok;
}

void onHot(const Hot& h) {
  switch (h.type) {
    case HOT_REFRESH:
      broadcastMsg(settingsRequest);
      statusLine = "Geraete abgefragt ...";
      redraw();
      break;
    case HOT_DEVICE:
      sel = h.arg; page = PAGE_DETAIL; statusLine = "";
      broadcastMsg(settingsRequest);          // frischen Stand des Geraets holen
      redraw();
      break;
    case HOT_BACK:
      page = PAGE_LIST; sel = -1; statusLine = "";
      redraw();
      break;
    case HOT_TOGGLE: {
      bool cur = devSt[sel].val & (1u << h.arg);
      int32_t info = (int32_t)((h.arg << 1) | (cur ? 0 : 1));
      if (sendCmd(cmdSetSetting, info))
        devSt[sel].val ^= (1u << h.arg);      // optimistisch; settingsReport bestaetigt
      redraw();
      break;
    }
    case HOT_ACTION: {
      int32_t info = (actCmd[h.arg] == cmdCalibrate) ? 45000 : 0;
      sendCmd(actCmd[h.arg], info);
      redraw();
      break;
    }
  }
}

// --------------------------------------------------------------------------------------
// Display initialisieren (nur der board-spezifische Teil des Prototyps; ohne Sprites).
void initDisplay() {
#if defined(CYD28)
  gfx.init(); gfx.setRotation(3);
#elif defined(CYD35)
  gfx.init(); gfx.setRotation(3); gfx.setBrightness(220);
#elif defined(Sunton7)
  gfx.begin();
#elif defined(Sunton5)
  gfx.begin();
#elif defined(M5Core2)
  auto cfg = M5.config(); M5.begin(cfg);
#elif defined(M5Tab5)
  auto cfg = M5.config(); M5.begin(cfg); gfx.setRotation(3);
#elif defined(Wave7)
  gfx.init(); gfx.setRotation(0);
#endif
  gfx.setBrightness(255);
  gfx.fillScreen(TFT_BLACK);
}

void setup() {
  Serial.begin(115200);
  initDisplay();

  // Autoskalierung aus der Aufloesung ableiten
  TS       = screenWidth >= 1000 ? 4 : screenWidth >= 700 ? 3 : 2;
  MARGIN   = max(6, screenWidth / 60);
  HEADER_H = max(28, screenHight / 8);

#if defined(M5Tab5)
  WiFi.setPins(12, 13, 11, 10, 9, 8, 15);
#endif
  setUpWifi(device[ID].IP);
  initMcUdp();                              // lernt Geraete-IPs aus HB
  initUnicast();
  initText2Udp();
  setUpOTA();
  broadcastMsg(settingsRequest);            // alle steuerbaren Geraete melden sich
  lastReq = millis();
  redraw();
}

void loop() {
  ArduinoOTA.handle();
#if defined(M5Core2) || defined(M5Tab5)
  M5.update();
#endif

  // Touch (nur Press-Flanke auswerten)
  uint16_t tx, ty;
  if (gfx.getTouch(&tx, &ty)) {
    if (!touchDown) {
      touchDown = true;
      for (int i = 0; i < nHot; i++) {
        const Hot& h = hots[i];
        if ((int)tx >= h.x && (int)tx < h.x + h.w && (int)ty >= h.y && (int)ty < h.y + h.h) {
          onHot(h); break;
        }
      }
    }
  } else {
    touchDown = false;
  }

  // periodisch die steuerbaren Geraete abfragen (neue Geraete erscheinen)
  if (page == PAGE_LIST && millis() - lastReq > 8000) {
    broadcastMsg(settingsRequest);
    lastReq = millis();
  }

  // settingsReport vom Bus: Faehigkeiten/Zustand der Geraete lernen
  if (mcDataReceived) {
    xMsg m = lastMcMsg;
    mcDataReceived = false;
    if (m.header.msgCode == settingsReport && m.header.sender < deviceCount) {
      settingsPayload sp;
      if (getPayload(m, sp)) {
        DevState& d = devSt[m.header.sender];
        bool wasKnown = d.known;
        d.known = true; d.sup = sp.supported; d.val = sp.values; d.act = sp.actions;
        d.seen = millis();
        if (page == PAGE_LIST && !wasKnown) redraw();                 // neues Geraet
        else if (page == PAGE_DETAIL && m.header.sender == sel) redraw();  // Zustand bestaetigt
      }
    }
  }

  // Radar-/Geraete-Statustext (Text-Multicast) unten anzeigen
  if (udpTextReceived) {
    udpTextReceived = false;
    String t = udpTextMsg; t.trim();
    if (t.length()) { statusLine = t; redraw(); }
  }
}
