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
#include <time.h>
#include <Preferences.h>

// WiFi credentials
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// API endpoint
const char* apiURL = API_URL;

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define I2C_SCL 18
#define I2C_SDA 19

// Relay pin (using GPIO 4 which is safe for ESP32 DevKit)
#define RELAY_PIN 4
#define BUTTON_PIN 0 // Boot button for page switch

// Create objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

// Config struct for runtime config
struct Config {
  String wifi_ssid;
  String wifi_password;
  String api_url;
  String static_ip;
  String gateway;
  String subnet;
  String dns1;
  String dns2;
  int reboot_hour = 3; // Default: 3 AM
  int reboot_minute = 0; // Default: 00
};

Config config;
Preferences preferences;

// Variables
unsigned long lastAPICheck = 0;
const unsigned long apiCheckInterval = 5000; // 5 seconds
bool wifiConnected = false;
String lastStatus = "Starting...";
unsigned long displayUpdateTime = 0;
bool relayTriggered = false;
unsigned long lastDisplayAnim = 0;
unsigned long lastDisplaySwitch = 0;
const unsigned long displaySwitchInterval = 5000; // 5 seconds
int displayMode = 0; // 0: status, 1: clock, 2: log
const int displayModeCount = 3;
int animPos = 0;
String lastLog = "";
String lastLoggedStatus = "";

// Log buffer (ring buffer)
#define LOG_SIZE 20
String logBuffer[LOG_SIZE];
int logIndex = 0;

// Scheduled reboot variables
bool rebootedToday = false;
int lastRebootDay = -1;

// API/WiFi failure reboot feature
unsigned long lastGoodAPITime = 0;
unsigned long lastGoodWiFiTime = 0;
const unsigned long maxOfflineMillis = 60UL * 60UL * 1000UL; // 1 hour

void addLog(String msg) {
  logBuffer[logIndex] = String("[") + getTimeString() + "] " + msg;
  logIndex = (logIndex + 1) % LOG_SIZE;
}

String getTimeString() {
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
  return String(buf);
}

void loadConfig() {
  preferences.begin("espremote", true);
  config.wifi_ssid = preferences.getString("ssid", WIFI_SSID);
  config.wifi_password = preferences.getString("wifipw", WIFI_PASSWORD);
  config.api_url = preferences.getString("apiurl", API_URL);
  config.static_ip = preferences.getString("staticip", STATIC_IP);
  config.gateway = preferences.getString("gateway", GATEWAY);
  config.subnet = preferences.getString("subnet", SUBNET);
  config.dns1 = preferences.getString("dns1", DNS1);
  config.dns2 = preferences.getString("dns2", DNS2);
  config.reboot_hour = preferences.getInt("reboot_hour", 3);
  config.reboot_minute = preferences.getInt("reboot_minute", 0);
  preferences.end();
}

void saveConfig() {
  preferences.begin("espremote", false);
  preferences.putString("ssid", config.wifi_ssid);
  preferences.putString("wifipw", config.wifi_password);
  preferences.putString("apiurl", config.api_url);
  preferences.putString("staticip", config.static_ip);
  preferences.putString("gateway", config.gateway);
  preferences.putString("subnet", config.subnet);
  preferences.putString("dns1", config.dns1);
  preferences.putString("dns2", config.dns2);
  preferences.putInt("reboot_hour", config.reboot_hour);
  preferences.putInt("reboot_minute", config.reboot_minute);
  preferences.end();
}

