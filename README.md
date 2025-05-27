# ESP32 PC Remote Control

![Screenshot](./screenshot.png)

This project allows you to remotely power on your PC using an ESP32 microcontroller, a relay, and a simple web interface. It also supports OTA (Over-The-Air) updates, mDNS (local domain name), and displays status on an OLED screen. The backend server is written in Node.js and provides a simple API for triggering the relay.

## Features
- Remotely power on your PC via web interface or API
- OLED display for real-time status, clock, and log (auto-rotating pages)
- WiFi connection status and OTA update support
- Node.js backend API for secure relay triggering
- Stylish web UI for manual control and configuration
- **mDNS support**: Access your ESP32 at http://remote.local on your local network
- **Web-based configuration**: Change WiFi, API, and static IP settings from the web UI
- **View logs**: See the latest 20 log entries from the web UI

## Hardware Requirements
- ESP32 DevKit
- Relay module (for PC power switch)
- SSD1306 OLED display (I2C)
- Jumper wires

## Folder Structure
```
backend/         # Node.js backend API
  main.js        # Main backend server
  Dockerfile     # (Optional) Docker support
main/            # ESP32 Arduino code
  main.ino       # Main firmware for ESP32
  secrets.h      # WiFi credentials and static IP config (not tracked by git)
```

## How It Works
1. The ESP32 connects to your WiFi and starts a web server.
2. The backend Node.js server exposes an API (`/set/on`) to trigger the relay via the ESP32.
3. The ESP32 checks the backend API every 5 seconds. If the API returns `{ on: true }`, it triggers the relay to power on the PC.
4. You can also manually trigger the relay from the ESP32's web interface.
5. The OLED display shows connection and system status, clock, and log (auto-rotating).
6. **You can access the ESP32 using http://remote.local thanks to mDNS.**
7. You can configure WiFi, API URL, and static IP from the web UI (`/config`).
8. You can view the latest logs from the web UI (`/log`).

## Setup Instructions

### 1. ESP32 Firmware
- Open `main/main.ino` in Arduino IDE or PlatformIO.
- Install required libraries:
  - WiFi
  - WebServer
  - HTTPClient
  - ArduinoJson
  - Wire
  - Adafruit_GFX
  - Adafruit_SSD1306
  - ArduinoOTA
  - ESPmDNS *(for mDNS support)*
  - Preferences *(for config storage)*
- Update `main/main.ino` to include WiFi credentials and static IP from `secrets.h` instead of hardcoding them.
- **Hide your API URL and WiFi credentials:**
  - In `main/secrets.h`, add:
    ```cpp
    #define WIFI_SSID "YourSSID"
    #define WIFI_PASSWORD "YourPassword"
    #define API_URL "http://your-backend-api-url"
    #define STATIC_IP "192.168.1.99"
    #define GATEWAY "192.168.1.1"
    #define SUBNET "255.255.255.0"
    #define DNS1 "8.8.8.8"
    #define DNS2 "8.8.4.4"
    ```
  - Make sure `secrets.h` is listed in `.gitignore` so it is not tracked by git.
- Flash the firmware to your ESP32.

### 2. Backend Server
- Go to the `backend` folder:
  ```sh
  cd backend
  npm install
  node main.js
  ```
- The server will run on `http://localhost:3000` by default.
- To trigger the relay, send a GET request to `http://localhost:3000/set/on`.

### 3. Hardware Wiring
- Connect the relay module to GPIO 4 of the ESP32.
- Connect the SSD1306 OLED display to I2C pins (SDA: 19, SCL: 18).
- Wire the relay to your PC's power switch header (in parallel).

## API Endpoints (Backend)
- `GET /`         - Returns `{ on: true/false }` and resets `on` to false after each call.
- `GET /set/on`   - Sets `on` to true for the next `/` request.

## Web Interface
- Access the ESP32's IP address or `http://remote.local` in your browser to use the web UI.
- Click the "Power On PC" button to trigger the relay manually.
- Click the **Config** button to change WiFi, API, or static IP settings from the web UI.
- Click the **Log** button to view the latest 20 log entries (status changes, manual triggers, etc).

## OTA Update
- Hostname: `ESP32-PC-Remote`
- Password: `pcremote123`

## mDNS (Local Domain Name)
- The ESP32 advertises itself as `remote.local` on your local network.
- You can access the web interface at: [http://remote.local](http://remote.local)
- Make sure your computer/device supports mDNS (Windows: install Bonjour, Mac/Linux: built-in).

## License
MIT License

---

**Author:** Thanatach (and contributors)
