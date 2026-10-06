# SafeNest Main Controller

This directory contains the firmware for the primary ESP32 microcontroller, written in C++ using the Arduino IDE framework.

## Features & Implementation

*   **FreeRTOS Multithreading:** The firmware implements a dual-core strategy. Core 0 is dedicated to generating non-blocking PWM signals for the siren, ensuring alarms are never delayed by network latency. Core 1 manages sensor polling, the I2C LCD, and HTTP/Wi-Fi protocols.
*   **Sensor Configuration:**
    *   **DHT22** (Temperature/Humidity): Connected to Pin 23.
    *   **MQ-9** (Gas/Carbon Monoxide): Connected to Pin 34, utilizing voltage division calculations for PPM conversion.
*   **Firebase Integration:** Uploads real-time data to the `/telemetry` node and writes a boolean flag to the `/control/command_snap` node to trigger the remote camera.
*   **Localization Support:** The local LCD interface supports multiple languages mapped via an index variable:
    *   `0`: Kazakh
    *   `1`: Russian
    *   `2`: English

## Dependencies
*   Firebase ESP32 Client
*   LiquidCrystal I2C
*   DHT sensor library
