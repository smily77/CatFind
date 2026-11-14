// rangerX40.ino -- Entfernungsmesser X-40 / Hi-AT (40 m, UART 19200 8N1, ASCII-Befehle)
// Library: smily77/Laser_RangeFinder_X40 (X40LaserDistanceMeter, Protokoll noch "Entwurf").
// Beim Start gesucht: Befehl 'S' (Status) auf beiden Pin-Richtungen des Slave-Ports;
// jede Antwort zaehlt (Rohtext wird gemeldet). Scan: Laser einmal an ('O'), je Schritt
// eine Messung ('D'), danach Laser aus ('C').
// ACHTUNG Versorgung: das Modul will 2,0..3,3 V (2,8 V bevorzugt), NICHT 5 V.
#if defined(EPF1_MODE_X40)
#include <X40LaserDistanceMeter.h>

HardwareSerial x40Serial(2);
X40LaserDistanceMeter x40(&x40Serial);
bool x40Found = false;
int  x40Rx = SLAVE_PIN_A, x40Tx = SLAVE_PIN_B;

static bool x40Try(int rx, int tx) {
  x40Serial.end();
  x40.begin(X40_BAUD, rx, tx);
  x40.setTimeout(1000);
  delay(150);
  float t = NAN, v = NAN;
  bool ok = x40.readStatus(&t, &v);
  String raw = x40.lastRawResponse();
  if (ok || raw.length()) {
    x40Rx = rx; x40Tx = tx;
    say("X40 antwortet (ESP-RX GPIO" + String(rx) + ", TX GPIO" + String(tx) + "): S -> '" + raw + "'" +
        (ok ? " = " + String(t, 1) + " C, " + String(v, 2) + " V" : String("")));
    return true;
  }
  return false;
}

bool rangerFind() {
  x40Found = x40Try(SLAVE_PIN_A, SLAVE_PIN_B) || x40Try(SLAVE_PIN_B, SLAVE_PIN_A);
  if (!x40Found) say("KEIN X40 am Slave-Port (19200 Baud, Befehl S, GPIO7/8 beide Richtungen)");
  x40.setTimeout(X40_MEAS_TIMEOUT_MS);
  return x40Found;
}
bool rangerPresent() { return x40Found; }
void rangerPoll() {}
void rangerScanBegin() {
  bool ok = x40.controlLaser(X40_LASER_ON);
  say(String("X40 Laser an: ") + (ok ? "OK" : "'" + x40.lastRawResponse() + "'"));
}
void rangerScanEnd() { x40.controlLaser(X40_LASER_OFF); }
// Laser dauernd an/aus (Zielen/Pruefen von Hand; X40 ist sichtbar)
bool rangerLaser(bool on) {
  if (!x40Found) rangerFind();
  bool ok = x40.controlLaser(on ? X40_LASER_ON : X40_LASER_OFF);
  say(String("X40 Laser ") + (on ? "AN" : "AUS") + ": " + (ok ? "OK" : "'" + x40.lastRawResponse() + "'"));
  return ok;
}

// Eigene Messung statt x40.measure(): der X40 antwortet zweiteilig - erst das Echo "D",
// ~1 s spaeter ": 6.286m,1923" (Meter, Signalqualitaet SQ). Die Library beendet die
// Antwort nach 40 ms Pause, also schon nach dem Echo -> jede zweite Messung scheiterte
// und die naechste bekam den Rest der vorherigen (= Wert vom vorherigen Winkel).
// Hier: lesen bis eine Meterzahl samt Zeilenende da ist (oder "Er"), sonst Timeout.
// (struct X40Result steht in hwDef.h - Arduino-Prototypen)
static X40Result x40Measure() {
  X40Result r = {false, 0, -1, ""};
  while (x40Serial.available()) x40Serial.read();
  x40Serial.write('D');
  unsigned long t0 = millis(), tLast = 0;
  bool haveM = false;
  while (millis() - t0 < X40_MEAS_TIMEOUT_MS) {
    while (x40Serial.available()) {
      char c = (char)x40Serial.read();
      tLast = millis();
      if (c == 13 || c == 10) { if (haveM || r.raw.indexOf("Er") >= 0) goto done; continue; }
      if (c >= 32) r.raw += c;
      if (c == 'm' || c == 'M') haveM = true;
    }
    if ((haveM || r.raw.indexOf("Er") >= 0) && tLast && millis() - tLast > 60) break;   // ohne Zeilenende
    delay(2);
  }
done:
  r.raw.trim();
  int mpos = r.raw.indexOf('m'); if (mpos < 0) mpos = r.raw.indexOf('M');
  if (mpos < 0 || r.raw.indexOf("Er") >= 0) return r;
  int st = mpos;
  while (st > 0 && (isDigit(r.raw[st - 1]) || r.raw[st - 1] == '.')) st--;
  if (st == mpos) return r;
  r.mm = lroundf(r.raw.substring(st, mpos).toFloat() * 1000.0f);
  int comma = r.raw.indexOf(',', mpos);
  if (comma > 0) r.sq = r.raw.substring(comma + 1).toInt();
  r.ok = true;
  return r;
}

// mm (0 = keine gueltige); st: 0 gueltig, 1 Geraetefehler/ausser Bereich, 0xFFFF keine Antwort
uint16_t rangerMeasure(uint16_t& st) {
  X40Result m = x40Measure();
  if (!m.ok) { st = m.raw.length() ? 1 : 0xFFFF; return 0; }
  if (m.mm < X40_MIN_MM || m.mm > X40_MAX_MM) { st = 1; return 0; }
  st = 0;
  return (uint16_t)m.mm;
}

void rangerTest() {
  if (!x40Found) rangerFind();
  if (!x40Found) return;
  for (int i = 0; i < 3; i++) {
    unsigned long t0 = millis();
    X40Result m = x40Measure();
    say("X40 D -> '" + m.raw + "'" + (m.ok ? " = " + String(m.mm) + " mm, SQ " + String(m.sq) : String(" (Fehler)")) +
        ", " + String(millis() - t0) + " ms");
  }
}

#endif  // EPF1_MODE_X40