void setup() {
  Serial.begin(115200);
  loadConfig();
  
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
  server.on("/config", HTTP_GET, handleConfigTailwind);
  server.on("/log", HTTP_GET, handleLogTailwind);
  server.begin();
  
  Serial.println("HTTP server started");
  updateDisplay("Server Ready", "IP: " + WiFi.localIP().toString());
  lastGoodWiFiTime = millis();
  lastGoodAPITime = millis();
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
  
  // Track WiFi connection
  if (WiFi.status() == WL_CONNECTED) {
    lastGoodWiFiTime = millis();
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

  // Reboot if WiFi or API down for over 1 hour
  unsigned long nowMillis = millis();
  if ((nowMillis - lastGoodWiFiTime > maxOfflineMillis) || (nowMillis - lastGoodAPITime > maxOfflineMillis)) {
    addLog("Reboot: WiFi/API offline > 1hr");
    delay(1000);
    ESP.restart();
  }

  // Animation/clock/QR update every 300ms
  if (millis() - lastDisplayAnim > 300) {
    updateDisplayPage();
    lastDisplayAnim = millis();
  }
  // เปลี่ยนหน้าอัตโนมัติทุก 5 วินาที
  if (millis() - lastDisplaySwitch > displaySwitchInterval) {
    displayMode = (displayMode + 1) % displayModeCount;
    lastDisplaySwitch = millis();
  }

  // Scheduled daily reboot
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  if (t) {
    if (t->tm_hour == config.reboot_hour && t->tm_min == config.reboot_minute) {
      if (!rebootedToday || t->tm_mday != lastRebootDay) {
        addLog("Scheduled reboot at " + String(config.reboot_hour) + ":" + String(config.reboot_minute));
        delay(1000); // Give time for log to flush
        ESP.restart();
        rebootedToday = true;
        lastRebootDay = t->tm_mday;
      }
    } else if (t->tm_mday != lastRebootDay) {
      rebootedToday = false;
    }
  }
  delay(100);
}

void connectToWiFi() {
  if (config.static_ip.length() > 0 && config.static_ip != "0.0.0.0") {
    IPAddress ip, gw, subnet, dns1, dns2;
    ip.fromString(config.static_ip);
    gw.fromString(config.gateway);
    subnet.fromString(config.subnet);
    dns1.fromString(config.dns1);
    dns2.fromString(config.dns2);
    WiFi.config(ip, gw, subnet, dns1, dns2);
  }
  WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());
  updateDisplay("Connecting WiFi", config.wifi_ssid);
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
  addLog("Manual Power ON via /on");
  
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
  // If query string contains ?config or path is /config, show config page
  if (server.uri().indexOf("config") >= 0 || server.hasArg("config")) {
    handleConfigTailwind();
    return;
  }
  
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

  // Add Config Button
  html += "<a href='/config' class='w-full block glass rounded-xl sm:rounded-2xl p-3 sm:p-4 text-white text-center font-medium text-sm sm:text-base hover:bg-white hover:bg-opacity-20 transition-all duration-200 flex items-center justify-center space-x-2 active:scale-95 mb-2'>";
  html += "<span class='text-lg sm:text-xl'>⚙️</span>";
  html += "<span>Config</span>";
  html += "</a>";

  // Add Log Button
  html += "<a href='/log' class='w-full block glass rounded-xl sm:rounded-2xl p-3 sm:p-4 text-white text-center font-medium text-sm sm:text-base hover:bg-white hover:bg-opacity-20 transition-all duration-200 flex items-center justify-center space-x-2 active:scale-95 mb-2'>";
  html += "<span class='text-lg sm:text-xl'>📝</span>";
  html += "<span>Log</span>";
  html += "</a>";

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

void handleConfigTailwind() {
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>";
  html += "<title>ESP32 Config</title>";
  html += "<script src='https://cdn.tailwindcss.com'></script>";
  html += "<style>body { font-family: 'Inter', sans-serif; } .gradient-bg { background: linear-gradient(135deg, #667eea 0%, #764ba2 50%, #f093fb 100%); } .glass { backdrop-filter: blur(16px); background: rgba(255,255,255,0.1); border: 1px solid rgba(255,255,255,0.2); } input,select { color: #222; }</style></head>";
  html += "<body class='gradient-bg min-h-screen flex flex-col items-center justify-center'>";
  html += "<div class='glass rounded-2xl p-6 max-w-md w-full mt-8'>";
  html += "<h2 class='text-2xl font-bold text-white mb-4 text-center'>ESP32 Configuration</h2>";
  html += "<form method='POST' action='/config' class='space-y-4'>";
  html += "<div><label class='block text-white mb-1'>WiFi SSID</label><input name='ssid' value='" + config.wifi_ssid + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>WiFi Password</label><input name='wifipw' type='password' value='" + config.wifi_password + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>API URL</label><input name='apiurl' value='" + config.api_url + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>Static IP</label><input name='staticip' value='" + config.static_ip + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>Gateway</label><input name='gateway' value='" + config.gateway + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>Subnet</label><input name='subnet' value='" + config.subnet + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>DNS1</label><input name='dns1' value='" + config.dns1 + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>DNS2</label><input name='dns2' value='" + config.dns2 + "' class='w-full rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div>";
  html += "<div><label class='block text-white mb-1'>Daily Reboot Time</label><div class='flex space-x-2'><input name='reboot_hour' type='number' min='0' max='23' value='" + String(config.reboot_hour) + "' class='w-20 rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/>:<input name='reboot_minute' type='number' min='0' max='59' value='" + String(config.reboot_minute) + "' class='w-20 rounded px-3 py-2 bg-white bg-opacity-80 focus:outline-none'/></div><span class='text-xs text-blue-200'>24h format (e.g. 3:00 = 3 AM)</span></div>";
  html += "<button type='submit' class='w-full bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded transition-all'>Save & Reboot</button>";
  html += "</form>";
  html += "<div class='text-center mt-4'><a href='/' class='text-blue-200 hover:underline'>Back to Home</a></div>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleConfig() {
  String html = "<html><head><title>ESP32 Config</title><meta name='viewport' content='width=device-width,initial-scale=1.0'><style>body{font-family:sans-serif;background:#222;color:#fff;}input,button{font-size:1.1em;margin:4px 0;}form{background:#333;padding:16px;border-radius:8px;max-width:400px;margin:auto;}label{display:block;margin-top:10px;}button{background:#4caf50;color:#fff;padding:8px 16px;border:none;border-radius:4px;cursor:pointer;}button:hover{background:#388e3c;}</style></head><body>";
  html += "<h2 style='text-align:center'>ESP32 Configuration</h2>";
  html += "<form method='POST' action='/config'>";
  html += "<label>WiFi SSID:<input name='ssid' value='" + config.wifi_ssid + "'></label>";
  html += "<label>WiFi Password:<input name='wifipw' type='password' value='" + config.wifi_password + "'></label>";
  html += "<label>API URL:<input name='apiurl' value='" + config.api_url + "'></label>";
  html += "<label>Static IP:<input name='staticip' value='" + config.static_ip + "'></label>";
  html += "<label>Gateway:<input name='gateway' value='" + config.gateway + "'></label>";
  html += "<label>Subnet:<input name='subnet' value='" + config.subnet + "'></label>";
  html += "<label>DNS1:<input name='dns1' value='" + config.dns1 + "'></label>";
  html += "<label>DNS2:<input name='dns2' value='" + config.dns2 + "'></label>";
  html += "<button type='submit'>Save & Reboot</button>";
  html += "</form>";
  html += "<div style='text-align:center;margin-top:20px;'><a href='/'>Back to Home</a></div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleConfigPost() {
  if (server.hasArg("ssid")) config.wifi_ssid = server.arg("ssid");
  if (server.hasArg("wifipw")) config.wifi_password = server.arg("wifipw");
  if (server.hasArg("apiurl")) config.api_url = server.arg("apiurl");
  if (server.hasArg("staticip")) config.static_ip = server.arg("staticip");
  if (server.hasArg("gateway")) config.gateway = server.arg("gateway");
  if (server.hasArg("subnet")) config.subnet = server.arg("subnet");
  if (server.hasArg("dns1")) config.dns1 = server.arg("dns1");
  if (server.hasArg("dns2")) config.dns2 = server.arg("dns2");
  if (server.hasArg("reboot_hour")) config.reboot_hour = server.arg("reboot_hour").toInt();
  if (server.hasArg("reboot_minute")) config.reboot_minute = server.arg("reboot_minute").toInt();
  saveConfig();
  server.send(200, "text/html", "<html><body><h2>Config Saved! Rebooting...</h2></body></html>");
  delay(1500);
  ESP.restart();
}

void checkAPI() {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(apiURL);
  http.setTimeout(3000); // Set 3 second timeout
  http.setConnectTimeout(2000); // Set 2 second connection timeout
  int httpCode = http.GET();
  String prevStatus = lastStatus;
  if (httpCode == 200) {
    lastGoodAPITime = millis();
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
    lastStatus = "API Offline";
    Serial.println("API connection failed - continuing operation");
  }
  http.end();
  // Log only if status changed
  if (lastStatus != lastLoggedStatus) {
    updateDisplay("Status: " + lastStatus, "IP: " + WiFi.localIP().toString());
    lastLoggedStatus = lastStatus;
  } else {
    // update OLED but don't add log
    lastLog = "Status: " + lastStatus + " | IP: " + WiFi.localIP().toString();
    updateDisplayPage();
  }
}

void triggerRelay() {
  Serial.println("Triggering relay - Setting LOW (ON)");
  digitalWrite(RELAY_PIN, LOW);
  delay(1000);
  Serial.println("Relay trigger complete - Setting HIGH (OFF)");
  digitalWrite(RELAY_PIN, HIGH);
  updateDisplay("PC POWER ON!", "Signal Sent");
}

void updateDisplayPage() {
  switch (displayMode) {
    case 0:
      updateDisplayStatus();
      break;
    case 1:
      updateDisplayClock();
      break;
    case 2:
      updateDisplayLog();
      break;
  }
}

void updateDisplayStatus() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("ESP32 PC Remote");
  display.setCursor(2, 18);
  display.print(lastStatus);
  display.setCursor(2, 30);
  display.print(WiFi.localIP());
  // WiFi icon
  if (wifiConnected) {
    for (int i = 0; i < 3; i++) display.drawCircle(120, 10, 3 + i * 3, SSD1306_WHITE);
    display.fillCircle(120, 10, 3, SSD1306_WHITE);
  } else {
    display.drawLine(116, 6, 126, 16, SSD1306_WHITE);
    display.drawLine(126, 6, 116, 16, SSD1306_WHITE);
  }
  // Animation: moving dot (bottom area)
  display.fillCircle(10 + animPos * 10, 60, 3, SSD1306_WHITE);
  animPos = (animPos + 1) % 11;
  display.display();
}

