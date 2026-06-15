// =============================================================================
//  LaserMarkerI2CSlave - standalone I2C firmware for LaserMarker 6_3 hardware
//
//  This sketch is intentionally independent from the CatFind project: it uses no
//  CatFind headers, no UDP, no WiFi and no CatFind records. It exposes the same
//  LaserMarker functions (mainLaser, subLaser, Aux, NeoPixel RGB, state query)
//  to any external controller through I2C.
//
//  Target board: ESP32-C3 Super Mini
//  Default I2C slave address: 0x2A (7-bit)
//  API documentation: ../README.md
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

// ----------------------------- Hardware mapping -----------------------------
// Kept local on purpose: this sketch must remain usable without CatFind files.
static constexpr uint8_t MAIN_LASER_PIN = 3;   // GPIO3  -> TTL laser module, HIGH = on
static constexpr uint8_t SUB_LASER_PIN  = 21;  // GPIO21 -> BS170 MOSFET laser, HIGH = on
static constexpr uint8_t AUX_PIN        = 7;   // GPIO7  -> auxiliary output, HIGH = on
static constexpr uint8_t PIXEL_PIN      = 10;  // GPIO10 -> 1x WS2812 NeoPixel
static constexpr uint8_t I2C_SDA_PIN    = 0;   // GPIO0  -> I2C SDA connector pin
static constexpr uint8_t I2C_SCL_PIN    = 1;   // GPIO1  -> I2C SCL connector pin

// ------------------------------- I2C protocol -------------------------------
static constexpr uint8_t I2C_ADDRESS = 0x2A;       // 7-bit slave address
static constexpr uint8_t PROTOCOL_VERSION = 0x01;
static constexpr uint16_t MAIN_LASER_BLINK_INTERVAL_MS = 500;
static constexpr uint8_t MAIN_LASER_INDICATOR_LEVEL = 25;
static constexpr uint8_t SUB_LASER_INDICATOR_LEVEL = 255;

// Command codes deliberately match the original CatFind UDP API.
static constexpr uint8_t CMD_MAIN_LASER  = 11;     // info: 0=off, non-zero=on
static constexpr uint8_t CMD_SUB_LASER   = 12;     // info: 0=off, non-zero=on
static constexpr uint8_t CMD_AUX         = 13;     // info: 0=off, non-zero=on
static constexpr uint8_t CMD_PIXEL_COLOR = 14;     // info: 0x00RRGGBB
static constexpr uint8_t CMD_MARKER_STATE = 15;    // info ignored; only updates request metadata
static constexpr uint8_t CMD_ALL_OFF     = 16;     // standalone extension: all outputs off

static constexpr uint8_t STATUS_SIZE = 16;
static constexpr uint8_t EVENT_BOOT = 1;
static constexpr uint8_t EVENT_COMMAND_OK = 2;
static constexpr uint8_t EVENT_BAD_LENGTH = 3;
static constexpr uint8_t EVENT_UNKNOWN_COMMAND = 4;

