/*
 * Step1_ServoSweep — PowerActor2 nach dem Umbau, Schritt 1: nur den Servo drehen
 *
 * Der Servobus-Adapter ist ersetzt durch einen einfachen Widerstand, die
 * Verdrahtung entspricht MapDiscovery/PanTiltHead_WM (TX ueber 1 kOhm):
 *   TX (GPIO19) --[1kΩ]--+-- SERVO BUS DATA
 *   RX (GPIO18) ----------+
 * Servo: Feetech ST3020 (STS-Protokoll), 1 Mbit/s, Lib FeetechBusServos
 * (smily77/SerialServo) wie im PanTiltHead_WM.
 *
 * Ablauf:
 *   - sucht den Servo (ID 0..SCAN_MAX_ID per ping), alter PA2 = ID 1
 *   - findet er keinen: Diagnose (Echo-Test der Leitung + andere Baudraten),
 *     dann alle 5 s erneut suchen (Servo darf im Betrieb getauscht werden)
 *   - Position-Mode, Torque an, faehrt zur Mitte (2048)
 *   - pendelt dann alle SWING_PERIOD_MS zwischen SWING_LEFT und SWING_RIGHT
 *     (innerhalb der alten Anschlaege leftStopp 760 / rightStopp 3400)
 *   - gibt Soll/Ist-Position, Temperatur und Strom aus
 *   - WLAN mit fester IP 192.168.0.181 (PowerActor1 = PA2i in xComDef) + ArduinoOTA
 *
 * Ausgabe: Serial (115200) UND Telnet Port 23 (ohne USB: telnet 192.168.0.181).
 *          Beim Verbinden per Telnet laeuft die Diagnose sofort neu.
 * Board  : ESP32 classic, FQBN esp32:esp32:esp32
 * OTA    : arduino-cli upload --fqbn esp32:esp32:esp32 --port 192.168.0.181 .
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Credentials.h>   // ssid, password
#include <FeetechBus.h>
#include <FeetechST3020.h>

// ── Servo-Bus ────────────────────────────────────────────────────────────────

#define SERVO_RX_PIN   18
#define SERVO_TX_PIN   19
#define SERVO_BAUD     1000000
#define SCAN_MAX_ID    20

#define SWING_LEFT       1200
#define SWING_RIGHT      2900
#define SWING_CENTER     2048
#define SWING_MOVE_MS    1500
#define SWING_PERIOD_MS  3000
#define SWING_SPEED      800    // STS: Speed begrenzt, Zeit allein wirkt nicht zuverlaessig

FeetechBus    bus(Serial1);
FeetechST3020 servo(bus, 1);
static bool   servoFound = false;

// ── Ausgabe auf Serial + Telnet ──────────────────────────────────────────────

static WiFiServer telnetServer(23);
static WiFiClient telnet;

static void logf(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    if (telnet && telnet.connected()) telnet.print(buf);
}

// ── WLAN / OTA ───────────────────────────────────────────────────────────────

#define HOST_NAME   "PowerActor1"
#define LAST_OCTET  181

static void setUpWifi() {
    WiFi.mode(WIFI_STA);
    IPAddress localIP(192, 168, 0, LAST_OCTET);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(192, 168, 0, 1);
    WiFi.config(localIP, gateway, subnet, dns);
    WiFi.setHostname(HOST_NAME);
    WiFi.begin(ssid, password);
    logf("Verbinde mit WLAN \"%s\"", ssid);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 30000) {
            logf("\nWLAN fehlgeschlagen - Neustart\n");
            delay(1000);
            ESP.restart();
        }
        logf(".");
        delay(250);
    }
    logf("\nWLAN ok, IP %s, RSSI %d dBm\n",
         WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

static void setUpOTA() {
    ArduinoOTA.setHostname(HOST_NAME);
    ArduinoOTA
        .onStart([]() {
            if (servoFound) servo.setTorque(false);   // waehrend Update nicht weiterdrehen
            logf("OTA Start\n");
        })
        .onEnd([]() { logf("\nOTA fertig\n"); })
        .onProgress([](unsigned int p, unsigned int t) {
            Serial.printf("OTA %u%%\r", p * 100 / t);
        })
        .onError([](ota_error_t e) { logf("OTA Fehler %u\n", e); });
    ArduinoOTA.begin();
}

// ── Servo ────────────────────────────────────────────────────────────────────

static bool scanIds(uint8_t maxId = SCAN_MAX_ID) {
    for (uint8_t id = 0; id <= maxId; id++) {
        servo.setId_local(id);
        if (servo.ping()) {
            logf("Servo gefunden: ID %u\n", id);
            return true;
        }
    }
    return false;
}

// Echo-Test: TX und RX haengen ueber den 1-kOhm-Widerstand an derselben
// Datenleitung, also muss jedes gesendete Byte auf RX zurueckkommen. Kommt
// kein Echo, stimmt die Verbindung TX-Widerstand-RX nicht (oder die Leitung
// wird von aussen festgehalten).
static void echoTest(int8_t rx, int8_t tx, uint32_t baud) {
    Serial1.end();
    Serial1.begin(baud, SERIAL_8N1, rx, tx);
    delay(20);
    while (Serial1.available()) Serial1.read();
    const uint8_t probe[4] = {0x55, 0xAA, 0x0F, 0xF0};
    Serial1.write(probe, sizeof(probe));
    Serial1.flush();
    delay(10);
    int n = 0, match = 0;
    while (Serial1.available()) {
        int c = Serial1.read();
        if (n < 4 && c == probe[n]) match++;
        n++;
    }
    logf("  Echo RX%d TX%d @%lu: %d Bytes zurueck, %d/4 richtig%s\n",
         rx, tx, (unsigned long)baud, n, match,
         match == 4 ? "  (Leitung ok)" : "  (Leitung NICHT ok)");
}

// Sucht den Servo mit der Soll-Verdrahtung. Mit diagnose: zeigt ein
// Echo-Test, ob die Leitung TX-Widerstand-RX steht, und es wird mit anderen
// Baudraten gesucht.
static void findServo(bool diagnose) {
    Serial1.end();
    bus.beginPins(SERVO_BAUD, SERVO_RX_PIN, SERVO_TX_PIN);
    delay(50);
    servoFound = scanIds();
    if (servoFound) return;
    if (!diagnose) {
        logf("KEIN Servo gefunden\n");
        return;
    }
    logf("KEIN Servo auf RX%d/TX%d @1M - Diagnose:\n", SERVO_RX_PIN, SERVO_TX_PIN);
    echoTest(SERVO_RX_PIN, SERVO_TX_PIN, SERVO_BAUD);
    Serial1.end();
    bus.beginPins(SERVO_BAUD, SERVO_RX_PIN, SERVO_TX_PIN);
    delay(50);
    logf("  Suche ID 0..253 @1M: ");
    if (scanIds(253)) { servoFound = true; return; }
    logf("nichts\n");
    const uint32_t bauds[] = {500000, 250000, 115200};
    for (uint32_t b : bauds) {
        Serial1.end();
        bus.beginPins(b, SERVO_RX_PIN, SERVO_TX_PIN);
        delay(50);
        logf("  Suche @%lu: ", (unsigned long)b);
        if (scanIds()) logf("  -> Servo laeuft mit DIESER Baudrate\n");
        else logf("nichts\n");
    }
    Serial1.end();
    bus.beginPins(SERVO_BAUD, SERVO_RX_PIN, SERVO_TX_PIN);   // zurueck auf Soll
    delay(50);
    servoFound = false;
}

static void initServo(bool diagnose) {
    findServo(diagnose);
    if (!servoFound) return;
    logf("setMode POSITION: %s\n", servo.setMode(ServoMode::POSITION) ? "OK" : "FAIL");
    FeetechST3020::Profile p;
    p.statusReturnLevel = 1;
    p.returnDelayUnits  = 0;
    p.torqueOnAfterInit = true;
    logf("init: %s\n", servo.init(p) ? "OK" : "FAIL");
    servo.moveTime(SWING_CENTER, SWING_MOVE_MS, SWING_SPEED);
}

static void printServoState(uint16_t goal) {
    uint16_t pos = 0;
    uint8_t  temp = 0;
    float    amps = 0;
    bool okPos  = servo.readPresentPosition(pos);
    bool okTemp = servo.readCurrentTemperature(temp);
    bool okAmp  = servo.readCurrentCurrent(amps);
    logf("Soll %4u  Ist %s%4u  %u C  %.2f A%s\n", goal,
         okPos ? "" : "?", pos, temp, amps,
         (okPos && okTemp && okAmp) ? "" : "  (Lesefehler)");
}

// ── Arduino ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    logf("\nPowerActor2 Step1_ServoSweep\n");
    setUpWifi();
    setUpOTA();
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    initServo(true);
}

void loop() {
    ArduinoOTA.handle();

    static uint32_t tNext  = 3000;
    static uint32_t tPrint = 0;
    static bool     right  = false;
    static uint16_t goal   = SWING_CENTER;

    if (telnetServer.hasClient()) {          // neuer Zuschauer: Diagnose neu
        if (telnet) telnet.stop();
        telnet = telnetServer.available();
        logf("PowerActor2 Step1_ServoSweep - Telnet verbunden\n");
        initServo(true);
        tNext = millis() + 1000;
    }

    if (!servoFound) {
        if (millis() > tNext) {             // alle 5 s erneut suchen
            initServo(false);
            tNext = millis() + 5000;
        }
        return;
    }
    if (millis() > tNext) {
        right = !right;
        goal  = right ? SWING_RIGHT : SWING_LEFT;
        servo.moveTime(goal, SWING_MOVE_MS, SWING_SPEED);
        tNext = millis() + SWING_PERIOD_MS;
    }
    if (millis() > tPrint) {
        printServoState(goal);
        tPrint = millis() + 1000;
    }
}
