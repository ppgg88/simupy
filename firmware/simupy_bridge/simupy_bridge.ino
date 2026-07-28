// SimuPy bridge: a board's pins over USB. Protocol: docs/guide/hardware.rst.

#define SIMUPY_BRIDGE_VERSION 1

#if defined(ESP32)
  #define BOARD_NAME "esp32"
  #define ADC_BITS 12
  #define PWM_BITS 12
  #define DEFAULT_BAUD 115200
  // The 2.x ESP32 core has no analogWrite; LEDC is the portable route.
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    #define HAS_ANALOG_WRITE 1
  #else
    #define HAS_ANALOG_WRITE 0
    #define LEDC_BASE_FREQ 5000
    static int8_t g_ledcChannel[40];
    static uint8_t g_ledcNext = 0;
  #endif
#elif defined(ESP8266)
  #define BOARD_NAME "esp8266"
  #define ADC_BITS 10
  #define PWM_BITS 10
  #define DEFAULT_BAUD 115200
  #define HAS_ANALOG_WRITE 1
#elif defined(ARDUINO_ARCH_SAMD)
  #define BOARD_NAME "samd"
  #define ADC_BITS 12
  #define PWM_BITS 12
  #define DEFAULT_BAUD 115200
  #define HAS_ANALOG_WRITE 1
#elif defined(ARDUINO_ARCH_RP2040)
  #define BOARD_NAME "rp2040"
  #define ADC_BITS 12
  #define PWM_BITS 12
  #define DEFAULT_BAUD 115200
  #define HAS_ANALOG_WRITE 1
#else
  #define BOARD_NAME "avr"
  #define ADC_BITS 10
  #define PWM_BITS 8
  #define DEFAULT_BAUD 115200
  #define HAS_ANALOG_WRITE 1
#endif

static const uint16_t ADC_MAX = (1u << ADC_BITS) - 1u;
static const uint16_t PWM_MAX = (1u << PWM_BITS) - 1u;

/// Prepares `pin` for PWM. A no-op where analogWrite handles it itself.
static void pwmBegin(uint8_t pin) {
#if defined(ESP32) && !HAS_ANALOG_WRITE
  if (pin < sizeof(g_ledcChannel) && g_ledcChannel[pin] < 0) {
    g_ledcChannel[pin] = g_ledcNext++;
    ledcSetup(g_ledcChannel[pin], LEDC_BASE_FREQ, PWM_BITS);
    ledcAttachPin(pin, g_ledcChannel[pin]);
  }
#else
  pinMode(pin, OUTPUT);
#endif
}

static void pwmWrite(uint8_t pin, uint16_t duty) {
#if defined(ESP32) && !HAS_ANALOG_WRITE
  if (pin < sizeof(g_ledcChannel) && g_ledcChannel[pin] >= 0)
    ledcWrite(g_ledcChannel[pin], duty);
#else
  analogWrite(pin, duty);
#endif
}

/// Raises ADC and PWM resolution, so the banner's bits are the ones used.
static void resolutionBegin() {
#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_RP2040)
  analogReadResolution(ADC_BITS);
  analogWriteResolution(PWM_BITS);
#elif defined(ESP32)
  analogReadResolution(ADC_BITS);
  #if HAS_ANALOG_WRITE
  analogWriteResolution(PWM_BITS);
  #endif
#endif
}

// Sixteen: more than one USB link can carry, small enough for an Uno.
#define MAX_STREAM_PINS 16

struct StreamPin {
  uint8_t pin;
  bool analog;
};

static StreamPin g_stream[MAX_STREAM_PINS];
static uint8_t g_streamCount = 0;
static uint16_t g_generation = 0;
static uint16_t g_rateHz = 200;
static uint32_t g_periodUs = 5000;
static uint32_t g_nextDue = 0;

#define LINE_MAX 96
static char g_line[LINE_MAX];
static uint8_t g_lineLen = 0;

static void fail(const char* message) {
  Serial.print('!');
  Serial.println(message);
}

/// "A3" -> the analog input's digital pin; "D7" or "7" -> that pin.
static bool parsePin(const char* token, uint8_t* pin, bool* analog) {
  if (!token || !*token) return false;

  char kind = token[0];
  const char* digits = token;
  *analog = false;

  if (kind == 'A' || kind == 'a') {
    *analog = true;
    digits = token + 1;
  } else if (kind == 'D' || kind == 'd') {
    digits = token + 1;
  }
  if (!*digits) return false;

  long index = 0;
  for (const char* c = digits; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    index = index * 10 + (*c - '0');
    if (index > 255) return false;
  }

  if (*analog) {
#if defined(analogInputToDigitalPin)
    int mapped = analogInputToDigitalPin(index);
    *pin = (mapped >= 0) ? (uint8_t)mapped : (uint8_t)(A0 + index);
#else
    *pin = (uint8_t)(A0 + index);
#endif
  } else {
    *pin = (uint8_t)index;
  }
  return true;
}

static char* nextToken(char** cursor) {
  char* s = *cursor;
  while (*s == ' ' || *s == '\t') ++s;
  if (!*s) {
    *cursor = s;
    return nullptr;
  }
  char* start = s;
  while (*s && *s != ' ' && *s != '\t') ++s;
  if (*s) *s++ = '\0';
  *cursor = s;
  return start;
}

static void identify() {
  Serial.print(F("#simupy "));
  Serial.print(SIMUPY_BRIDGE_VERSION);
  Serial.print(' ');
  Serial.print(F(BOARD_NAME));
  Serial.print(' ');
  Serial.print(ADC_BITS);
  Serial.print(' ');
  Serial.print(PWM_BITS);
  Serial.print(' ');
  Serial.print(NUM_ANALOG_INPUTS);
  Serial.print(' ');
  Serial.println(NUM_DIGITAL_PINS);
}

