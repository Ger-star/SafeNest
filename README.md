# SafeNest: IoT Smart Home Safety Monitoring System

SafeNest is a dual-microcontroller embedded system designed for real-time environmental monitoring, hazard detection, and remote security alerts. Built using an ESP32 and an ESP32-CAM, the system leverages the Firebase Realtime Database for cloud-mediated communication and features Telegram integration for instant photo alerts.

**Author:** Yerassyl Berdybek 

## System Architecture

SafeNest evolved from direct UART communication to a robust, cloud-mediated architecture:
*   **Main Node (ESP32):** Handles environmental telemetry (temperature, humidity, CO gas levels), local UI via an I2C LCD, and hazard alarms. It utilizes a multi-threaded FreeRTOS architecture.
*   **Camera Node (ESP32-CAM):** Operates as an asynchronous visual security module. It polls Firebase for snapshot commands and dispatches images securely via the HTTPS Telegram Bot API.
*   **Web Dashboard:** A client-side HTML interface for remote monitoring and manual control overrides.Firebase rules are not closed yet.

## Repository Structure

*   `/firmware/main-esp32/` - Source code for the primary environmental monitor.
*   `/firmware/esp32-cam/` - Source code for the camera module.
*   `/web/` - Client-side HTML dashboard.
*   `/hardware/` - Schematics, component lists, and physical prototype photos.
*   `/documentation/` - Architecture logs, testing results, and engineering notes.
*   `/registered_copyright/` - Legal protection for written code.
