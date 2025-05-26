# ESP32 PC Remote Control

This project allows you to remotely power on your PC using an ESP32 microcontroller, a relay, and a simple web interface. It also supports OTA (Over-The-Air) updates and displays status on an OLED screen. The backend server is written in Node.js and provides a simple API for triggering the relay.

## Features
- Remotely power on your PC via web interface or API
- OLED display for real-time status
- WiFi connection status and OTA update support
- Node.js backend API for secure relay triggering
- Stylish web UI for manual control

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
```

## How It Works
1. The ESP32 connects to your WiFi and starts a web server.
2. The backend Node.js server exposes an API (`/set/on`) to trigger the relay via the ESP32.
3. The ESP32 checks the backend API every 5 seconds. If the API returns `{ on: true }`, it triggers the relay to power on the PC.
4. You can also manually trigger the relay from the ESP32's web interface.
5. The OLED display shows connection and system status.

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
- Update `main/main.ino` to include WiFi credentials from `secrets.h` instead of hardcoding them.
- Create a `main/secrets.h` file with your WiFi SSID and password as follows:
  ```cpp
  #define WIFI_SSID "YourSSID"
  #define WIFI_PASSWORD "YourPassword"
  ```
- The `main/secrets.h` file is already in `.gitignore` and will not be tracked by Git.
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
- Connect the relay module to GPIO 2 of the ESP32.
- Connect the SSD1306 OLED display to I2C pins (SDA: 19, SCL: 18).
- Wire the relay to your PC's power switch header (in parallel).

## API Endpoints (Backend)
- `GET /`         - Returns `{ on: true/false }` and resets `on` to false after each call.
- `GET /set/on`   - Sets `on` to true for the next `/` request.

## Web Interface
- Access the ESP32's IP address in your browser to use the web UI.
- Click the "Turn On PC" button to trigger the relay manually.

## OTA Update
- Hostname: `ESP32-PC-Remote`
- Password: `pcremote123`

## License
MIT License

---

**Author:** Thanatach (and contributors)