static void configureStream(char* cursor) {
  uint8_t count = 0;
  StreamPin pins[MAX_STREAM_PINS];

  for (char* token = nextToken(&cursor); token; token = nextToken(&cursor)) {
    if (count >= MAX_STREAM_PINS) {
      fail("too many pins");
      return;
    }
    uint8_t pin;
    bool analog;
    if (!parsePin(token, &pin, &analog)) {
      fail("bad pin");
      return;
    }
    pins[count].pin = pin;
    pins[count].analog = analog;
    ++count;
  }

  // Applied once the whole list parses, so a typo leaves the old one running.
  for (uint8_t i = 0; i < count; ++i) {
    g_stream[i] = pins[i];
    if (!pins[i].analog) pinMode(pins[i].pin, INPUT);
  }
  g_streamCount = count;
  ++g_generation;
  g_nextDue = micros();

  // The host must not assume the generation counter starts where its own does.
  Serial.print(F("#C "));
  Serial.println(g_generation);
}

static void setRate(char* cursor) {
  char* token = nextToken(&cursor);
  if (!token) {
    fail("R needs a rate");
    return;
  }
  long hz = atol(token);
  if (hz < 1) hz = 1;
  if (hz > 2000) hz = 2000;
  g_rateHz = (uint16_t)hz;
  g_periodUs = 1000000UL / (uint32_t)hz;
}

static void setMode(char* cursor) {
  char* pinToken = nextToken(&cursor);
  char* modeToken = nextToken(&cursor);
  uint8_t pin;
  bool analog;
  if (!pinToken || !modeToken || !parsePin(pinToken, &pin, &analog)) {
    fail("M needs a pin and a mode");
    return;
  }
  switch (modeToken[0]) {
    case 'O': case 'o': pinMode(pin, OUTPUT); break;
    case 'U': case 'u': pinMode(pin, INPUT_PULLUP); break;
    default:            pinMode(pin, INPUT); break;
  }
}

static void writeDigital(char* cursor) {
  char* pinToken = nextToken(&cursor);
  char* valueToken = nextToken(&cursor);
  uint8_t pin;
  bool analog;
  if (!pinToken || !valueToken || !parsePin(pinToken, &pin, &analog)) {
    fail("W needs a pin and a level");
    return;
  }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, atoi(valueToken) ? HIGH : LOW);
}

static void writePwm(char* cursor) {
  char* pinToken = nextToken(&cursor);
  char* valueToken = nextToken(&cursor);
  uint8_t pin;
  bool analog;
  if (!pinToken || !valueToken || !parsePin(pinToken, &pin, &analog)) {
    fail("P needs a pin and a duty");
    return;
  }

  // Duty is 0..1, so the host never has to know this board's PWM width.
  double duty = atof(valueToken);
  if (duty < 0.0) duty = 0.0;
  if (duty > 1.0) duty = 1.0;

  pwmBegin(pin);
  pwmWrite(pin, (uint16_t)(duty * PWM_MAX + 0.5));
}

static void dispatch(char* line) {
  char* cursor = line;
  char* command = nextToken(&cursor);
  if (!command) return;

  switch (command[0]) {
    case '?': identify();            break;
    case 'C': case 'c': configureStream(cursor); break;
    case 'R': case 'r': setRate(cursor);         break;
    case 'M': case 'm': setMode(cursor);         break;
    case 'W': case 'w': writeDigital(cursor);    break;
    case 'P': case 'p': writePwm(cursor);        break;
    default:  fail("unknown command");           break;
  }
}

static void emitSample() {
  Serial.print(g_generation);
  Serial.print(' ');
  Serial.print(micros());
  for (uint8_t i = 0; i < g_streamCount; ++i) {
    Serial.print(' ');
    Serial.print(g_stream[i].analog ? analogRead(g_stream[i].pin)
                                    : (int)digitalRead(g_stream[i].pin));
  }
  Serial.println();
}

void setup() {
#if defined(ESP32) && !HAS_ANALOG_WRITE
  for (uint8_t i = 0; i < sizeof(g_ledcChannel); ++i) g_ledcChannel[i] = -1;
#endif

  Serial.begin(DEFAULT_BAUD);
#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_RP2040) || \
    defined(ARDUINO_AVR_LEONARDO) || defined(ARDUINO_AVR_MICRO)
  // Native-USB boards enumerate after setup(), so the wait is bounded.
  const unsigned long deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {}
#endif

  resolutionBegin();
  g_nextDue = micros();
  identify();
}

void loop() {
  // Commands first: a write takes effect on this sample, not the next.
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      g_line[g_lineLen] = '\0';
      if (g_lineLen) dispatch(g_line);
      g_lineLen = 0;
      continue;
    }
    if (g_lineLen < LINE_MAX - 1) {
      g_line[g_lineLen++] = c;
    } else {
      // Overlong line: drop it rather than corrupt the next one.
      g_lineLen = 0;
      fail("line too long");
    }
  }

  if (g_streamCount == 0) return;

  // Signed difference, so the 71-minute micros() rollover does not stall it.
  const uint32_t now = micros();
  if ((int32_t)(now - g_nextDue) < 0) return;

  g_nextDue += g_periodUs;
  // Resynchronise rather than accumulate a backlog that never clears.
  if ((int32_t)(now - g_nextDue) > (int32_t)g_periodUs) g_nextDue = now + g_periodUs;

  emitSample();
}
