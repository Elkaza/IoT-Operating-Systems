#include <Arduino.h>
#include <DHTesp.h>
#include <cstring>

/*
  FreeRTOS Homework – ESP32-S3 DevKitC-1
  Tasks on core 1:
    - heartbeatTask: toggle pin every 200 ms
    - fastbeatTask:  toggle pin every 1 ms (highest priority)
    - dhtTask:       periodically read DHT22 and send values via queue
    - serialTask:    ONLY task that prints to Serial; prints sensor values and user id
    - buttonTask:    on BOOT press, send user id once to serialTask
  Wiring:
    DHT22: VCC->3V3, GND->GND, DATA->GPIO9 (+ 10k pull-up if bare 4-pin sensor)
    Heartbeat LED: GPIO38 (use board LED or external LED+330Ω to GND)
    Fastbeat: GPIO5 (optional LED+330Ω to GND or logic analyzer)
    Button: BOOT (GPIO0, active LOW)
*/

//// ===== USER CONFIG =====
#define IO_USER_ID       "io25m25"   // 
#define DHT_PIN          9            // Yellow DATA wire on GPIO9
#define HEARTBEAT_PIN    38           // LED pin
#define FASTBEAT_PIN     5            // 1 ms toggle pin
#define BUTTON_PIN       0            // BOOT (active LOW)

// DHT22 minimum period ~2000 ms (use 2500 ms to be safe)
#define DHT_PERIOD_MS    2500

// Required output format (adapt only if your HW2 used different text)
#define SERIAL_FORMAT    "T=%.1fC,H=%.1f%%\r\n"

//// ===== CORE / RTOS =====
static const BaseType_t RUN_CORE = 1; // pin all tasks to core 1

enum MsgType : uint8_t { MSG_SENSOR, MSG_USERID };

struct SerialMsg {
  MsgType type;
  float   temperature;  // valid if MSG_SENSOR
  float   humidity;     // valid if MSG_SENSOR
  char    userId[16];   // valid if MSG_USERID
};

QueueHandle_t qSerial = nullptr;

// (optional) task handles
TaskHandle_t thHeartbeat = nullptr;
TaskHandle_t thFastbeat  = nullptr;
TaskHandle_t thDHT       = nullptr;
TaskHandle_t thSerial    = nullptr;
TaskHandle_t thButton    = nullptr;

DHTesp dht;

// forward decls
void heartbeatTask(void*);
void fastbeatTask(void*);
void dhtTask(void*);
void serialTask(void*);
void buttonTask(void*);

void setup() {
  // Per requirement: ONLY serialTask will access Serial

  pinMode(HEARTBEAT_PIN, OUTPUT);
  digitalWrite(HEARTBEAT_PIN, LOW);

  pinMode(FASTBEAT_PIN, OUTPUT);
  digitalWrite(FASTBEAT_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP); // BOOT: HIGH=released, LOW=pressed

  // queue for messages to serial task
  qSerial = xQueueCreate(12, sizeof(SerialMsg));
  configASSERT(qSerial);

  // DHT line: help with internal pull-up, small settle, then init
  pinMode(DHT_PIN, INPUT_PULLUP);
  vTaskDelay(pdMS_TO_TICKS(100));
  dht.setup(DHT_PIN, DHTesp::DHT22);

  // create tasks — all pinned to core 1
  xTaskCreatePinnedToCore(heartbeatTask, "heartbeat", 2048, nullptr, 2, &thHeartbeat, RUN_CORE);
  xTaskCreatePinnedToCore(fastbeatTask,  "fastbeat",  2048, nullptr, configMAX_PRIORITIES - 1, &thFastbeat, RUN_CORE);
  xTaskCreatePinnedToCore(dhtTask,       "dht",       4096, nullptr, 3, &thDHT, RUN_CORE);
  xTaskCreatePinnedToCore(serialTask,    "serial",    4096, nullptr, 4, &thSerial, RUN_CORE);
  xTaskCreatePinnedToCore(buttonTask,    "button",    2048, nullptr, 3, &thButton, RUN_CORE);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); } // RTOS-driven

//// ===== TASKS =====

// heartbeat: 200 ms blink
void heartbeatTask(void*) {
  TickType_t last = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(200);
  for (;;) {
    digitalWrite(HEARTBEAT_PIN, !digitalRead(HEARTBEAT_PIN));
    vTaskDelayUntil(&last, period);
  }
}

// fastbeat: 1 ms at highest priority
void fastbeatTask(void*) {
  TickType_t last = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1);
  for (;;) {
    digitalWrite(FASTBEAT_PIN, !digitalRead(FASTBEAT_PIN));
    vTaskDelayUntil(&last, period);
  }
}

// DHT22 periodic read -> queue message to serial task
void dhtTask(void*) {
  for (;;) {
    TempAndHumidity th = dht.getTempAndHumidity();
    if (!isnan(th.temperature) && !isnan(th.humidity)) {
      SerialMsg m{};
      m.type = MSG_SENSOR;
      m.temperature = th.temperature;
      m.humidity = th.humidity;
      xQueueSend(qSerial, &m, portMAX_DELAY);
    }
    vTaskDelay(pdMS_TO_TICKS(DHT_PERIOD_MS));
  }
}

// ONLY task that accesses Serial
void serialTask(void*) {
  Serial.begin(115200);
  // optional: brief wait for USB-CDC to enumerate
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) { vTaskDelay(pdMS_TO_TICKS(50)); }

  for (;;) {
    SerialMsg m;
    if (xQueueReceive(qSerial, &m, portMAX_DELAY) == pdTRUE) {
      if (m.type == MSG_SENSOR) {
        Serial.printf(SERIAL_FORMAT, m.temperature, m.humidity);
      } else if (m.type == MSG_USERID) {
        Serial.println(m.userId);
      }
    }
  }
}

// BOOT button (debounced) -> send user id once per press
void buttonTask(void*) {
  bool lastRead = HIGH, lastStable = HIGH;   // HIGH=released
  TickType_t lastChange = xTaskGetTickCount();
  const TickType_t debounce = pdMS_TO_TICKS(25);

  for (;;) {
    bool now = digitalRead(BUTTON_PIN); // HIGH=released, LOW=pressed
    if (now != lastRead) { lastRead = now; lastChange = xTaskGetTickCount(); }
    if (xTaskGetTickCount() - lastChange >= debounce) {
      if (now != lastStable) {
        lastStable = now;
        if (lastStable == LOW) {
          SerialMsg m{}; m.type = MSG_USERID;
          // safe copy of your id
          strncpy(m.userId, IO_USER_ID, sizeof(m.userId) - 1);
          m.userId[sizeof(m.userId) - 1] = '\0';
          xQueueSend(qSerial, &m, portMAX_DELAY);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
