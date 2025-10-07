#include <Arduino.h>
#include <NimBLEDevice.h>
#include <DHT.h>

/* ====== Hardware Pins (adjust DHT_PIN if needed) ====== */
#define DHT_PIN   4          // DHT22 data -> GPIO4
#define LED_PIN   48         // ESP32-S3 DevKitC-1 onboard LED (usually 48)

/* ====== DHT Setup ====== */
#define DHT_TYPE  DHT22
static DHT dht(DHT_PIN, DHT_TYPE);

/* ====== BLE UUIDs (random, consistent) ====== */
static NimBLEUUID UUID_SVC_ENV("6e400001-b5a3-f393-e0a9-e50e24dcca9e"); // Service: Env
static NimBLEUUID UUID_CH_TEMP("6e400002-b5a3-f393-e0a9-e50e24dcca9e"); // Char: Temperature
static NimBLEUUID UUID_CH_HUM ("6e400003-b5a3-f393-e0a9-e50e24dcca9e"); // Char: Humidity

/* ====== RTOS Objects ====== */
static TimerHandle_t measureTimer;          // 5s periodic timer
static QueueHandle_t qMeasureTrigger;       // signal DHT task to measure
static QueueHandle_t qNewReading;           // DHT task -> BLE update task

typedef struct {
  float temperatureC;
  float humidityPct;
  bool  valid;
} Reading_t;

/* ====== Globals ====== */
static volatile bool g_connected = false;
static NimBLEServer*         pServer = nullptr;
static NimBLECharacteristic* pChTemp = nullptr;
static NimBLECharacteristic* pChHum  = nullptr;

/* ====== Fwds ====== */
static void startMeasureTimer();
static void stopMeasureTimer();
static void cbMeasureTimer(TimerHandle_t);
static void taskDHT(void* arg);
static void taskBLEUpdate(void* arg);

/* ====== BLE Server Callbacks (v1.x NimBLE signatures) ====== */
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s) override {
    g_connected = true;
    startMeasureTimer();
    Serial.println("[BLE] Device connected -> 5s measurement timer STARTED");
  }
  void onDisconnect(NimBLEServer* s) override {
    Serial.println("[BLE] Device disconnected -> timer STOPPED, re-advertising");
    g_connected = false;
    stopMeasureTimer();
    NimBLEDevice::startAdvertising();
  }
};

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2000) { delay(10); } // wait up to 2s for USB-CDC

  Serial.println();
  Serial.println("=== ESP32-S3 BLE DHT22 Sensor (FreeRTOS timer-triggered) ===");

  // ---- Queues & Timer ----
  qMeasureTrigger = xQueueCreate(8, sizeof(uint8_t));
  qNewReading     = xQueueCreate(8, sizeof(Reading_t));
  measureTimer    = xTimerCreate("mTimer", pdMS_TO_TICKS(5000), pdTRUE, nullptr, cbMeasureTimer);

  // ---- DHT ----
  dht.begin();
  Serial.printf("[DHT] Initialized on GPIO %d (type DHT22)\n", DHT_PIN);

  // ---- BLE Init ----
  NimBLEDevice::init("ESP32 DHT22 (BLE)");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);         // stronger TX for testing
  NimBLEDevice::setSecurityAuth(false, false, true); // no passkey

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(UUID_SVC_ENV);

  // Temperature characteristic: READ + NOTIFY
  pChTemp = pService->createCharacteristic(
    UUID_CH_TEMP,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  // Humidity characteristic: READ + NOTIFY
  pChHum = pService->createCharacteristic(
    UUID_CH_HUM,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  // Optional user-friendly labels (Descriptor 0x2901)
  pChTemp->createDescriptor("2901")->setValue("Temperature (C)");
  pChHum ->createDescriptor("2901")->setValue("Humidity (%)");

  // Initial values
  pChTemp->setValue("NaN");
  pChHum ->setValue("NaN");

  pService->start();

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(UUID_SVC_ENV);
  pAdv->setScanResponse(true);
  pAdv->start();
  Serial.println("[BLE] Advertising started — open ST BLE Toolbox or nRF Connect");

  // ---- Tasks ----
  xTaskCreate(taskDHT,       "taskDHT",       4096, nullptr, 2, nullptr);
  xTaskCreate(taskBLEUpdate, "taskBLEUpdate", 4096, nullptr, 2, nullptr);

  Serial.println("[SYS] Setup complete. Waiting for BLE connection...");
}

void loop() {
  // Heartbeat LED (proves the sketch is running even if Serial is quiet)
  static uint32_t last = 0;
  if (millis() - last > 500) {
    last = millis();
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  // All work happens in tasks/timer
}

/* ====== Timer callback (short, ISR-safe) ====== */
static void cbMeasureTimer(TimerHandle_t) {
  if (!g_connected) return;
  uint8_t token = 1;
  xQueueSendFromISR(qMeasureTrigger, &token, nullptr);
}

/* ====== Timer helpers ====== */
static void startMeasureTimer() {
  if (measureTimer) xTimerStart(measureTimer, 0);
}
static void stopMeasureTimer() {
  if (measureTimer) xTimerStop(measureTimer, 0);
}

/* ====== DHT Task (sensor I/O + validation) ====== */
static void taskDHT(void* arg) {
  (void)arg;
  Serial.println("[DHT] Task started (waiting for triggers)");
  for (;;) {
    uint8_t token;
    if (xQueueReceive(qMeasureTrigger, &token, portMAX_DELAY) == pdTRUE) {
      Reading_t r{};
      float h = dht.readHumidity();
      float t = dht.readTemperature(); // Celsius
      if (isnan(h) || isnan(t)) {
        r.valid = false;
        Serial.println("[DHT] Read failed (NaN) — check wiring and pull-up (4.7–10k)");
      } else {
        r.valid = true;
        r.temperatureC = t;
        r.humidityPct  = h;
        Serial.printf("[DHT] T=%.2f C, H=%.2f %%\n", t, h);
      }
      xQueueSend(qNewReading, &r, pdMS_TO_TICKS(50));
    }
  }
}

/* ====== BLE Update Task (set values + notify) ====== */
static void taskBLEUpdate(void* arg) {
  (void)arg;
  Serial.println("[BLE] Update task started");
  char buf[16];
  for (;;) {
    Reading_t r{};
    if (xQueueReceive(qNewReading, &r, portMAX_DELAY) == pdTRUE) {
      if (!g_connected || !r.valid) continue;

      snprintf(buf, sizeof(buf), "%.2f", r.temperatureC);
      pChTemp->setValue((uint8_t*)buf, strlen(buf));
      snprintf(buf, sizeof(buf), "%.2f", r.humidityPct);
      pChHum->setValue((uint8_t*)buf, strlen(buf));

      // Notify only if client subscribed (CCCD set)
      if (pChTemp->getSubscribedCount() > 0) pChTemp->notify();
      if (pChHum ->getSubscribedCount() > 0) pChHum ->notify();
    }
  }
}
