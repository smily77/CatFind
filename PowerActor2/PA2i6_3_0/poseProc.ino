// poseProc.ino -- PowerActor2: Traegerpose selbst bestimmen (beim Start + auf Kommando)
//
// Ablauf (nicht blockierend, Zustandsautomat in poseTick):
//   1. Turm nach ganz links, dann langsam ganz nach rechts; der LP40B misst dauernd,
//      jede Messung mit der dabei gelesenen Servo-Position (Rundum-Profil bis ~16 m).
//   2. Rohscan des mitgefuehrten LD06 holen (UDP-Rohdatenstrom "LD06SCAN" wie das
//      Kalibrier-Tool, Port 4320/4321), per Kalibrierung ins Traeger-System drehen.
//   3. Beides an den VPS-Localizer (/localize_carrier, in einer eigenen Task, damit HB
//      und Bus weiterlaufen). Die Pose kommt global aus dem LP40B; der LD06 prueft, ob
//      der Schwenk stimmt (beide sehen im Nahbereich dieselben Konturen).
//   4. Angenommen -> myPose + poseReport, LD06 und Compact Dome bekommen ihre Pose vom
//      Traeger (der LD06 findet sie gegen den reinen Rasenumriss nicht eindeutig, der
//      Compact Dome gar nicht selbst).
//      Abgelehnt -> keine Pose (kein Schuss), neuer Versuch nach POSE_RETRY_MS.
//   Ohne Traegerpose wird die Pose des Compact Dome geloescht: er laedt sonst beim Start
//   seine gespeicherte (NVS) und meldet nach dem Umstellen Ziele am falschen Ort.
// Traeger-System: Ursprung Drehachse, x rechts, y = 0-Grad-Linie (Servo 2048) nach vorn.

enum PoseState { PS_IDLE, PS_TO_START, PS_SWEEP, PS_LD06, PS_HTTP };
PoseState poseState = PS_IDLE;
unsigned long poseStateAt = 0, poseNextAt = POSE_START_DELAY_MS;
bool poseTried = false;                // schon einmal gesucht (sonst: "Traeger startet")

// LP40B-Profil: je 0,5-Grad-Bin bis zu 3 Distanzen (Median gegen Ausreisser)
#define LP_BINS 720
uint16_t lpVal[LP_BINS][3];
uint8_t  lpCnt[LP_BINS];
unsigned long lpLastAt = 0;
uint16_t lpSamples = 0;

// LD06-Scan: je Grad bis zu 7 Distanzen
#define LD_PER_BIN 7
uint16_t ldVal[360][LD_PER_BIN];
uint8_t  ldCnt[360];
WiFiUDP  ldUdp;
unsigned long ldReqAt = 0, ldFirstAt = 0;   // ldFirstAt: erstes Rohdaten-Paket (0 = noch keins)
uint32_t ldPoints = 0;

// Mitgefuehrte Sensoren, die ihre Pose vom Traeger bekommen (PoseHand: hwDef.h)
PoseHand hand[] = {
  {LD06,        LD06_ROT_DEG, LD06_OFF_MM, false, "LD06"},
  {CompactDome, CD_ROT_DEG,   CD_OFF_MM,   true,  "Compact Dome"},
};
#define HAND_N  (sizeof(hand) / sizeof(hand[0]))
#define ldIp    hand[0].ip

// HTTP-Task <-> loop
String   poseBody;
volatile bool poseHttpDone = false;
bool     poseOk = false;
String   poseReason;
int32_t  poseX = 0, poseY = 0;
float    poseH = 0, poseUniq = 0, poseInlier = 0, poseMutual = -1;

bool poseBusy() { return poseState != PS_IDLE; }

static PoseHand* handOf(uint8_t dev) {
  for (auto& h : hand) if (h.dev == dev) return &h;
  return nullptr;
}

static void sendCmd(uint8_t ip, uint8_t cmd, int32_t info) {
  cmdPayload c;
  c.cmd = cmd; c.info = info;
  unicastMsg(commandMsg, c, ip);
}

static void handClear(PoseHand& h) {
  if (!h.clearNoPose || !h.ip) return;
  sendCmd(h.ip, cmdClearPose, 0);
  sendUdpTextln(String("PowerActor2: ") + h.name + "-Pose geloescht (Traeger ohne Pose)");
}

