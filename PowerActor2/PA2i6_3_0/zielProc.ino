// zielProc.ino -- PowerActor2: Ziel verfolgen, Schussbedingungen, Schuss
// (Dateiname bewusst nach hwProc.ino: die IDE haengt die Dateien alphabetisch an.)
//
// Ablauf: catDetected (oder catTracked, falls das catDetected verloren ging) startet
// die Verfolgung. Die Position liefert der Cat Identifier mit catTracked (~5/s) -
// der Aktor folgt damit genau dem Track, den der Identifier bestaetigt hat. Solange
// der Track lebt, wird gewartet, bis checkShot() nichts mehr einzuwenden hat (z.B.
// bis die Katze in Reichweite ist), dann Servo ausrichten -> Laser + Ventil FIRE_MS
// -> catHit. Weitere Schuesse auf denselben Track fruehestens nach REFIRE_MS.
//
// Laser-Folgemodus (stgLaserFollow, Test): ohne laufende Verfolgung richtet sich der
// Turm auf jedes catObserved in der No-Shot-Karte und nur der Laser leuchtet.

enum EngState { ENG_IDLE, ENG_TRACK, ENG_FIRE };
EngState engState = ENG_IDLE;
int32_t  tgtX = 0, tgtY = 0;
unsigned long engageStart = 0, lastTrackMs = 0, aimStart = 0, fireStart = 0, lastShotMs = 0;
uint16_t aimServo = SERVO_CENTER;
float    aimDist = 0;
String   lastReason = "";
uint8_t  shots = 0;

unsigned long laserFollowMs = 0;       // letztes catObserved im Laser-Folgemodus
bool     laserFollowOn = false;

// Kreuzt die Strecke P->Q die Linie y = VERANDA_Y zwischen x0 und x1?
static bool crossesLine(float px, float py, float qx, float qy, float x0, float x1) {
  float dp = py - VERANDA_Y, dq = qy - VERANDA_Y;
  if ((dp > 0 && dq > 0) || (dp < 0 && dq < 0) || dp == dq) return false;
  float x = px + (VERANDA_Y - py) / (qy - py) * (qx - px);
  return x >= x0 && x <= x1;
}

// Welt-Punkt -> Servo-Position (PA-Winkel) und Entfernung ab Drehachse
static void aimFor(int32_t wx, int32_t wy, uint16_t& servoOut, float& distOut) {
  int xl, yl;
  worldToLocal(wx, wy, myPose, xl, yl);
  float phi, r;
  toPaPol(xl, yl, phi, r);
  distOut = r;
  int s = (int)lroundf(phi);
  while (s < 0) s += 4096;
  while (s > 4095) s -= 4096;
  servoOut = (uint16_t)s;
}

