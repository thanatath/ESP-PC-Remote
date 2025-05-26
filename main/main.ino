#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "secrets.h"

// WiFi credentials
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// API endpoint
const char* apiURL = "http://turnonpc.thanatach.com/";

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define I2C_SCL 18
#define I2C_SDA 19

// Relay pin (using GPIO 2 which is safe for ESP32 DevKit)
#define RELAY_PIN 2

// Create objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

// Variables
unsigned long lastAPICheck = 0;
const unsigned long apiCheckInterval = 5000; // 5 seconds
bool wifiConnected = false;
String lastStatus = "Starting...";
unsigned long displayUpdateTime = 0;
bool relayTriggered = false;

void setup() {
  Serial.begin(115200);
  
  // Initialize relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  Serial.println("Relay pin initialized to HIGH (OFF)");
  Serial.print("Current relay pin state: ");
  Serial.println(digitalRead(RELAY_PIN));
  
  // Initialize Wire with custom SCL and SDA pins
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Initialize OLED display
  if (!display.begin(SSD1306_PAGEADDR, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  
  // Clear the buffer and set display settings
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  updateDisplay("Initializing...", "");
  
  // Connect to WiFi
  connectToWiFi();

  // Start mDNS responder
  if (MDNS.begin("remote")) {
    Serial.println("mDNS responder started: http://remote.local");
    updateDisplay("mDNS Ready", "remote.local");
  } else {
    Serial.println("Error setting up mDNS responder!");
    updateDisplay("mDNS Error", "");
  }

  // Setup OTA
  setupOTA();
  
  // Setup HTTP server routes
  server.on("/on", HTTP_GET, handlePowerOn);
  server.on("/", HTTP_GET, handleRoot);
  server.begin();
  
  Serial.println("HTTP server started");
  updateDisplay("Server Ready", "IP: " + WiFi.localIP().toString());
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();
  
  // Handle HTTP requests
  server.handleClient();
  
  // Debug: Print relay state every 10 seconds
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 10000) {
    Serial.print("Relay pin state: ");
    Serial.println(digitalRead(RELAY_PIN));
    lastDebug = millis();
  }
  
  // Check API every 5 seconds
  if (millis() - lastAPICheck >= apiCheckInterval) {
    checkAPI();
    lastAPICheck = millis();
  }
  
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      wifiConnected = false;
      updateDisplay("WiFi Lost", "Reconnecting...");
      connectToWiFi();
    }
  }
  
  delay(100);
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  updateDisplay("Connecting WiFi", ssid);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    updateDisplay("WiFi Connected", WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed!");
    updateDisplay("WiFi Failed", "Check credentials");
  }
}

void setupOTA() {
  // Set OTA hostname
  ArduinoOTA.setHostname("ESP32-PC-Remote");
  
  // Set OTA password (optional but recommended)
  ArduinoOTA.setPassword("pcremote123");
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
    updateDisplay("OTA Update", "Starting...");
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    updateDisplay("OTA Complete", "Rebooting...");
    delay(1000);
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int percent = (progress / (total / 100));
    Serial.printf("Progress: %u%%\r", percent);
    updateDisplay("OTA Update", String(percent) + "%");
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    String errorMsg = "";
    if (error == OTA_AUTH_ERROR) {
      errorMsg = "Auth Failed";
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      errorMsg = "Begin Failed";
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      errorMsg = "Connect Failed";
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      errorMsg = "Receive Failed";
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      errorMsg = "End Failed";
      Serial.println("End Failed");
    }
    updateDisplay("OTA Error", errorMsg);
    delay(3000);
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Hostname: ESP32-PC-Remote");
  Serial.println("Password: pcremote123");
}

