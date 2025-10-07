# BLE-DHT22-FreeRTOS  
**Bluetooth Low Energy Sensor Node – Temperature & Humidity**

## 📡 Overview
This project implements a **BLE-enabled sensor node** on an **ESP32-S3 DevKitC-1** using **FreeRTOS**.  
It measures temperature and humidity from a **DHT22 sensor** and exposes both as **GATT characteristics** with real-time notifications.

## 🧠 Features
- ✅ **BLE GATT Server** with one profile and one service  
  - Temperature Characteristic (UUID ...002)  
  - Humidity Characteristic (UUID ...003)  
- ✅ **Notifications** supported for both characteristics  
- ✅ **DHT22 sensor reading** handled in its own FreeRTOS task  
- ✅ **Software Timer (5 s)** triggers measurement **only when a BLE device is connected**  
- ✅ **LED heartbeat** (GPIO 48) indicates running system  
- ✅ Works with **ST BLE Toolbox** or **nRF Connect** mobile apps  

## 🔌 Hardware Connections
| DHT22 Pin | ESP32-S3 Pin | Notes |
|------------|--------------|-------|
| **VCC**    | 3 V3         | 3.3 V only |
| **DATA**   | GPIO 9       | Used in code (#define DHT_PIN 9) |
| **GND**    | GND          | Common ground |
| *(Optional)* | 4.7 kΩ – 10 kΩ | Pull-up resistor DATA→3.3 V (if bare sensor) |

## ⚙️ PlatformIO Configuration
\\\ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600
monitor_dtr = 0
monitor_rts = 0
build_flags =
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DCORE_DEBUG_LEVEL=3
lib_deps =
  h2zero/NimBLE-Arduino @ ^1.4.2
  beegee-tokyo/DHT sensor library for ESPx @ ^1.19.0
\\\

## 🧩 Software Architecture
- **Task 1 – DHT Sensor Task** (reads every 5 s when connected)  
- **Task 2 – BLE Update Task** (sets values & notifies)  
- **FreeRTOS Software Timer** (5 s, starts on connect, stops on disconnect)

**Service UUID:** 6e400001-b5a3-f393-e0a9-e50e24dcca9e  
**Characteristics:** ...002 Temperature (C), ...003 Humidity (%), both **READ + NOTIFY**

## 📲 Testing Procedure
1) pio run -t upload  
2) pio device monitor -b 115200  
3) Connect with **ST BLE Toolbox** or **nRF Connect** to **ESP32 DHT22 (BLE)**  
4) Enable **notifications** on both characteristics  
5) Observe updates every ≈ 5 s

## 🧾 Example Output
\\\
[BLE] Device connected -> 5s measurement timer STARTED
[DHT] T=29.60 C, H=40.60 %
\\\

## 📸 Screenshot
Attach your app screenshot showing both characteristics and values.

## 📚 Credits
- NimBLE-Arduino  
- DHT sensor library for ESPx
