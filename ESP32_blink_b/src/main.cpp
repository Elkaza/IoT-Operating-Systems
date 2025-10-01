#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// --- Board pins (change PIN_NEOPIX to 38 if 48 doesn't change the LED) ---
static const uint8_t PIN_BUTTON = 0;    // BOOT button (active LOW)
static const uint8_t PIN_NEOPIX = 38;   // Onboard WS2812 (try 38 if needed)
static const uint8_t NUM_PIXELS = 1;

Adafruit_NeoPixel strip(NUM_PIXELS, PIN_NEOPIX, NEO_GRB + NEO_KHZ800);

// Debounce state
static bool lastBtnReading   = HIGH;  // using INPUT_PULLUP
static bool stableLast       = HIGH;
static uint32_t lastDebounce = 0;
static const uint16_t DEBOUNCE_MS = 30;

// 0=RED, 1=GREEN, 2=BLUE
static uint8_t colorIdx = 0;

static void showColor(uint8_t idx) {
  uint32_t c = 0;
  switch (idx % 3) {
    case 0: c = strip.Color(255, 0, 0); break; // red
    case 1: c = strip.Color(0, 255, 0); break; // green
    case 2: c = strip.Color(0, 0, 255); break; // blue
  }
  strip.setPixelColor(0, c);
  strip.show();
}

static bool buttonPressedOnce() {
  bool reading = digitalRead(PIN_BUTTON);
  uint32_t now = millis();

  if (reading != lastBtnReading) {
    lastDebounce = now;  // edge: restart debounce
  }

  bool pressedEdge = false;
  if ((now - lastDebounce) > DEBOUNCE_MS) {
    if (stableLast == HIGH && reading == LOW) {
      pressedEdge = true;      // detect HIGH -> LOW
    }
    stableLast = reading;
  }

  lastBtnReading = reading;
  return pressedEdge;
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 1500) { delay(10); }  // brief wait for USB-CDC

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(32);
  colorIdx = 0;                // start RED at boot
  showColor(colorIdx);

  Serial.println("Boot: RGB=RED. Send 'n' to cycle; other chars echo. Press BOOT for greeting.");
  Serial.print("Using PIN_NEOPIX = ");
  Serial.println(PIN_NEOPIX);
}

void loop() {
  // 1) Button prints once per press
  if (buttonPressedOnce()) {
    Serial.println("Hello from io25m25!");
  }

  // 2) Serial handling
  while (Serial.available() > 0) {
    int b = Serial.read();
    if (b == '\r' || b == '\n') continue;   // ignore Enter endings
    if (b == 'n' || b == 'N') {
      colorIdx = (uint8_t)((colorIdx + 1) % 3);
      showColor(colorIdx);
      // feedback so you SEE it's handled
      Serial.print("n received -> LED ");
      Serial.println(colorIdx == 0 ? "RED" : colorIdx == 1 ? "GREEN" : "BLUE");
    } else {
      Serial.write((char)b);                 // echo everything else
    }
  }

  delay(1);
}
