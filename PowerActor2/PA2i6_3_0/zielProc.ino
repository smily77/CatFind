// zielProc.ino -- PowerActor2: Ziel verfolgen, Schussbedingungen, Schuss
// (Dateiname bewusst nach hwProc.ino: die IDE haengt die Dateien alphabetisch an.)
//
// Ablauf: catDetected -> ENGAGE_MS lang Ziel verfolgen (catObserved in TRACK_MM
// ersetzen die Position) -> Servo ausrichten -> Laser (+ Ventil) FIRE_MS -> catHit.
// Geschossen wird nur, wenn checkShot() nichts einzuwenden hat; sonst wartet der
// Aktor bis zum Ende des Zeitfensters, ob das Ziel in einen erlaubten Bereich laeuft.

enum EngState { ENG_IDLE, ENG_AIM, ENG_FIRE };
EngState engState = ENG_IDLE;
int32_t  tgtX = 0, tgtY = 0;
unsigned long engageUntil = 0, aimStart = 0, fireStart = 0, lastShotMs = 0;
uint16_t aimServo = SERVO_CENTER;
float    aimDist = 0;
String   lastReason = "";

// Kreuzt die Strecke P->Q die Linie y = VERANDA_Y zwischen x0 und x1?
static bool crossesLine(float px, float py, float qx, float qy, float x0, float x1) {
  float dp = py - VERANDA_Y, dq = qy - VERANDA_Y;
  if ((dp > 0 && dq > 0) || (dp < 0 && dq < 0) || dp == dq) return false;
  float x = px + (VERANDA_Y - py) / (qy - py) * (qx - px);
  return x >= x0 && x <= x1;
}

// Darf auf den Welt-Punkt (wx, wy) geschossen werden? nullptr = ja, sonst der Grund.
// servoOut/distOut = Servo-Position (PA-Winkel) und Entfernung ab Drehachse.
const char* checkShot(int32_t wx, int32_t wy, uint16_t& servoOut, float& distOut) {
  if (!myPose.validWorldPose) return "keine Pose";
  if (!noShotOK) return "keine No-Shot-Karte";
  if (!insideNoShot(wx, wy)) return "ausserhalb der No-Shot-Karte";
  int xl, yl;
  worldToLocal(wx, wy, myPose, xl, yl);
  float phi, r;
  toPaPol(xl, yl, phi, r);
  distOut = r;
  int s = (int)lroundf(phi);
  while (s < 0) s += 4096;
  while (s > 4095) s -= 4096;
  servoOut = (uint16_t)s;
  if (r < NEAR_MM) return "zu nah";
  if (r > WATER_RANGE_MM) return "ausser Reichweite";
  // Veranda-Regel mit der vollen Strahllaenge (Wasser fliegt am Ziel vorbei weiter)
  float px = myPose.worldX, py = myPose.worldY;
  float qx = px + (wx - px) / r * WATER_RANGE_MM;
  float qy = py + (wy - py) / r * WATER_RANGE_MM;
  bool onVeranda = py < VERANDA_Y && px > VERANDA_X0 && px < VERANDA_X1;
  if (onVeranda && !crossesLine(px, py, qx, qy, VERANDA_X0 + VERANDA_MARGIN, VERANDA_X1 - VERANDA_MARGIN))
    return "Strahl ginge an die Verandawand";
  if (!onVeranda && crossesLine(px, py, qx, qy, VERANDA_X0 - VERANDA_MARGIN, VERANDA_X1 + VERANDA_MARGIN))
    return "Strahl ginge auf die Veranda";
  return nullptr;
}

void engageStop(const char* why) {
  if (engState == ENG_FIRE) fireOff();
  if (engState != ENG_IDLE && why) sendUdpTextln(String("PowerActor2: Ziel aufgegeben (") + why + ")");
  engState = ENG_IDLE;
  lidarStop();
}

