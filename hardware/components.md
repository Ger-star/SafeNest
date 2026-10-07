# Hardware Components & Bill of Materials (BOM)

## Core System Components

| Component | Model / Spec | Interface / Protocol | Primary Function |
| :--- | :--- | :--- | :--- |
| **Main Controller** | ESP32 Dev Module | FreeRTOS, I2C, ADC, Wi-Fi | Central telemetry processing, alarm evaluation, LCD display, and cloud communication |
| **Camera Controller** | AI-Thinker ESP32-CAM (OV2640) | Wi-Fi, HTTPS Telegram API | Cloud-polled visual capture node for remote security photo dispatch[cite: 1]. |
| **Carbon Monoxide Sensor** | MQ-9 Gas Sensor | Analog Input (Pin 34 via 11dB attenuation) | Measures CO levels using analog voltage conversion[cite: 1]. |
| **Climate Sensor** | DHT22 (AM2302) | Single-Wire Digital (Pin 23) | Monitors room temperature and humidity[cite: 1]. |
| **Local Display** | I2C 16×2 LCD with PCF8574 I2C Adapter | I2C (SDA Pin 21, SCL Pin 22) | Displays local multi-language telemetry and alarm status[cite: 1]. |
| **Audible Alarm** | Passive Piezo Buzzer | PWM (Pin 25, Core 0 FreeRTOS) | Generates non-blocking local siren patterns[cite: 1]. |
| **Power Stabilization** | 470 µF Electrolytic Capacitor | Power Rail | Filters current spikes and prevents power drops during camera operation. |

---

## Hardware Evolution & Technical Iterations

### 1. Gas Sensor Replacement (MQ-7 → MQ-9)
* **Initial Selection:** The prototype was initially constructed using an MQ-7 carbon monoxide sensor.
* **Iteration:** Following sensor failure during early physical development, the MQ-7 was replaced with an MQ-9 sensor to maintain reliable analog sensing

### 2. Camera Power Integrity
* **Problem:** Integrating the ESP32-CAM module onto the shared power board caused voltage fluctuations and instability during Wi-Fi transmission.
* **Solution:** Added a 470 µF decoupling capacitor across the power rails and disabled the software brownout detector (`WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)`) in the camera firmware to ensure continuous execution

### 3. Architecture Evolution (UART to Cloud Synchronization)
* **Initial Concept:** Inter-controller communication was originally designed over direct physical UART serial lines
* **Final Implementation:** To eliminate communication locks and physical pin conflicts, communication was moved to a decoupled cloud-polled model using the Firebase Realtime Database `/control/command_snap` node