// Kontrolle in 0,5 s ansetzen (bei Uebergabe: neu senden, falls er abweicht)
static void handRecheck(PoseHand& h) {
  h.handed = true; h.ok = false; h.tries = 0; h.gotValid = false;
  h.checkAt = millis() + 500;
}

// Vom Bus: HB (IP) und Pose (Kontrolle der Uebergabe) der mitgefuehrten Sensoren
void poseNoteHB(uint8_t sender) {
  PoseHand* h = handOf(sender);
  if (!h) return;
  bool back = !h->hbAt || millis() - h->hbAt > HAND_REBOOT_MS;   // neu gestartet / wieder gespeist
  h->hbAt = millis();
  h->ip = device[sender].IP;           // traegt initMcUdp aus jedem HB ein
  if (!back) return;
  if (myPose.validWorldPose) handRecheck(*h);
  else handClear(*h);
}
void poseNoteReport(uint8_t sender, const xMsg& m) {
  worldPosePayload wp;
  PoseHand* h = handOf(sender);
  if (!h || !getPayload(m, wp)) return;
  h->gotValid = wp.validWorldPose;
  h->gotX = wp.worldX; h->gotY = wp.worldY;
  // Der LD06 lokalisiert sich nach dem Start auch selbst (gegen den Rasenumriss
  // mehrdeutig), das Radar kalibriert sich evtl. selbst - weicht er ab, gleich neu setzen.
  if (h->handed && !h->checkAt && myPose.validWorldPose &&
      (!h->gotValid || abs(h->gotX - h->wantX) > 100 || abs(h->gotY - h->wantY) > 100)) {
    h->tries = 0;
    h->checkAt = millis() + 500;
  }
}

static uint16_t median3(uint16_t* v, uint8_t n) {
  uint16_t a[LD_PER_BIN];
  for (uint8_t i = 0; i < n; i++) a[i] = v[i];
  for (uint8_t i = 1; i < n; i++)                  // Einfuegesortierung, n <= 7
    for (uint8_t j = i; j > 0 && a[j - 1] > a[j]; j--) { uint16_t t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
  return a[n / 2];
}

// Suche starten (Boot-Automatik oder Kommando "Kalibrieren")
void poseStart(const char* why) {
  if (poseBusy()) return;
  poseTried = true;
  engageStop("Traegerpose-Suche");
  fireOff();
  lidarStop();
  myPose.validWorldPose = false;       // Traeger kann versetzt worden sein
  memset(lpCnt, 0, sizeof(lpCnt));
  memset(ldCnt, 0, sizeof(ldCnt));
  lpSamples = 0; ldPoints = 0;
  for (auto& h : hand) { h.handed = false; h.checkAt = 0; handClear(h); }
  poseState = PS_TO_START;
  poseStateAt = millis();
  servoGoal = POSE_SWEEP_LO;
  if (servoOk) servo.moveTime(POSE_SWEEP_LO, 0, SERVO_FAST);
  sendUdpTextln(String("PowerActor2: Traegerpose-Suche (") + why + "): Turm-Schwenk ...");
}

static void poseFail(const String& why) {
  poseState = PS_IDLE;
  lidarStop();
  servoGoTo(SERVO_CENTER);
  poseNextAt = millis() + POSE_RETRY_MS;
  sendUdpTextln("PowerActor2: Traegerpose NICHT gefunden - " + why +
                (settingOn(stgAutoCalib) ? " (neuer Versuch in " + String(POSE_RETRY_MS / 60000) + " min)"
                                         : String(" (Auto-Kalibrierung aus)")));
}

static void poseHttpTask(void*) {
  poseOk = false; poseReason = "VPS nicht erreichbar";
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://" + ipVPS.toString() + ":" + String(VPS_LOC_PORT) + POSE_VPS_PATH;
    if (http.begin(url)) {
      http.setTimeout(POSE_HTTP_TIMEOUT_MS);
      http.addHeader("Content-Type", "application/json");
      int code = http.POST(poseBody);
      if (code == 200) {
        String r = http.getString();
        int sec = r.indexOf("\"second\"");     // zweitbeste Loesung nicht mitlesen
        if (sec >= 0) { int e = r.indexOf('}', sec); r.remove(sec, e >= 0 ? e - sec + 1 : r.length() - sec); }
        int a = r.indexOf("\"accept\"");
        poseOk = a >= 0 && r.substring(a, a + 16).indexOf("true") > 0;
        poseReason = jsonStr(r, "\"reason\"");
        poseX = (int32_t)lroundf(jsonNum(r, "\"x_mm\""));
        poseY = (int32_t)lroundf(jsonNum(r, "\"y_mm\""));
        poseH = jsonNum(r, "\"heading_deg\"");
        poseUniq = jsonNum(r, "\"uniq\"");
        poseInlier = jsonNum(r, "\"inlier_ratio\"");
        float mu = jsonNum(r, "\"mutual\"");
        poseMutual = isnan(mu) ? -1 : mu;
        if (isnan(poseH)) { poseOk = false; poseReason = "Antwort unvollstaendig"; }
      } else poseReason = "VPS HTTP " + String(code);
      http.end();
    }
  }
  poseBody = String();                 // Speicher frei
  poseHttpDone = true;
  vTaskDelete(nullptr);
}