void onCatDetected(const catDetectedPayload& cd) {
  static catDetectedPayload last;
  static unsigned long lastMs = 0;
  bool dup = millis() - lastMs < CATDET_DUP_MS && memcmp(&last, &cd, sizeof cd) == 0;
  last = cd; lastMs = millis();
  if (dup) return;
  String at = String(cd.worldX / 1000.0f, 1) + "/" + String(cd.worldY / 1000.0f, 1) + " m";
  if (!hbState.readyToFire) { sendUdpTextln("PowerActor2: Katze bei " + at + ", gesichert"); return; }
  if (inhibited())          { sendUdpTextln("PowerActor2: Katze bei " + at + ", Feuersperre"); return; }
  if (millis() - lastShotMs < REFIRE_MS) return;
  if (engState == ENG_FIRE) return;
  tgtX = cd.worldX; tgtY = cd.worldY;
  engageUntil = millis() + ENGAGE_MS;
  if (engState == ENG_IDLE) { aimStart = millis(); lastReason = ""; }
  engState = ENG_AIM;
  lidarStart();                       // misst waehrend des Zielens mit (Protokoll)
}

void onCatObserved(int32_t wx, int32_t wy) {
  if (engState != ENG_AIM) return;
  float dx = wx - tgtX, dy = wy - tgtY;
  if (dx * dx + dy * dy <= (float)TRACK_MM * TRACK_MM) { tgtX = wx; tgtY = wy; }
}

void engageTick() {
  if (engState == ENG_IDLE) return;
  unsigned long now = millis();
  if (engState == ENG_FIRE) {
    if (now - fireStart < FIRE_MS) return;
    fireOff();
    lastShotMs = now;
    reportShot();
    engState = ENG_IDLE;
    lidarStop();
    return;
  }
  if ((long)(now - engageUntil) >= 0) {
    engageStop(lastReason.length() ? lastReason.c_str() : "Zeit abgelaufen");
    return;
  }
  if (!hbState.readyToFire || inhibited()) { engageStop(inhibited() ? "Feuersperre" : "gesichert"); return; }
  uint16_t sv; float dist;
  const char* why = checkShot(tgtX, tgtY, sv, dist);
  if (why) {
    if (lastReason != why) {
      lastReason = why;
      sendUdpTextln("PowerActor2: Ziel " + String(tgtX / 1000.0f, 1) + "/" + String(tgtY / 1000.0f, 1) +
                    " m, kein Schuss: " + why);
    }
    return;
  }
  lastReason = "";
  if (abs((int)sv - (int)servoGoal) > 5) servoGoTo(sv);
  aimServo = sv; aimDist = dist;
  if (!servoAligned() && now - aimStart < AIM_MAX_MS) return;
  fireOn();
  fireStart = now;
  engState = ENG_FIRE;
}

// Nach dem Schuss: catHit auf den Bus + Zeile fuer den VPS-Debug (mit LP40B-Messung)
void reportShot() {
  posPayload hit;
  int xl, yl;
  worldToLocal(tgtX, tgtY, myPose, xl, yl);
  hit.x = xl; hit.y = yl;
  hit.angle = aimServo; hit.radius = aimDist;
  hit.targetSpeed = 0;
  hit.res = lidarFresh() ? (int32_t)lidarMm : 0;    // LP40B-Distanz (0 = keine Messung)
  hit.worldX = tgtX; hit.worldY = tgtY; hit.worldValid = 1;
  hit.sensor = 0;
  broadcastMsg(catHit, hit);
  sendUdpTextln(String("PowerActor2: SCHUSS") + (settingOn(stgWater) ? "" : " (trocken, nur Laser)") +
                " auf " + String(tgtX / 1000.0f, 1) + "/" + String(tgtY / 1000.0f, 1) + " m, " +
                String(aimDist / 1000.0f, 2) + " m, Servo " + String(aimServo) + "/" + String(servoPos) +
                ", LP40B " + (lidarFresh() ? String(lidarMm / 1000.0f, 2) + " m" : String("-")));
}
