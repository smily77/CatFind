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
//   4. Angenommen -> myPose + poseReport, der LD06 bekommt seine Pose vom Traeger
//      (er kann sie gegen den reinen Rasenumriss nicht selbst eindeutig finden).
//      Abgelehnt -> keine Pose (kein Schuss), neuer Versuch nach POSE_RETRY_MS.
// Traeger-System: Ursprung Drehachse, x rechts, y = 0-Grad-Linie (Servo 2048) nach vorn.

enum PoseState { PS_IDLE, PS_TO_START, PS_SWEEP, PS_LD06, PS_HTTP };
PoseState poseState = PS_IDLE;
unsigned long poseStateAt = 0, poseNextAt = POSE_START_DELAY_MS;
bool poseAuto = true;                  // automatisch (neu) suchen, solange keine Pose da ist

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
uint8_t  ldIp = 0;                     // letztes Oktett des LD06 (DHCP, aus seinem HB)
WiFiUDP  ldUdp;
unsigned long ldReqAt = 0, ldFirstAt = 0;   // ldFirstAt: erstes Rohdaten-Paket (0 = noch keins)
uint32_t ldPoints = 0;
int32_t  ldPoseX = 0, ldPoseY = 0;     // zuletzt vom LD06 gemeldete Pose (poseReport)
bool     ldPoseValid = false;
uint8_t  ldPoseTries = 0;
unsigned long ldPoseCheckAt = 0;
int32_t  ldWantX = 0, ldWantY = 0;
bool     ldHanded = false;             // LD06 hat seine Pose vom Traeger bekommen

// HTTP-Task <-> loop
String   poseBody;
volatile bool poseHttpDone = false;
bool     poseOk = false;
String   poseReason;
int32_t  poseX = 0, poseY = 0;
float    poseH = 0, poseUniq = 0, poseInlier = 0, poseMutual = -1;

bool poseBusy() { return poseState != PS_IDLE; }

// Vom Bus: HB des LD06 (IP) und seine Pose (Kontrolle der Uebergabe)
void poseNoteHB(uint8_t sender) {
  if (sender == LD06) ldIp = device[LD06].IP;   // traegt initMcUdp aus jedem HB ein
}
void poseNoteReport(uint8_t sender, const xMsg& m) {
  worldPosePayload wp;
  if (sender != LD06 || !getPayload(m, wp)) return;
  ldPoseValid = wp.validWorldPose;
  ldPoseX = wp.worldX; ldPoseY = wp.worldY;
  // Der LD06 lokalisiert sich nach dem Start auch selbst (gegen den Rasenumriss
  // mehrdeutig) - ueberschreibt er die Traeger-Pose danach, gleich wieder setzen.
  if (ldHanded && !ldPoseCheckAt && myPose.validWorldPose &&
      (!ldPoseValid || abs(ldPoseX - ldWantX) > 100 || abs(ldPoseY - ldWantY) > 100)) {
    ldPoseTries = 0;
    ldPoseCheckAt = millis() + 500;
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
  engageStop("Traegerpose-Suche");
  fireOff();
  lidarStop();
  myPose.validWorldPose = false;       // Traeger kann versetzt worden sein
  memset(lpCnt, 0, sizeof(lpCnt));
  memset(ldCnt, 0, sizeof(ldCnt));
  lpSamples = 0; ldPoints = 0;
  ldHanded = false; ldPoseCheckAt = 0;
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
                " (neuer Versuch in " + String(POSE_RETRY_MS / 60000) + " min)");
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

// LD06 bekommt seine Pose vom Traeger: Ort = Drehachse + LD06_OFF_MM in 0-Grad-Richtung,
// Richtung = Traeger + LD06_ROT_DEG (Kalibrierung), gleiche Kommandos wie die manuelle VPS-Pose.
static void sendLd06Pose() {
  if (!ldIp || !myPose.validWorldPose) return;
  float a = myPose.heading * 2.0f * PI / 4096.0f;
  ldWantX = myPose.worldX - (int32_t)lroundf(LD06_OFF_MM * sinf(a));
  ldWantY = myPose.worldY + (int32_t)lroundf(LD06_OFF_MM * cosf(a));
  float hd = myPose.heading * 360.0f / 4096.0f + LD06_ROT_DEG;
  while (hd >= 360.0f) hd -= 360.0f;
  int32_t hu = (int32_t)lroundf(hd * 4096.0f / 360.0f) % 4096;
  cmdPayload c;
  c.cmd = cmdManualPoseX;            c.info = ldWantX; unicastMsg(commandMsg, c, ldIp); delay(30);
  c.cmd = cmdManualPoseY;            c.info = ldWantY; unicastMsg(commandMsg, c, ldIp); delay(30);
  c.cmd = cmdManualPoseHeading;      c.info = hu;      unicastMsg(commandMsg, c, ldIp); delay(30);
  c.cmd = cmdManualPoseMirrorCommit; c.info = 1;       unicastMsg(commandMsg, c, ldIp);
  ldPoseValid = false;
  ldPoseCheckAt = millis() + LD06_POSE_CHECK_MS;
}

static void poseAccept() {
  myPose.worldX = poseX; myPose.worldY = poseY;
  myPose.heading = wrap360f(poseH) * 4096.0f / 360.0f;
  myPose.mirror = 1;
  myPose.validWorldPose = true;
  savePose(myPose);
  sendPoseReport();
  poseAuto = true;
  sendUdpTextln(statusText("Traegerpose gefunden") + " (Eindeutigkeit " + String(poseUniq, 2) +
                ", LP40B " + String(poseInlier, 2) + ", LD06 " + String(poseMutual, 2) + ")");
  ldPoseTries = 0;
  ldHanded = true;
  sendLd06Pose();
}

void poseTick() {
  // LD06-Pose-Uebergabe kontrollieren (UDP ohne Resend)
  if (ldPoseCheckAt && (long)(millis() - ldPoseCheckAt) >= 0) {
    ldPoseCheckAt = 0;
    bool ok = ldPoseValid && abs(ldPoseX - ldWantX) < 30 && abs(ldPoseY - ldWantY) < 30;
    if (ok) sendUdpTextln("PowerActor2: LD06-Pose uebergeben");
    else if (++ldPoseTries < LD06_POSE_TRIES) sendLd06Pose();   // LD06 evtl. noch blockiert
    else sendUdpTextln("PowerActor2: LD06-Pose-Uebergabe FEHLGESCHLAGEN");
  }

  switch (poseState) {
    case PS_IDLE:
      if (poseAuto && !myPose.validWorldPose && servoOk && (long)(millis() - poseNextAt) >= 0)
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