// Punkte (Traeger-System, mm) als JSON-Liste anhaengen
static void appendPoint(String& b, bool& first, float x, float y) {
  if (!first) b += ',';
  first = false;
  b += '['; b += (int)lroundf(x); b += ','; b += (int)lroundf(y); b += ']';
}

static void buildBody() {
  poseBody = String();
  poseBody.reserve(16000);
  poseBody = "{\"mirror\":1,\"lp\":[";
  bool first = true;
  for (int i = 0; i < LP_BINS; i++) {
    if (!lpCnt[i]) continue;
    float th = ((i + 0.5f) * 4096.0f / LP_BINS - SERVO_CENTER) * (2.0f * PI / 4096.0f);
    float r = median3(lpVal[i], lpCnt[i]) + LP40B_OFFSET_MM;
    appendPoint(poseBody, first, r * sinf(th), r * cosf(th));
  }
  poseBody += "],\"ld\":[";
  first = true;
  float c = cosf(LD06_ROT_DEG * PI / 180.0f), s = sinf(LD06_ROT_DEG * PI / 180.0f);
  for (int b = 0; b < 360; b++) {
    if (ldCnt[b] < 3) continue;
    float d = median3(ldVal[b], ldCnt[b]);
    float lx = d * cosf(b * PI / 180.0f), ly = d * sinf(b * PI / 180.0f);
    appendPoint(poseBody, first, c * lx - s * ly, s * lx + c * ly + LD06_OFF_MM);
  }
  poseBody += "]}";
}

// LD06-Rohdaten (Paket: 'L','6', seq u16, count u8, count x {angle u16 [1/100 Grad], dist u16, q u8})
static void readLd06() {
  int len;
  while ((len = ldUdp.parsePacket()) > 0) {
    static uint8_t buf[5 + 96 * 5];
    int n = ldUdp.read(buf, sizeof(buf));
    if (n < 5 || buf[0] != 'L' || buf[1] != '6') continue;
    if (!ldFirstAt) ldFirstAt = millis();
    if (millis() - ldFirstAt < LD06_SPINUP_MS) continue;      // Motor laeuft erst an
    uint8_t cnt = buf[4];
    for (int i = 0; i < cnt && 5 + 5 * i + 5 <= n; i++) {
      const uint8_t* p = buf + 5 + 5 * i;
      uint16_t ang = p[0] | (p[1] << 8), dist = p[2] | (p[3] << 8);
      uint8_t q = p[4];
      if (q < LD06_MIN_QUALITY || dist < LD06_MIN_MM) continue;
      int b = ((ang + 50) / 100) % 360;
      if (ldCnt[b] < LD_PER_BIN) ldVal[b][ldCnt[b]++] = dist;
      ldPoints++;
    }
  }
}

