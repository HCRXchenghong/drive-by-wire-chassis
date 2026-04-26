#include <SoftwareSerial.h>

// Arduino Nano wiring summary
// D4  -> E22 M0
// D5  -> E22 M1
// D10 <- E22 TXD
// D11 -> divider / level shifter -> E22 RXD
// D12 <- E22 AUX (optional monitor only)
// A0  <- left joystick X
// A1  <- left joystick Y
// A2  <- right joystick X
// A3  <- right joystick Y
// 5V  -> joystick VCC
// GND -> joystick GND + E22 GND + external supply GND
//
// If your E22 is a 22dBm module, VCC may use a stable 5V rail.
// If your E22 is a 30dBm module, use an external high-current 5V supply.
// Never connect Nano 5V TX directly to E22 RXD.

namespace
{
constexpr uint8_t PIN_E22_RX = 10;
constexpr uint8_t PIN_E22_TX = 11;
constexpr uint8_t PIN_E22_M0 = 4;
constexpr uint8_t PIN_E22_M1 = 5;
constexpr uint8_t PIN_E22_AUX = 12;
constexpr uint8_t PIN_STATUS_LED = LED_BUILTIN;

constexpr uint8_t PIN_LEFT_X = A0;
constexpr uint8_t PIN_LEFT_Y = A1;
constexpr uint8_t PIN_RIGHT_X = A2;
constexpr uint8_t PIN_RIGHT_Y = A3;

constexpr long USB_BAUD = 115200L;
constexpr long E22_BAUD = 9600L;
constexpr uint32_t FRAME_PERIOD_MS = 20U;
constexpr int16_t AXIS_LIMIT = 1000;
constexpr int16_t DEAD_BAND = 35;
constexpr uint8_t CALIBRATION_SAMPLES = 64;

enum RadioMode
{
  RADIO_MODE_TRANSPARENT = 0,
  RADIO_MODE_WOR = 1,
  RADIO_MODE_CONFIG = 2,
  RADIO_MODE_SLEEP = 3,
};

struct AxisCalibration
{
  int16_t center;
  int16_t minValue;
  int16_t maxValue;
};

struct RadioFrame
{
  uint32_t sequence;
  int16_t throttle;
  int16_t steer;
  int16_t auxX;
  int16_t auxY;
  uint16_t buttons;
};

SoftwareSerial radioSerial(PIN_E22_RX, PIN_E22_TX);

AxisCalibration leftXCal = {512, 0, 1023};
AxisCalibration leftYCal = {512, 0, 1023};
AxisCalibration rightXCal = {512, 0, 1023};
AxisCalibration rightYCal = {512, 0, 1023};

RadioFrame lastFrame = {0U, 0, 0, 0, 0, 0U};

uint32_t lastFrameTickMs = 0U;
uint32_t lastDebugTickMs = 0U;

int16_t clampInt16(int32_t value, int16_t minValue, int16_t maxValue)
{
  if (value < minValue)
  {
    return minValue;
  }

  if (value > maxValue)
  {
    return maxValue;
  }

  return static_cast<int16_t>(value);
}

void setRadioMode(RadioMode mode)
{
  switch (mode)
  {
    case RADIO_MODE_TRANSPARENT:
      digitalWrite(PIN_E22_M1, LOW);
      digitalWrite(PIN_E22_M0, LOW);
      break;

    case RADIO_MODE_WOR:
      digitalWrite(PIN_E22_M1, LOW);
      digitalWrite(PIN_E22_M0, HIGH);
      break;

    case RADIO_MODE_CONFIG:
      digitalWrite(PIN_E22_M1, HIGH);
      digitalWrite(PIN_E22_M0, LOW);
      break;

    case RADIO_MODE_SLEEP:
    default:
      digitalWrite(PIN_E22_M1, HIGH);
      digitalWrite(PIN_E22_M0, HIGH);
      break;
  }

  delay(20);
}

int16_t normalizeAxis(uint8_t pin, const AxisCalibration &calibration, bool invertAxis)
{
  int16_t raw = analogRead(pin);
  int32_t delta = static_cast<int32_t>(raw) - calibration.center;
  int32_t span = 0;
  int32_t scaled = 0;

  if (abs(delta) <= DEAD_BAND)
  {
    return 0;
  }

  if (delta > 0)
  {
    span = calibration.maxValue - calibration.center;
  }
  else
  {
    span = calibration.center - calibration.minValue;
  }

  if (span < 1)
  {
    span = 1;
  }

  scaled = (delta * AXIS_LIMIT) / span;
  scaled = clampInt16(scaled, -AXIS_LIMIT, AXIS_LIMIT);

  if (invertAxis)
  {
    scaled = -scaled;
  }

  return static_cast<int16_t>(scaled);
}

uint8_t computeXorChecksum(const char *text)
{
  uint8_t checksum = 0U;

  while (*text != '\0')
  {
    checksum ^= static_cast<uint8_t>(*text);
    ++text;
  }

  return checksum;
}

void calibrateAxis(uint8_t pin, AxisCalibration &calibration)
{
  int32_t sum = 0;

  for (uint8_t index = 0; index < CALIBRATION_SAMPLES; ++index)
  {
    sum += analogRead(pin);
    delay(4);
  }

  calibration.center = static_cast<int16_t>(sum / CALIBRATION_SAMPLES);
}

void calibrateAllAxes()
{
  calibrateAxis(PIN_LEFT_X, leftXCal);
  calibrateAxis(PIN_LEFT_Y, leftYCal);
  calibrateAxis(PIN_RIGHT_X, rightXCal);
  calibrateAxis(PIN_RIGHT_Y, rightYCal);
}

RadioFrame sampleFrame()
{
  RadioFrame frame = {};

  frame.sequence = lastFrame.sequence + 1U;
  frame.throttle = normalizeAxis(PIN_LEFT_Y, leftYCal, true);
  frame.steer = normalizeAxis(PIN_RIGHT_X, rightXCal, false);
  frame.auxX = normalizeAxis(PIN_LEFT_X, leftXCal, false);
  frame.auxY = normalizeAxis(PIN_RIGHT_Y, rightYCal, true);
  frame.buttons = 0U;
  return frame;
}

void sendFrame(const RadioFrame &frame)
{
  char body[64];
  char packet[72];
  uint8_t checksum = 0U;

  snprintf(body,
           sizeof(body),
           "RC,%lu,%d,%d,%d,%d,%u",
           static_cast<unsigned long>(frame.sequence),
           static_cast<int>(frame.throttle),
           static_cast<int>(frame.steer),
           static_cast<int>(frame.auxX),
           static_cast<int>(frame.auxY),
           static_cast<unsigned int>(frame.buttons));

  checksum = computeXorChecksum(body);
  snprintf(packet, sizeof(packet), "$%s*%02X\r\n", body, checksum);
  radioSerial.print(packet);
}

void printDebug(const RadioFrame &frame)
{
  Serial.print(F("seq="));
  Serial.print(frame.sequence);
  Serial.print(F(" aux="));
  Serial.print(digitalRead(PIN_E22_AUX));
  Serial.print(F(" throttle="));
  Serial.print(frame.throttle);
  Serial.print(F(" steer="));
  Serial.print(frame.steer);
  Serial.print(F(" auxX="));
  Serial.print(frame.auxX);
  Serial.print(F(" auxY="));
  Serial.println(frame.auxY);
}
}  // namespace

void setup()
{
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  pinMode(PIN_E22_M0, OUTPUT);
  pinMode(PIN_E22_M1, OUTPUT);
  pinMode(PIN_E22_AUX, INPUT);

  analogReference(DEFAULT);

  Serial.begin(USB_BAUD);
  radioSerial.begin(E22_BAUD);
  setRadioMode(RADIO_MODE_TRANSPARENT);

  delay(150);
  calibrateAllAxes();

  Serial.println(F("Arduino Nano E22-T LoRa hand controller ready."));
  Serial.println(F("Radio mode is forced to transparent mode: M1=0, M0=0."));
  Serial.println(F("Keep sticks centered during power-up for best calibration."));
}

void loop()
{
  uint32_t now = millis();

  if ((now - lastFrameTickMs) >= FRAME_PERIOD_MS)
  {
    lastFrameTickMs = now;
    lastFrame = sampleFrame();
    sendFrame(lastFrame);
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
  }

  if ((now - lastDebugTickMs) >= 200U)
  {
    lastDebugTickMs = now;
    printDebug(lastFrame);
  }
}
