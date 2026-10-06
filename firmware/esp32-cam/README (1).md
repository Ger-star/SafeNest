# SafeNest Camera Controller

This directory contains the firmware for the ESP32-CAM module. It operates as a dedicated visual alert node separated from the main environmental sensor loop.

## Operation

Instead of relying on unstable physical wiring (like UART) between microcontrollers, the ESP32-CAM acts as an independent cloud client:
1.  **Polling:** The module continuously monitors the `/control/command_snap` boolean node in the Firebase Realtime Database.
2.  **Execution:** When the main ESP32 (or the web dashboard) sets this flag to `true` (e.g., during a gas leak or manual trigger), the ESP32-CAM captures a JPEG image.
3.  **Dispatch:** The image is formatted and dispatched directly to a predefined chat using the HTTPS Telegram Bot API.
4.  **Reset:** The camera node resets the Firebase flag back to `false` to await the next trigger.

## Setup Requirements
*   A valid Telegram Bot Token and Chat ID.
*   Firebase Realtime Database credentials.
*   Ensure the correct board model (e.g., AI Thinker) is selected in the Arduino IDE before flashing.