void updateDisplayClock() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 18);
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
  display.print(buf);
  display.setTextSize(1);
  display.setCursor(2, 50);
  display.print("Uptime: ");
  unsigned long uptime = millis() / 1000;
  if (uptime < 3600) {
    // แสดงเป็น mm:ss
    sprintf(buf, "%02lu:%02lu", uptime/60, uptime%60);
    display.print(buf);
  } else {
    // แสดงเป็น h:mm:ss
    sprintf(buf, "%lu:%02lu:%02lu", uptime/3600, (uptime/60)%60, uptime%60);
    display.print(buf);
  }
  display.display();
}

void updateDisplayLog() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("Last Log:");
  display.setCursor(2, 18);
  display.print(lastLog);
  display.display();
}

void updateDisplay(String line1, String line2) {
  lastLog = line1 + " | " + line2;
  addLog(line1 + " | " + line2);
  updateDisplayPage();
}

void handleLogTailwind() {
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>";
  html += "<title>ESP32 Log</title>";
  html += "<script src='https://cdn.tailwindcss.com'></script>";
  html += "<style>body { font-family: 'Inter', sans-serif; } .gradient-bg { background: linear-gradient(135deg, #667eea 0%, #764ba2 50%, #f093fb 100%); } .glass { backdrop-filter: blur(16px); background: rgba(255,255,255,0.1); border: 1px solid rgba(255,255,255,0.2); } pre { white-space: pre-wrap; word-break: break-all; }</style></head>";
  html += "<body class='gradient-bg min-h-screen flex flex-col items-center justify-center'>";
  html += "<div class='glass rounded-2xl p-6 max-w-md w-full mt-8'>";
  html += "<h2 class='text-2xl font-bold text-white mb-4 text-center'>ESP32 Log</h2>";
  html += "<div class='bg-white bg-opacity-80 rounded p-3 mb-4' style='max-height:300px;overflow-y:auto;'>";
  for (int i = 0; i < LOG_SIZE; i++) {
    int idx = (logIndex + i) % LOG_SIZE;
    if (logBuffer[idx].length() > 0) {
      html += "<div class='text-xs text-gray-800 font-mono border-b border-gray-200 py-1'>" + logBuffer[idx] + "</div>";
    }
  }
  html += "</div>";
  html += "<div class='text-center mt-4'><a href='/' class='text-blue-500 hover:underline'>Back to Home</a></div>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}