struct MarkerState {
  uint8_t mainLaser;
  uint8_t subLaser;
  uint8_t aux;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct PendingCommand {
  bool available;
  uint8_t cmd;
  int32_t info;
};

Adafruit_NeoPixel pixels(1, PIXEL_PIN, NEO_RGB + NEO_KHZ800);
MarkerState state = {0, 0, 0, 0, 0, 0};
PendingCommand pendingCommand = {false, 0, 0};
uint8_t renderedPixelR = 255;
uint8_t renderedPixelG = 255;
uint8_t renderedPixelB = 255;

volatile uint32_t commandCounter = 0;
volatile uint32_t errorCounter = 0;
volatile uint8_t lastCommand = 0;
volatile uint8_t lastEvent = EVENT_BOOT;
volatile uint8_t lastError = 0;
uint32_t lastStatusRequestMs = 0;

portMUX_TYPE i2cMux = portMUX_INITIALIZER_UNLOCKED;

static int32_t readInt32LittleEndian(const uint8_t *bytes) {
  uint32_t value = static_cast<uint32_t>(bytes[0]) |
                   (static_cast<uint32_t>(bytes[1]) << 8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) |
                   (static_cast<uint32_t>(bytes[3]) << 24);
  return static_cast<int32_t>(value);
}

static void writeUint32LittleEndian(uint8_t *bytes, uint32_t value) {
  bytes[0] = value & 0xFF;
  bytes[1] = (value >> 8) & 0xFF;
  bytes[2] = (value >> 16) & 0xFF;
  bytes[3] = (value >> 24) & 0xFF;
}

static void applyOutputs() {
  digitalWrite(MAIN_LASER_PIN, state.mainLaser ? HIGH : LOW);
  digitalWrite(SUB_LASER_PIN, state.subLaser ? HIGH : LOW);
  digitalWrite(AUX_PIN, state.aux ? HIGH : LOW);
}

static bool commandedPixelIsOff() {
  return state.r == 0 && state.g == 0 && state.b == 0;
}

static void renderPixel(uint8_t r, uint8_t g, uint8_t b) {
  if (r == renderedPixelR && g == renderedPixelG && b == renderedPixelB) {
    return;
  }

  renderedPixelR = r;
  renderedPixelG = g;
  renderedPixelB = b;
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

static void updatePixelOutput() {
  // Priority:
  // 1. Explicit pixel command: any non-black commanded color is shown exactly.
  // 2. mainLaser indicator: if no pixel color is active, blink weak red.
  // 3. subLaser indicator: if no pixel color and no mainLaser are active, show full white.
  if (!commandedPixelIsOff()) {
    renderPixel(state.r, state.g, state.b);
    return;
  }

  if (state.mainLaser) {
    const bool blinkOn = (millis() / MAIN_LASER_BLINK_INTERVAL_MS) % 2 == 0;
    renderPixel(blinkOn ? MAIN_LASER_INDICATOR_LEVEL : 0, 0, 0);
    return;
  }

  if (state.subLaser) {
    renderPixel(SUB_LASER_INDICATOR_LEVEL, SUB_LASER_INDICATOR_LEVEL, SUB_LASER_INDICATOR_LEVEL);
    return;
  }

  renderPixel(0, 0, 0);
}

static void recordEvent(uint8_t event, uint8_t command, bool isError) {
  portENTER_CRITICAL(&i2cMux);
  lastEvent = event;
  lastCommand = command;
  if (isError) {
    lastError = event;
    errorCounter++;
  } else {
    lastError = 0;
    commandCounter++;
  }
  portEXIT_CRITICAL(&i2cMux);
}

static bool executeCommand(uint8_t cmd, int32_t info) {
  switch (cmd) {
    case CMD_MAIN_LASER:
      state.mainLaser = info ? 1 : 0;
      break;
    case CMD_SUB_LASER:
      state.subLaser = info ? 1 : 0;
      break;
    case CMD_AUX:
      state.aux = info ? 1 : 0;
      break;
    case CMD_PIXEL_COLOR:
      state.r = (static_cast<uint32_t>(info) >> 16) & 0xFF;
      state.g = (static_cast<uint32_t>(info) >> 8) & 0xFF;
      state.b = static_cast<uint32_t>(info) & 0xFF;
      break;
    case CMD_MARKER_STATE:
      lastStatusRequestMs = millis();
      break;
    case CMD_ALL_OFF:
      state = {0, 0, 0, 0, 0, 0};
      break;
    default:
      return false;
  }

  applyOutputs();
  return true;
}

static void receiveI2C(int byteCount) {
  uint8_t buffer[5];
  uint8_t index = 0;

  while (Wire.available() && index < sizeof(buffer)) {
    buffer[index++] = Wire.read();
  }
  while (Wire.available()) {
    Wire.read();
  }

  if (byteCount != 5 || index != 5) {
    recordEvent(EVENT_BAD_LENGTH, 0, true);
    return;
  }

  portENTER_CRITICAL(&i2cMux);
  pendingCommand.cmd = buffer[0];
  pendingCommand.info = readInt32LittleEndian(&buffer[1]);
  pendingCommand.available = true;
  portEXIT_CRITICAL(&i2cMux);
}

static void requestI2C() {
  uint8_t response[STATUS_SIZE];
  uint32_t commands;
  uint32_t errors;
  uint8_t event;
  uint8_t error;

  portENTER_CRITICAL(&i2cMux);
  commands = commandCounter;
  errors = errorCounter;
  event = lastEvent;
  error = lastError;
  portEXIT_CRITICAL(&i2cMux);

  response[0] = PROTOCOL_VERSION;
  response[1] = event;
  response[2] = error;
  response[3] = state.mainLaser;
  response[4] = state.subLaser;
  response[5] = state.aux;
  response[6] = state.r;
  response[7] = state.g;
  response[8] = state.b;
  response[9] = lastCommand;
  response[10] = 0; // reserved
  response[11] = 0; // reserved
  writeUint32LittleEndian(&response[12], commands + errors);

  Wire.write(response, sizeof(response));
}

void setup() {
  Serial.begin(115200);

  pinMode(MAIN_LASER_PIN, OUTPUT);
  pinMode(SUB_LASER_PIN, OUTPUT);
  pinMode(AUX_PIN, OUTPUT);
  state = {0, 0, 0, 0, 0, 0};

  pixels.begin();
  pixels.clear();
  applyOutputs();
  updatePixelOutput();

  Wire.begin(static_cast<uint8_t>(I2C_ADDRESS), I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  Wire.onReceive(receiveI2C);
  Wire.onRequest(requestI2C);

  Serial.println("LaserMarker standalone I2C slave ready at address 0x2A");
}

void loop() {
  PendingCommand command;

  portENTER_CRITICAL(&i2cMux);
  command = pendingCommand;
  pendingCommand.available = false;
  portEXIT_CRITICAL(&i2cMux);

  if (!command.available) {
    updatePixelOutput();
    delay(1);
    return;
  }

  if (executeCommand(command.cmd, command.info)) {
    recordEvent(EVENT_COMMAND_OK, command.cmd, false);
  } else {
    recordEvent(EVENT_UNKNOWN_COMMAND, command.cmd, true);
  }

  updatePixelOutput();
}
