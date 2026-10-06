# SafeNest Web Dashboard

This directory contains the client-side user interface (`index.html`) for remotely managing the SafeNest system.

## Functionality

The dashboard interacts directly with the Firebase Realtime Database (`safenest2508-default-rtdb`):
*   **Telemetry (Read):** Pulls live temperature, humidity, and gas PPM data from the `/telemetry` node to update radial gauges and historical charts.
*   **Control (Write):** Allows users to send commands to the `/control` node to toggle the local LCD backlight, change siren modes, or manually trigger an express photo capture from the ESP32-CAM.

## Deployment & Security Note

Because this is a purely client-side application, Firebase configuration details are exposed in the HTML source code. Before deploying this dashboard publicly (e.g., via GitHub Pages or Firebase Hosting), you **must** configure your Firebase Security Rules to restrict unauthorized read/write access to the database to prevent external tampering with your hardware.
