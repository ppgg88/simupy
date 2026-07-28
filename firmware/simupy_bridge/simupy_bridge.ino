// SimuPy bridge — exposes a board's pins to a running simulation over USB.
//
// Flash this once; the model decides at run time which pins to read and
// write. Nothing here is model-specific.
//
// Boards: AVR (Uno, Nano, Mega, Leonardo, Micro), SAMD (Zero, MKR, Nano 33
// IoT), RP2040, ESP32 (all variants, core 2.x and 3.x), ESP8266. Everything
// board-specific lives in the HAL section below; the protocol above it is
// identical everywhere.
//
// ----------------------------------------------------------------------
// Protocol — ASCII lines, '\n' terminated, host to board:
//
//   ?                 identify
//   C <pin> <pin> …   set the streamed input list, in this order. No pins
//                     stops the stream.
//   R <hz>            stream rate, 1..2000 Hz
//   M <pin> <mode>    pin mode: I input, U input with pull-up, O output
//   W <pin> <0|1>     digitalWrite
//   P <pin> <duty>    analogWrite, duty 0..1 as a decimal
//
// Board to host:
//
//   #simupy <ver> <board> <adcBits> <pwmBits> <analogPins> <digitalPins>
//   #C <gen>          a C command was accepted, under this generation
//   !<message>        something was wrong with a command
//   <gen> <micros> <v> <v> …   a sample of the configured pins
//
// The board streams; the host never has to ask. That is what keeps a control
// loop off the round-trip: a read is a lookup in the host's cache, not a
// question put to the board and waited on.
//
// `gen` counts configuration changes, and the board reports it in the `#C`
// acknowledgement rather than leaving the host to guess. A host that has just
// sent a new C can then tell which lines were already in flight under the old
// pin list and drop them, instead of pairing values with the wrong pins.
// ----------------------------------------------------------------------

#define SIMUPY_BRIDGE_VERSION 1

// ----------------------------------------------------------------------
// Board HAL
// ----------------------------------------------------------------------

#if defined(ESP32)
  #define BOARD_NAME "esp32"
  #define ADC_BITS 12
  #define PWM_BITS 12
  #define DEFAULT_BAUD 115200
  // The 2.x core has no analogWrite; LEDC is the portable way to a PWM pin.
  // The 3.x core added analogWrite and deprecated attaching channels by hand.
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
#else  // AVR and anything else Arduino-shaped
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

/// Raises the ADC and PWM resolution where the core allows it, so the bits
/// advertised in the banner are the bits actually used.
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

// ----------------------------------------------------------------------
// Streamed pin list
// ----------------------------------------------------------------------

// Sixteen is more pins than a control loop over one USB link can usefully
// carry, and small enough to live on an Uno's 2 kB of RAM.
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

// ----------------------------------------------------------------------
// Line reader
// ----------------------------------------------------------------------

#define LINE_MAX 96
static char g_line[LINE_MAX];
static uint8_t g_lineLen = 0;

static void fail(const char* message) {
  Serial.print('!');
  Serial.println(message);
}

/// "A3" -> the analog input's digital pin; "D7" or "7" -> that pin.
/// Returns false when the token is not a pin at all.
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

// ----------------------------------------------------------------------
// Commands
// ----------------------------------------------------------------------

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

  // Applied only once the whole list has parsed, so a typo halfway through
  // leaves the previous configuration running rather than half-replacing it.
  for (uint8_t i = 0; i < count; ++i) {
    g_stream[i] = pins[i];
    // Analog inputs need no mode; digital ones are left as the host set them
    // with M, defaulting to a plain input.
    if (!pins[i].analog) pinMode(pins[i].pin, INPUT);
  }
  g_streamCount = count;
  ++g_generation;
  g_nextDue = micros();

  // Acknowledged with the number the board actually assigned. The host must
  // not assume the counter starts where its own does: a board that does not
  // reset when the port opens — an ESP32, or any board on a second run —
  // carries on from wherever it was, and a host guessing would then reject
  // every sample that followed.
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
  if (hz > 2000) hz = 2000;  // beyond this the link, not the board, is the limit
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

  // Duty arrives as 0..1 so the host never has to know this board's PWM
  // width — which differs by a factor of sixteen between an Uno and an ESP32.
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

// ----------------------------------------------------------------------

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
  // Native-USB boards enumerate after setup() starts. Waiting forever would
  // hang a board running standalone, so the wait is bounded.
  const unsigned long deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {}
#endif

  resolutionBegin();
  g_nextDue = micros();
  identify();
}

void loop() {
  // Commands first: a write should take effect on this sample, not the next.
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

  // Compared as a signed difference so the 32-bit micros() rollover, which
  // happens every 71 minutes, does not stall the stream for another 71.
  const uint32_t now = micros();
  if ((int32_t)(now - g_nextDue) < 0) return;

  g_nextDue += g_periodUs;
  // If the host asked for more than the link can carry, resynchronise rather
  // than accumulate a backlog that never clears.
  if ((int32_t)(now - g_nextDue) > (int32_t)g_periodUs) g_nextDue = now + g_periodUs;

  emitSample();
}
