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
const char* apiURL = API_URL;

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define I2C_SCL 18
#define I2C_SDA 19

// Relay pin (using GPIO 4 which is safe for ESP32 DevKit)
#define RELAY_PIN 4

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
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>";
  html += "<title>ESP32 PC Remote Control</title>";
  html += "<script src='https://cdn.tailwindcss.com'></script>";
  html += "<script>";
  html += "tailwind.config = {";
  html += "  theme: {";
  html += "    extend: {";
  html += "      animation: {";
  html += "        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',";
  html += "        'bounce-slow': 'bounce 2s infinite',";
  html += "        'ping-slow': 'ping 2s cubic-bezier(0, 0, 0.2, 1) infinite'";
  html += "      }";
  html += "    }";
  html += "  }";
  html += "}";
  html += "</script>";
  html += "<style>";
  html += "@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap');";
  html += "body { font-family: 'Inter', sans-serif; }";
  html += ".gradient-bg { background: linear-gradient(135deg, #667eea 0%, #764ba2 50%, #f093fb 100%); }";
  html += ".glass { backdrop-filter: blur(16px); background: rgba(255, 255, 255, 0.1); border: 1px solid rgba(255, 255, 255, 0.2); }";
  html += ".status-online { background: linear-gradient(45deg, #10b981, #059669); }";
  html += ".status-offline { background: linear-gradient(45deg, #ef4444, #dc2626); }";
  html += ".power-btn-gradient { background: linear-gradient(45deg, #f59e0b, #d97706); }";
  html += ".power-btn-gradient:hover { background: linear-gradient(45deg, #d97706, #b45309); }";
  html += "</style></head>";
  html += "<body class='gradient-bg min-h-screen'>";
  
  // Main container with improved mobile layout
  html += "<div class='min-h-screen flex flex-col p-4 sm:p-6 lg:p-8'>";
  html += "<div class='flex-1 max-w-md mx-auto w-full space-y-4 sm:space-y-6'>";
  
  // Header with logo and title
  html += "<div class='glass rounded-2xl sm:rounded-3xl p-4 sm:p-6 text-center'>";
  html += "<div class='text-4xl sm:text-5xl mb-2 animate-bounce-slow'>🖥️</div>";
  html += "<h1 class='text-xl sm:text-2xl lg:text-3xl font-bold text-white mb-2'>ESP32 PC Remote</h1>";
  html += "<p class='text-blue-100 text-sm sm:text-base opacity-90'>Remote Power Control</p>";
  html += "</div>";
  
  // Connection Status Card
  html += "<div class='glass rounded-xl sm:rounded-2xl p-4 sm:p-5'>";
  html += "<div class='flex items-center justify-between mb-3'>";
  html += "<div class='flex items-center space-x-2 sm:space-x-3'>";
  html += "<div class='text-lg sm:text-xl'>📡</div>";
  html += "<span class='text-white font-medium text-sm sm:text-base'>WiFi Status</span>";
  html += "</div>";
  if (wifiConnected) {
    html += "<div class='status-online text-white px-2 sm:px-3 py-1 rounded-full text-xs sm:text-sm font-medium flex items-center space-x-1'>";
    html += "<div class='w-2 h-2 bg-white rounded-full animate-ping-slow'></div>";
    html += "<span>Online</span>";
    html += "</div>";
  } else {
    html += "<div class='status-offline text-white px-2 sm:px-3 py-1 rounded-full text-xs sm:text-sm font-medium'>Offline</div>";
  }
  html += "</div>";
  html += "<div class='text-blue-100 text-xs sm:text-sm'>";
  if (wifiConnected) {
    html += "Connected to " + String(ssid);
  } else {
    html += "Disconnected from WiFi";
  }
  html += "</div>";
  html += "</div>";
  
  // Network Information Card
  html += "<div class='glass rounded-xl sm:rounded-2xl p-4 sm:p-5'>";
  html += "<div class='flex items-center space-x-2 sm:space-x-3 mb-3'>";
  html += "<div class='text-lg sm:text-xl'>🌐</div>";
  html += "<span class='text-white font-medium text-sm sm:text-base'>Network Info</span>";
  html += "</div>";
  html += "<div class='space-y-2'>";
  html += "<div class='flex flex-col sm:flex-row sm:justify-between sm:items-center'>";
  html += "<span class='text-blue-200 text-xs sm:text-sm'>IP Address</span>";
  html += "<span class='text-white font-mono text-xs sm:text-sm break-all'>" + WiFi.localIP().toString() + "</span>";
  html += "</div>";
  html += "<div class='flex flex-col sm:flex-row sm:justify-between sm:items-center'>";
  html += "<span class='text-blue-200 text-xs sm:text-sm'>Hostname</span>";
  html += "<span class='text-white font-mono text-xs sm:text-sm'>remote.local</span>";
  html += "</div>";
  html += "</div>";
  html += "</div>";
  
  // System Status Card
  html += "<div class='glass rounded-xl sm:rounded-2xl p-4 sm:p-5'>";
  html += "<div class='flex items-center space-x-2 sm:space-x-3 mb-3'>";
  html += "<div class='text-lg sm:text-xl'>⚡</div>";
  html += "<span class='text-white font-medium text-sm sm:text-base'>System Status</span>";
  html += "</div>";
  html += "<div class='text-blue-100 text-xs sm:text-sm'>" + lastStatus + "</div>";
  html += "</div>";
  
  // OTA Update Info (Collapsible on mobile)
  html += "<details class='glass rounded-xl sm:rounded-2xl overflow-hidden'>";
  html += "<summary class='p-4 sm:p-5 cursor-pointer hover:bg-white hover:bg-opacity-10 transition-colors'>";
  html += "<div class='flex items-center space-x-2 sm:space-x-3'>";
  html += "<div class='text-lg sm:text-xl'>🛠️</div>";
  html += "<span class='text-white font-medium text-sm sm:text-base'>OTA Update Info</span>";
  html += "</div>";
  html += "</summary>";
  html += "<div class='px-4 sm:px-5 pb-4 sm:pb-5 pt-0 border-t border-white border-opacity-20'>";
  html += "<div class='space-y-2 mt-3'>";
  html += "<div class='flex flex-col sm:flex-row sm:justify-between sm:items-center'>";
  html += "<span class='text-blue-200 text-xs sm:text-sm'>Hostname</span>";
  html += "<span class='text-white font-mono text-xs sm:text-sm'>ESP32-PC-Remote</span>";
  html += "</div>";
  html += "<div class='flex flex-col sm:flex-row sm:justify-between sm:items-center'>";
  html += "<span class='text-blue-200 text-xs sm:text-sm'>Password</span>";
  html += "<span class='text-white font-mono text-xs sm:text-sm'>pcremote123</span>";
  html += "</div>";
  html += "</div>";
  html += "</div>";
  html += "</details>";
  
  // Control Buttons
  html += "<div class='space-y-3 sm:space-y-4 pt-2'>";
  html += "<button onclick='location.reload()' class='w-full glass rounded-xl sm:rounded-2xl p-3 sm:p-4 text-white hover:bg-white hover:bg-opacity-20 transition-all duration-200 flex items-center justify-center space-x-2 active:scale-95'>";
  html += "<span class='text-lg sm:text-xl'>🔄</span>";
  html += "<span class='font-medium text-sm sm:text-base'>Refresh Status</span>";
  html += "</button>";
  
  html += "<button id='powerBtn' onclick='powerOn()' class='w-full power-btn-gradient rounded-xl sm:rounded-2xl p-4 sm:p-6 text-white font-bold text-base sm:text-lg hover:shadow-2xl transition-all duration-300 transform hover:scale-105 active:scale-95 flex items-center justify-center space-x-3'>";
  html += "<span class='text-2xl sm:text-3xl animate-pulse-slow'>🔌</span>";
  html += "<span>Power On PC</span>";
  html += "</button>";
  html += "</div>";
  
  html += "</div>";
  html += "</div>";
  
  // Success Toast (hidden by default)
  html += "<div id='toast' class='fixed top-3 left-1/2 -translate-x-1/2 z-50 max-w-xs w-full px-4 pointer-events-none'>";
  html +=   "<div class='glass rounded-lg shadow-lg p-3 flex items-center space-x-2 text-white bg-opacity-90 transition-transform duration-300 transform -translate-y-20 opacity-0' style='backdrop-filter: blur(12px);' id='toastBox'>";
  html +=     "<span class='text-xl'>✅</span>";
  html +=     "<div>";
  html +=       "<div class='font-semibold text-sm'>Success!</div>";
  html +=       "<div class='text-xs text-blue-100'>PC Power Signal Sent</div>";
  html +=     "</div>";
  html +=   "</div>";
  html += "</div>";
  
  html += "<script>";
  html += "function powerOn() {";
  html += "  const btn = document.getElementById('powerBtn');";
  html += "  const toastBox = document.getElementById('toastBox');";
  html += "  btn.disabled = true;";
  html += "  btn.innerHTML = '<div class=\"animate-spin rounded-full h-6 w-6 border-b-2 border-white\"></div><span>Sending...</span>';";
  html += "  fetch('/on').then(response => {";
  html += "    if(response.ok) {";
  html += "      toastBox.classList.remove('-translate-y-20','opacity-0');";
  html += "      toastBox.classList.add('translate-y-0','opacity-100');";
  html += "      setTimeout(() => {";
  html += "        toastBox.classList.remove('translate-y-0','opacity-100');";
  html += "        toastBox.classList.add('-translate-y-20','opacity-0');";
  html += "        setTimeout(() => location.reload(), 500);";
  html += "      }, 1800);";
  html += "    } else {";
  html += "      alert('❌ Failed to send power signal');";
  html += "      btn.disabled = false;";
  html += "      btn.innerHTML = '<span class=\"text-2xl sm:text-3xl animate-pulse-slow\">🔌</span><span>Power On PC</span>';";
  html += "    }";
  html += "  }).catch(err => {";
  html += "    alert('❌ Network error');";
  html += "    btn.disabled = false;";
  html += "    btn.innerHTML = '<span class=\"text-2xl sm:text-3xl animate-pulse-slow\">🔌</span><span>Power On PC</span>';";
  html += "  });";
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