// Sensor bekommt seine Pose vom Traeger: Ort = Drehachse + offMm in 0-Grad-Richtung,
// Richtung = Traeger + rotDeg (Kalibrierung), gleiche Kommandos wie die manuelle VPS-Pose.
static void sendHandPose(PoseHand& h) {
  if (!h.ip || !myPose.validWorldPose) return;
  float a = myPose.heading * 2.0f * PI / 4096.0f;
  h.wantX = myPose.worldX - (int32_t)lroundf(h.offMm * sinf(a));
  h.wantY = myPose.worldY + (int32_t)lroundf(h.offMm * cosf(a));
  float hd = wrap360f(myPose.heading * 360.0f / 4096.0f + h.rotDeg);
  int32_t hu = (int32_t)lroundf(hd * 4096.0f / 360.0f) % 4096;
  sendCmd(h.ip, cmdManualPoseX, h.wantX);       delay(30);
  sendCmd(h.ip, cmdManualPoseY, h.wantY);       delay(30);
  sendCmd(h.ip, cmdManualPoseHeading, hu);      delay(30);
  sendCmd(h.ip, cmdManualPoseMirrorCommit, 1);
  h.gotValid = false;
  h.checkAt = millis() + HAND_CHECK_MS;
}

static void poseAccept() {
  myPose.worldX = poseX; myPose.worldY = poseY;
  myPose.heading = wrap360f(poseH) * 4096.0f / 360.0f;
  myPose.mirror = 1;
  myPose.validWorldPose = true;
  savePose(myPose);
  sendPoseReport();
  sendUdpTextln(statusText("Traegerpose gefunden") + " (Eindeutigkeit " + String(poseUniq, 2) +
                ", LP40B " + String(poseInlier, 2) + ", LD06 " + String(poseMutual, 2) + ")");
  poseHandOver();
}

// Traegerpose an LD06/Compact Dome weitergeben (auch nach einer manuellen Pose per VPS)
void poseHandOver() {
  for (auto& h : hand) {
    h.tries = 0; h.handed = true; h.ok = false;
    sendHandPose(h);
    if (!h.ip) sendUdpTextln(String("PowerActor2: ") + h.name + " unbekannt - Pose folgt mit seinem HB");
  }
}

// Traeger-Zustand fuer den LD06-Ring (nur Startphase; danach csDone)
static float ldAngle(float carrierDeg) {   // Traeger-Richtung (0 = vorn, rechts +) -> LD06-Winkel
  return wrap360f(90.0f - LD06_ROT_DEG - carrierDeg);
}
static void carrierDisplayTick() {
  static uint8_t last = 0;
  static unsigned long sentAt = 0, failAt = 0;
  static uint8_t doneSends = 0;
  uint8_t st; int32_t val = 0;
  if (poseBusy()) {
    st = poseState == PS_LD06 ? csLdScan : poseState == PS_HTTP ? csVps : csSweep;
    if (st == csSweep) val = lroundf(ldAngle((servoPos - SERVO_CENTER) * 360.0f / 4096.0f));
  } else if (!myPose.validWorldPose) {
    if (!settingOn(stgAutoCalib)) st = csWait;
    else if (!poseTried) st = csStart;
    else {
      long left = (long)(poseNextAt - millis());
      st = csRetry;
      val = left <= 0 ? 0 : (int32_t)(left * 1000L / (long)POSE_RETRY_MS);
    }
  } else {
    bool pending = false, failed = false;
    for (auto& h : hand) {
      if (!h.handed || h.ok) continue;
      if (h.checkAt) pending = true; else failed = true;
    }
    st = pending ? csHandOver : csDone;
    if (failed && !pending) {
      if (!failAt) failAt = millis();
      if (millis() - failAt < CS_FAIL_SHOW_MS) st = csHandFail;
    } else failAt = 0;
    if (st != csDone) val = lroundf(ldAngle(0));
  }
  if (st == csDone) {                  // ein paar Mal melden (UDP), dann still
    if (last != csDone) doneSends = 0;
    if (doneSends >= 3) { last = st; return; }
  }
  unsigned long every = st == csSweep ? CS_SWEEP_MS : CS_REPEAT_MS;
  if (st == last && millis() - sentAt < every) return;
  last = st; sentAt = millis();
  if (st == csDone) doneSends++;
  if (ldIp) sendCmd(ldIp, cmdCarrierState, (int32_t)st | (val << 8));
}