void handlePowerOn() {
  triggerRelay();
  
  // Create JSON response
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["status"] = "success";
  jsonDoc["message"] = "PC Power On Signal Sent";
  jsonDoc["timestamp"] = millis(); // Example timestamp

  String jsonString;
  serializeJson(jsonDoc, jsonString);
  
  server.send(200, "application/json", jsonString);
  Serial.println("Manual power on triggered via HTTP, JSON response sent.");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP32 PC Remote Control</title>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; min-height: 100vh; }";
  html += ".container { max-width: 500px; margin: 0 auto; background: rgba(255,255,255,0.1); padding: 30px; border-radius: 20px; backdrop-filter: blur(10px); box-shadow: 0 8px 32px rgba(0,0,0,0.3); }";
  html += "h1 { text-align: center; margin-bottom: 30px; font-size: 2em; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); }";
  html += ".status-card { background: rgba(255,255,255,0.2); padding: 20px; border-radius: 15px; margin: 15px 0; border-left: 5px solid #00ff88; }";
  html += ".status-label { font-weight: bold; font-size: 0.9em; color: #b3d9ff; }";
  html += ".status-value { font-size: 1.1em; margin-top: 5px; }";
  html += ".power-btn { width: 100%; padding: 20px; font-size: 1.2em; font-weight: bold; color: white; background: linear-gradient(45deg, #ff6b6b, #ff8e8e); border: none; border-radius: 15px; cursor: pointer; transition: all 0.3s ease; box-shadow: 0 4px 15px rgba(255,107,107,0.3); }";
  html += ".power-btn:hover { transform: translateY(-2px); box-shadow: 0 8px 25px rgba(255,107,107,0.4); background: linear-gradient(45deg, #ff5252, #ff7979); }";
  html += ".power-btn:active { transform: translateY(0); }";
  html += ".refresh-btn { background: linear-gradient(45deg, #4ecdc4, #44a08d); color: white; border: none; padding: 10px 20px; border-radius: 10px; cursor: pointer; float: right; font-size: 0.9em; }";
  html += ".icon { display: inline-block; margin-right: 10px; }";
  html += "@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.7; } 100% { opacity: 1; } }";
  html += ".pulse { animation: pulse 2s infinite; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>🖥️ ESP32 PC Remote</h1>";
  
  html += "<div class='status-card'>";
  html += "<div class='status-label'>📡 Connection Status</div>";
  html += "<div class='status-value'>";
  if (wifiConnected) {
    html += "✅ Connected to " + String(ssid);
  } else {
    html += "❌ Disconnected";
  }
  html += "</div></div>";
  
  html += "<div class='status-card'>";
  html += "<div class='status-label'>🌐 Network Information</div>";
  html += "<div class='status-value'>IP: " + WiFi.localIP().toString() + "</div>";
  html += "</div>";
  
  html += "<div class='status-card'>";
  html += "<div class='status-label'>⚡ System Status</div>";
  html += "<div class='status-value'>" + lastStatus + "</div>";
  html += "</div>";
  
  html += "<div class='status-card'>";
  html += "<div class='status-label'>🛠️ OTA Update</div>";
  html += "<div class='status-value'>Hostname: ESP32-PC-Remote</div>";
  html += "<div class='status-value'>Password: pcremote123</div>";
  html += "</div>";
  
  html += "<div style='margin: 30px 0;'>";
  html += "<button class='refresh-btn' onclick='location.reload()'>🔄 Refresh</button>";
  html += "</div>";
  
  html += "<button class='power-btn' onclick='powerOn()'>🔌 Turn On PC</button>";
  
  html += "</div>";
  html += "<script>";
  html += "function powerOn() {";
  html += "  fetch('/on').then(response => {";
  html += "    if(response.ok) {";
  html += "      alert('✅ PC Power Signal Sent Successfully!');";
  html += "      setTimeout(() => location.reload(), 1000);";
  html += "    } else {";
  html += "      alert('❌ Failed to send power signal');";
  html += "    }";
  html += "  }).catch(err => alert('❌ Network error'));";
  html += "}";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

void checkAPI() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  http.begin(apiURL);
  http.setTimeout(3000); // Set 3 second timeout
  http.setConnectTimeout(2000); // Set 2 second connection timeout
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("API Response: " + payload);
    
    // Parse JSON
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      bool shouldTurnOn = doc["on"];
      if (shouldTurnOn) {
        triggerRelay();
        lastStatus = "API Triggered ON";
        Serial.println("API triggered power on");
      } else {
        lastStatus = "API: OFF";
      }
    } else {
      lastStatus = "API Parse Error";
      Serial.println("JSON parse error");
    }
  } else if (httpCode > 0) {
    lastStatus = "API Error: " + String(httpCode);
    Serial.println("HTTP Error: " + String(httpCode));
  } else {
    // Connection failed (timeout, no connection, etc.)
    lastStatus = "API Offline";
    Serial.println("API connection failed - continuing operation");
  }
  
  http.end();
  
  // Always update display even if API fails
  updateDisplay("Status: " + lastStatus, "IP: " + WiFi.localIP().toString());
}

void triggerRelay() {
  Serial.println("Triggering relay - Setting LOW (ON)");
  digitalWrite(RELAY_PIN, LOW);
  delay(1000);
  Serial.println("Relay trigger complete - Setting HIGH (OFF)");
  digitalWrite(RELAY_PIN, HIGH);
  updateDisplay("PC POWER ON!", "Signal Sent");
}

void updateDisplay(String line1, String line2) {
  display.clearDisplay();
  
  // Header with decorative border
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Title area
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("ESP32 PC Remote");
  
  // Status area
  display.setCursor(2, 14);
  display.print(line1);
  display.setCursor(2, 24);
  display.print(line2);
  
  // WiFi indicator
  if (wifiConnected) {
    display.fillCircle(122, 5, 2, SSD1306_WHITE);
  }
  
  display.display();
}