// Darf auf den Welt-Punkt (wx, wy) geschossen werden? nullptr = ja, sonst der Grund.
const char* checkShot(int32_t wx, int32_t wy, uint16_t& servoOut, float& distOut) {
  if (!myPose.validWorldPose) return "keine Pose";
  if (!noShotOK) return "keine No-Shot-Karte";
  if (!insideNoShot(wx, wy)) return "ausserhalb der No-Shot-Karte";
  aimFor(wx, wy, servoOut, distOut);
  float r = distOut;
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

static void laserFollowOff() {
  if (laserFollowOn) { digitalWrite(laserPin, LOW); laserFollowOn = false; }
}

void engageStop(const char* why) {
  if (engState == ENG_FIRE) fireOff();
  if (engState != ENG_IDLE && why)
    sendUdpTextln(String("PowerActor2: Verfolgung beendet (") + why + ", " + String(shots) + " Schuss)");
  engState = ENG_IDLE;
  if (!poseBusy()) lidarStop();       // der Pose-Schwenk braucht den LP40B weiter
}

static String atStr(int32_t x, int32_t y) {
  return String(x / 1000.0f, 1) + "/" + String(y / 1000.0f, 1) + " m";
}

// Bestaetigte Katze (catDetected) oder Nachfuehrung (catTracked): Position uebernehmen,
// bei Bedarf die Verfolgung starten.
static void trackUpdate(const catDetectedPayload& cd, bool detected) {
  if (poseBusy()) {                   // Turm schwenkt fuer die Traegerpose
    if (detected) sendUdpTextln("PowerActor2: Katze bei " + atStr(cd.worldX, cd.worldY) +
                                ", Traegerpose-Suche laeuft");
    return;
  }
  if (!hbState.readyToFire || inhibited()) {
    if (detected) sendUdpTextln("PowerActor2: Katze bei " + atStr(cd.worldX, cd.worldY) +
                                (inhibited() ? ", Feuersperre" : ", gesichert"));
    return;
  }
  tgtX = cd.worldX; tgtY = cd.worldY;
  lastTrackMs = millis();
  if (engState != ENG_IDLE) return;
  laserFollowOff();
  engState = ENG_TRACK;
  engageStart = aimStart = millis();
  lastReason = "";
  shots = 0;
  lidarStart();                       // misst waehrend der Verfolgung mit (Protokoll)
  sendUdpTextln("PowerActor2: verfolge Katze ab " + atStr(tgtX, tgtY));
}

void onCatDetected(const catDetectedPayload& cd) {
  static catDetectedPayload last;
  static unsigned long lastMs = 0;
  bool dup = millis() - lastMs < CATDET_DUP_MS && memcmp(&last, &cd, sizeof cd) == 0;
  last = cd; lastMs = millis();
  if (!dup) trackUpdate(cd, true);
}

void onCatTracked(const catDetectedPayload& cd) { trackUpdate(cd, false); }

// Laser-Folgemodus: nur ohne laufende Verfolgung, nur scharf, ohne Sperre, mit Pose
// und nur in der No-Shot-Karte. Reichweite/Veranda gelten fuer den Laser nicht.
void onCatObserved(int32_t wx, int32_t wy) {
  if (!settingOn(stgLaserFollow) || engState != ENG_IDLE) return;
  if (!hbState.readyToFire || inhibited() || !myPose.validWorldPose) return;
  if (!noShotOK || !insideNoShot(wx, wy)) return;
  uint16_t sv; float d;
  aimFor(wx, wy, sv, d);
  if (abs((int)sv - (int)servoGoal) > 5) servoGoTo(sv);
  digitalWrite(laserPin, HIGH);
  laserFollowOn = true;
  laserFollowMs = millis();
}

void engageTick() {
  unsigned long now = millis();
  if (laserFollowOn && (now - laserFollowMs > LASER_HOLD_MS || !settingOn(stgLaserFollow) ||
                        !hbState.readyToFire || inhibited()))
    laserFollowOff();
  if (engState == ENG_IDLE) return;
  if (engState == ENG_FIRE) {
    if (now - fireStart < FIRE_MS) return;
    fireOff();
    lastShotMs = now;
    shots++;
    reportShot();
    engState = ENG_TRACK;             // Track weiter verfolgen (naechster Schuss nach REFIRE_MS)
    aimStart = now;
    return;
  }
  if (!hbState.readyToFire || inhibited()) { engageStop(inhibited() ? "Feuersperre" : "gesichert"); return; }
  if (now - lastTrackMs > TRACK_LOST_MS) {
    engageStop(lastReason.length() ? (String("Track verloren, zuletzt: ") + lastReason).c_str() : "Track verloren");
    return;
  }
  if (now - engageStart > ENGAGE_MAX_MS) { engageStop("Zeitlimit"); return; }
  uint16_t sv; float dist;
  const char* why = checkShot(tgtX, tgtY, sv, dist);
  if (why) {
    if (lastReason != why) {
      lastReason = why;
      sendUdpTextln("PowerActor2: Katze bei " + atStr(tgtX, tgtY) + ", warte: " + why);
    }
    return;
  }
  lastReason = "";
  if (abs((int)sv - (int)servoGoal) > 5) servoGoTo(sv);   // dem Track mit dem Turm folgen
  aimServo = sv; aimDist = dist;
  if (shots && now - lastShotMs < REFIRE_MS) return;
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
  sendUdpTextln("PowerActor2: SCHUSS " + String(shots) + " auf " + atStr(tgtX, tgtY) + ", " +
                String(aimDist / 1000.0f, 2) + " m, Servo " + String(aimServo) + "/" + String(servoPos) +
                ", LP40B " + (lidarFresh() ? String(lidarMm / 1000.0f, 2) + " m" : String("-")));
}