void poseTick() {
  carrierDisplayTick();
  // Pose-Uebergabe kontrollieren (UDP ohne Resend)
  for (auto& h : hand) {
    if (!h.checkAt || (long)(millis() - h.checkAt) < 0) continue;
    h.checkAt = 0;
    bool ok = h.gotValid && abs(h.gotX - h.wantX) < 30 && abs(h.gotY - h.wantY) < 30;
    h.ok = ok;
    if (ok) sendUdpTextln(String("PowerActor2: ") + h.name + "-Pose uebergeben");
    else if (++h.tries < HAND_TRIES) sendHandPose(h);          // evtl. noch blockiert
    else sendUdpTextln(String("PowerActor2: ") + h.name + "-Pose-Uebergabe FEHLGESCHLAGEN");
  }

  switch (poseState) {
    case PS_IDLE:
      // automatisch nur mit stgAutoCalib (z.B. drinnen abschalten: sonst dreht der Turm
      // alle 5 min); das Kommando "Kalibrieren" startet die Suche immer
      if (settingOn(stgAutoCalib) && !myPose.validWorldPose && servoOk &&
          (long)(millis() - poseNextAt) >= 0)
        poseStart("Start");
      return;

    case PS_TO_START: {
      uint16_t p;
      if (servo.readPresentPosition(p)) servoPos = p;
      bool there = servoPos < POSE_SWEEP_LO + 40;
      if (there || millis() - poseStateAt > 6000) {
        if (!there) { poseFail("Turm erreicht den Anfang nicht"); return; }
        lidarStart();
        lpLastAt = lidarAt;
        servoGoal = POSE_SWEEP_HI;
        servo.moveTime(POSE_SWEEP_HI, 0, POSE_SWEEP_SPEED);
        poseState = PS_SWEEP;
        poseStateAt = millis();
      }
      return;
    }

    case PS_SWEEP: {
      if (lidarAt != lpLastAt) {       // neue LP40B-Messung -> Servo-Position dazu lesen
        lpLastAt = lidarAt;
        uint16_t p;
        if (lidarErr == 0 && lidarMm >= POSE_LP_MIN_MM && lidarMm < 65000 &&
            servo.readPresentPosition(p)) {
          servoPos = p;
          int b = (int)((uint32_t)p * LP_BINS / 4096);
          if (b >= LP_BINS) b = LP_BINS - 1;
          if (lpCnt[b] < 3) lpVal[b][lpCnt[b]++] = (uint16_t)lidarMm;
          lpSamples++;
        }
      }
      if (servoPos > POSE_SWEEP_HI - 40 || millis() - poseStateAt > POSE_SWEEP_MAX_MS) {
        lidarStop();
        servoGoTo(SERVO_CENTER);
        if (lpSamples < 200) { poseFail("zu wenige LP40B-Messungen (" + String(lpSamples) + ")"); return; }
        ldUdp.begin(LD06_DATA_PORT);
        ldReqAt = 0; ldFirstAt = 0;
        poseState = PS_LD06;
        poseStateAt = millis();
      }
      return;
    }

    case PS_LD06:
      if (ldIp && millis() - ldReqAt > 1500) {
        ldUdp.beginPacket(IPAddress(192, 168, 0, ldIp), LD06_REQ_PORT);
        ldUdp.write((const uint8_t*)"LD06SCAN", 8);
        ldUdp.endPacket();
        ldReqAt = millis();
      }
      readLd06();
      if ((ldFirstAt && millis() - ldFirstAt > LD06_SPINUP_MS + LD06_SCAN_MS) ||
          millis() - poseStateAt > (ldIp ? LD06_MAX_WAIT_MS : LD06_WAIT_IP_MS)) {
        ldUdp.stop();
        buildBody();
        poseHttpDone = false;
        poseState = PS_HTTP;
        poseStateAt = millis();
        sendUdpTextln("PowerActor2: Schwenk fertig (" + String(lpSamples) + " LP40B, " +
                      String(ldPoints) + " LD06" + (ldIp ? "" : ", LD06 unbekannt") + ") -> VPS");
        xTaskCreatePinnedToCore(poseHttpTask, "poseHttp", 8192, nullptr, 1, nullptr, 0);
      }
      return;

    case PS_HTTP:
      if (!poseHttpDone) return;
      poseState = PS_IDLE;
      if (poseOk) poseAccept();
      else poseFail(poseReason + " (x=" + String(poseX / 1000.0f, 2) + " y=" + String(poseY / 1000.0f, 2) +
                    " h=" + String(poseH, 1) + " Eindeutigkeit " + String(poseUniq, 2) +
                    " LP40B " + String(poseInlier, 2) + " LD06 " + String(poseMutual, 2) + ")");
      return;
  }
}
