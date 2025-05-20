#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <WebServer.h>
#include <EEPROM.h>

// Cấu trúc lưu trữ cấu hình trong EEPROM
struct Config {
  char ssid[32];
  char password[32];
  char city[32];
  bool configured;
  int displayLayout; // 0: Original, 1: New layout
};

// Biến toàn cục
Config config;
WebServer server(80);

// OpenWeatherMap API
const char* weatherApiKey = "d59cd7cbe4d46110fbce95065c2bd3c9";
String weatherUrl;
String forecastUrl;

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000); // GMT+7 for Vietnam

// Khởi tạo I2C với chân SDA = 26, SCL = 27
TwoWire myWire = TwoWire(0);

// Khởi tạo OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 27, /* data=*/ 26);

// Dữ liệu ảnh XBM cho layout gốc
static const unsigned char image_menu_home_bits[] = {
  0x80,0x00,0x48,0x01,0x38,0x02,0x98,0x04,
  0x48,0x09,0x24,0x12,0x12,0x24,0x09,0x48,
  0x66,0x30,0x64,0x10,0x04,0x17,0x04,0x15,
  0x04,0x17,0x04,0x15,0xfc,0x1f,0x00,0x00
};

// Dữ liệu ảnh XBM cho layout mới
static const unsigned char image_Layer_12_bits[] = {
  0x20,0x00,0x20,0x00,0x30,0x00,0x50,0x00,0x48,0x00,0x88,0x00,0x04,0x01,0x04,0x01,
  0x82,0x02,0x02,0x03,0x01,0x05,0x01,0x04,0x02,0x02,0x02,0x02,0x0c,0x01,0xf0,0x00
};

static const unsigned char image_Layer_2_bits[] = {
  0x38,0x00,0x44,0x40,0xd4,0xa0,0x54,0x40,0xd4,0x1c,0x54,0x06,0xd4,0x02,0x54,0x02,
  0x54,0x06,0x92,0x1c,0x39,0x01,0x75,0x01,0x7d,0x01,0x39,0x01,0x82,0x00,0x7c,0x00
};

// Dữ liệu ảnh XBM cho thời tiết
static const unsigned char image_weather_cloud_rain_bits[] = {
  0x00,0x00,0x00,0xe0,0x03,0x00,0x10,0x04,0x00,0x08,0x08,0x00,0x0c,0x10,0x00,0x02,
  0x70,0x00,0x01,0x80,0x00,0x01,0x00,0x01,0x02,0x00,0x01,0xfc,0xff,0x00,0x80,0x08,
  0x00,0x44,0x44,0x00,0x22,0x21,0x00,0x89,0x14,0x00,0x44,0x02,0x00,0x00,0x01,0x00
};

static const unsigned char image_weather_cloud_snow_bits[] = {
  0x00,0x00,0x00,0xe0,0x03,0x00,0x10,0x04,0x00,0x08,0x08,0x00,0x0c,0x10,0x00,0x02,
  0x70,0x00,0x01,0x80,0x00,0x01,0x00,0x01,0x02,0x00,0x01,0xfc,0xff,0x00,0x00,0x00,
  0x00,0x90,0x52,0x00,0x04,0x00,0x00,0x10,0x09,0x00,0x44,0x22,0x00,0x00,0x00,0x00
};

static const unsigned char image_weather_cloud_sunny_bits[] = {
  0x00,0x04,0x00,0x40,0x40,0x00,0x00,0x0e,0x00,0x80,0x31,0x00,0x90,0x20,0x01,0x40,
  0x40,0x00,0x40,0x40,0x00,0xe0,0x41,0x00,0x10,0x22,0x01,0x08,0x34,0x00,0x0c,0x0c,
  0x00,0x06,0x78,0x00,0x01,0xc0,0x00,0x01,0x80,0x00,0x01,0x80,0x00,0xfe,0x7f,0x00
};

static const unsigned char image_weather_frost_bits[] = {
  0x80,0x00,0xc8,0x09,0x8c,0x18,0xce,0x39,0x90,0x04,0xa0,0x02,0xca,0x29,0x7f,0x7f,
  0xca,0x29,0xa0,0x02,0x90,0x04,0xce,0x39,0x8c,0x18,0xc8,0x09,0x80,0x00,0x00,0x00
};

static const unsigned char image_weather_wind_bits[] = {
  0x00,0x00,0x00,0x00,0x00,0x0c,0xc0,0x11,0x20,0x22,0x20,0x22,0x00,0x22,0x00,0x11,
  0xff,0x4c,0x00,0x00,0xb5,0x41,0x00,0x06,0x00,0x08,0x00,0x08,0x80,0x04,0x00,0x03
};

// Biến lưu dữ liệu thời tiết
float temp = 0.0;
float humidity = 0.0;
float dewPoint = 0.0;
float tomorrowTemp = 0.0;
float tomorrowHumidity = 0.0;
String tomorrowWeather = "";

// Biến kiểm soát hiển thị IP
bool showIP = true;
unsigned long ipDisplayStartTime = 0;
const unsigned long IP_DISPLAY_DURATION = 10000; // Hiển thị IP trong 10 giây

// Hàm chuyển đổi trạng thái thời tiết sang tiếng Việt không dấu
String getVietnameseWeather(String englishWeather) {
  englishWeather.toLowerCase();
  if (englishWeather == "clear") return "Troi nang";
  if (englishWeather == "clouds") return "Co may";
  if (englishWeather == "rain") return "Co mua";
  if (englishWeather == "drizzle") return "Mua phun";
  if (englishWeather == "thunderstorm") return "Bao";
  if (englishWeather == "snow") return "Tuyet";
  if (englishWeather == "mist" || englishWeather == "fog") return "Suong mu";
  return "Other";
}

// Hàm chọn icon thời tiết
const unsigned char* getWeatherIcon(String weather) {
  weather.toLowerCase();
  if (weather == "co mua") return image_weather_cloud_rain_bits;
  if (weather == "mua phun" || weather == "suong mu") return image_weather_cloud_snow_bits;
  if (weather == "co may") return image_weather_cloud_sunny_bits;
  if (weather == "bao") return image_weather_wind_bits;
  if (weather == "tuyet") return image_weather_frost_bits;
  if (weather == "troi nang") return image_weather_cloud_sunny_bits; // Sử dụng icon 17x16
  return image_weather_cloud_sunny_bits; // Mặc định
}

// Hàm đọc cấu hình từ EEPROM
void loadConfig() {
  EEPROM.begin(sizeof(Config));
  EEPROM.get(0, config);
  EEPROM.end();

  // Kiểm tra nếu chưa có cấu hình
  if (!config.configured) {
    strcpy(config.ssid, "21 LQS T2");
    strcpy(config.password, "0901140014");
    strcpy(config.city, "Hanoi");
    config.configured = true;
    config.displayLayout = 0;
    saveConfig();
  }
}

// Hàm lưu cấu hình vào EEPROM
void saveConfig() {
  EEPROM.begin(sizeof(Config));
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();
}

// Hàm hiển thị địa chỉ IP trên OLED
void displayIPAddress() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont11_tr);
  
  if (WiFi.status() == WL_CONNECTED) {
    u8g2.drawStr(10, 20, "WiFi Connected");
    u8g2.drawStr(10, 35, "IP Address:");
    u8g2.drawStr(10, 50, WiFi.localIP().toString().c_str());
  } else {
    u8g2.drawStr(10, 20, "Access Point Mode");
    u8g2.drawStr(10, 35, "Connect to: WeatherStation");
    u8g2.drawStr(10, 50, WiFi.softAPIP().toString().c_str());
  }
  
  u8g2.sendBuffer();
}

// Hàm kết nối WiFi
void connectWiFi() {
  WiFi.begin(config.ssid, config.password);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("WeatherStation");
    Serial.println("Access Point created");
    Serial.println("IP address: " + WiFi.softAPIP().toString());
  } else {
    Serial.println("Connected");
    Serial.println("IP address: " + WiFi.localIP().toString());
  }

  // Hiển thị IP ngay sau khi kết nối
  showIP = true;
  ipDisplayStartTime = millis();
  displayIPAddress();
}

// Hàm quét WiFi
String scanWiFiNetworks() {
  String wifiOptions = "";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    wifiOptions += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + " dBm)</option>";
  }
  return wifiOptions;
}

// Hàm cập nhật URL API
void updateApiUrls() {
  weatherUrl = "http://api.openweathermap.org/data/2.5/weather?q=" + String(config.city) + "&appid=" + String(weatherApiKey) + "&units=metric";
  forecastUrl = "http://api.openweathermap.org/data/2.5/forecast?q=" + String(config.city) + "&appid=" + String(weatherApiKey) + "&units=metric";
}

// Trang chủ web server
void handleRoot() {
  String ipAddress = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String wifiOptions = scanWiFiNetworks();
  
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Weather Station</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0 auto;padding:10px;background:#f0f0f0}";
  html += ".container{max-width:500px;margin:0 auto}";
  html += "h1{font-size:24px;color:#333}";
  html += ".tab{overflow:hidden;background:#e0e0e0}";
  html += ".tab button{background:#e0e0e0;border:none;padding:12px 16px;cursor:pointer;font-size:16px}";
  html += ".tab button:hover{background:#d0d0d0}";
  html += ".tab button.active{background:#c0c0c0}";
  html += ".tabcontent{padding:10px;border:1px solid #ccc;border-top:none;background:white}";
  html += "select,input,button{width:100%;padding:8px;margin:5px 0;border:1px solid #ccc;border-radius:4px}";
  html += "button{background:#4CAF50;color:white;border:none}";
  html += "button:hover{background:#45a049}";
  html += ".info-box{padding:10px;background:#e3f2fd;border-radius:4px;margin-bottom:10px}";
  html += "</style></head>";
  html += "<body><div class='container'>";
  html += "<h1>Weather Station Config</h1>";
  html += "<div class='info-box'>Current IP: " + ipAddress + "</div>";
  
  html += "<div class='tab'>";
  html += "<button class='tablinks' onclick=\"openTab('wifi')\">WiFi</button>";
  html += "<button class='tablinks' onclick=\"openTab('location')\">Location</button>";
  html += "<button class='tablinks' onclick=\"openTab('display')\">Display</button>";
  html += "</div>";
  
  // Tab WiFi
  html += "<div id='wifi' class='tabcontent'>";
  html += "<h2>WiFi Settings</h2>";
  html += "<form action='/savewifi' method='post'>";
  html += "<label>WiFi Network:</label>";
  html += "<select name='ssid'>" + wifiOptions + "</select>";
  html += "<label>Password:</label>";
  html += "<input type='password' name='password' placeholder='Enter WiFi password'>";
  html += "<button type='submit'>Save WiFi</button>";
  html += "</form>";
  html += "</div>";
  
  // Tab Location
  html += "<div id='location' class='tabcontent'>";
  html += "<h2>Location Settings</h2>";
  html += "<form action='/savelocation' method='post'>";
  html += "<label>City:</label>";
  html += "<input type='text' name='city' value='" + String(config.city) + "' placeholder='Enter city name'>";
  html += "<button type='submit'>Save Location</button>";
  html += "</form>";
  html += "</div>";
  
  // Tab Display
  html += "<div id='display' class='tabcontent'>";
  html += "<h2>Display Settings</h2>";
  html += "<form action='/savedisplay' method='post'>";
  html += "<label>Display Layout:</label>";
  html += "<select name='layout'>";
  html += "<option value='0'" + String(config.displayLayout == 0 ? " selected" : "") + ">Original Layout</option>";
  html += "<option value='1'" + String(config.displayLayout == 1 ? " selected" : "") + ">New Layout</option>";
  html += "</select>";
  html += "<button type='submit'>Save Display</button>";
  html += "</form>";
  html += "</div>";
  
  html += "<script>";
  html += "function openTab(tabName) {";
  html += "var i, tabcontent = document.getElementsByClassName('tabcontent'), tablinks = document.getElementsByClassName('tablinks');";
  html += "for(i=0;i<tabcontent.length;i++) tabcontent[i].style.display='none';";
  html += "for(i=0;i<tablinks.length;i++) tablinks[i].className=tablinks[i].className.replace(' active','');";
  html += "document.getElementById(tabName).style.display='block';";
  html += "event.currentTarget.className+=' active';}";
  html += "document.getElementsByClassName('tablinks')[0].click();";
  html += "</script>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

// Xử lý lưu cài đặt WiFi
void handleSaveWifi() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    strncpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid));
    strncpy(config.password, server.arg("password").c_str(), sizeof(config.password));
    saveConfig();
    
    server.send(200, "text/html", "<!DOCTYPE html><html><body><h1>WiFi Settings Saved</h1><p>Reconnecting...</p><script>setTimeout(() => location.href='/', 2000);</script></body></html>");
    
    WiFi.disconnect();
    connectWiFi();
  } else {
    server.send(400, "text/html", "<!DOCTYPE html><html><body><h1>Error</h1><p>Missing parameters</p><a href='/'>Back</a></body></html>");
  }
}

// Xử lý lưu cài đặt vị trí
void handleSaveLocation() {
  if (server.hasArg("city")) {
    strncpy(config.city, server.arg("city").c_str(), sizeof(config.city));
    saveConfig();
    updateApiUrls();
    getWeatherData();
    
    server.send(200, "text/html", "<!DOCTYPE html><html><body><h1>Location Saved</h1><p>Weather data updating...</p><script>setTimeout(() => location.href='/', 2000);</script></body></html>");
  } else {
    server.send(400, "text/html", "<!DOCTYPE html><html><body><h1>Error</h1><p>Missing city parameter</p><a href='/'>Back</a></body></html>");
  }
}

// Xử lý lưu cài đặt giao diện
void handleSaveDisplay() {
  if (server.hasArg("layout")) {
    config.displayLayout = server.arg("layout").toInt();
    saveConfig();
    
    server.send(200, "text/html", "<!DOCTYPE html><html><body><h1>Display Settings Saved</h1><p>Display updating...</p><script>setTimeout(() => location.href='/', 2000);</script></body></html>");
  } else {
    server.send(400, "text/html", "<!DOCTYPE html><html><body><h1>Error</h1><p>Missing layout parameter</p><a href='/'>Back</a></body></html>");
  }
}

void setup() {
  Serial.begin(115200);
  
  // Khởi tạo EEPROM và đọc cấu hình
  EEPROM.begin(sizeof(Config));
  loadConfig();
  
  // Khởi tạo I2C và OLED
  myWire.begin(26, 27);
  u8g2.setBusClock(400000);
  u8g2.begin();

  // Kết nối WiFi
  connectWiFi();
  
  // Cập nhật URL API
  updateApiUrls();

  // Khởi tạo NTP
  timeClient.begin();
  timeClient.update();
  setTime(timeClient.getEpochTime());

  // Cấu hình web server
  server.on("/", handleRoot);
  server.on("/savewifi", HTTP_POST, handleSaveWifi);
  server.on("/savelocation", HTTP_POST, handleSaveLocation);
  server.on("/savedisplay", HTTP_POST, handleSaveDisplay);
  server.begin();
  Serial.println("HTTP server started");

  // Lấy dữ liệu thời tiết lần đầu
  getWeatherData();
}

void loop() {
  server.handleClient();
  
  if (showIP && millis() - ipDisplayStartTime < IP_DISPLAY_DURATION) {
    displayIPAddress();
  } else {
    showIP = false;
    if (WiFi.status() == WL_CONNECTED) {
      timeClient.update();
      setTime(timeClient.getEpochTime());
      
      static unsigned long lastWeatherUpdate = 0;
      if (millis() - lastWeatherUpdate > 600000 || lastWeatherUpdate == 0) {
        getWeatherData();
        lastWeatherUpdate = millis();
      }

      displayOLED();
    } else {
      // Hiển thị thông báo khi mất kết nối WiFi
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(10, 20, "No WiFi connection");
      u8g2.drawStr(10, 35, "Connect to AP:");
      u8g2.drawStr(10, 50, "WeatherStation");
      u8g2.sendBuffer();
    }
  }
  
  delay(1000);
}

void getWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi connection - cannot get weather data");
    return;
  }

  HTTPClient http;

  // Lấy dữ liệu thời tiết hiện tại
  http.begin(weatherUrl);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    temp = doc["main"]["temp"];
    humidity = doc["main"]["humidity"];
    dewPoint = temp - ((100 - humidity) / 5.0);
  } else {
    Serial.println("Failed to get current weather data");
  }
  http.end();

  // Lấy dữ liệu dự báo
  http.begin(forecastUrl);
  httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);

    time_t now = timeClient.getEpochTime();
    time_t tomorrowNoon = now + 24 * 3600;
    struct tm *timeInfo = localtime(&tomorrowNoon);
    timeInfo->tm_hour = 12;
    timeInfo->tm_min = 0;
    timeInfo->tm_sec = 0;
    tomorrowNoon = mktime(timeInfo);

    JsonArray list = doc["list"];
    for (JsonObject forecast : list) {
      long forecastTime = forecast["dt"];
      if (abs(forecastTime - tomorrowNoon) < 3 * 3600) {
        tomorrowTemp = forecast["main"]["temp"];
        tomorrowHumidity = forecast["main"]["humidity"];
        tomorrowWeather = getVietnameseWeather(forecast["weather"][0]["main"].as<String>());
        Serial.println("Tomorrow Weather: " + tomorrowWeather); // Debug
        break;
      }
    }
  } else {
    Serial.println("Failed to get forecast data");
  }
  http.end();
}

void displayOLED() {
  u8g2.clearBuffer();
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);

  if (config.displayLayout == 0) {
    // Layout gốc: Hiển thị text trạng thái thời tiết
    String timeStr = timeClient.getFormattedTime();
    String dateStr = String(day()) + "." + String(month()) + "." + String(year() % 100);
    String timeDate = timeStr + " - " + dateStr;
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(7, 7, timeDate.c_str());

    u8g2.drawLine(7, 11, 119, 11);
    u8g2.drawLine(62, 17, 62, 55);

    // Hiển thị nhiệt độ và độ ẩm hiện tại
    u8g2.setFont(u8g2_font_helvB08_tr);
    String humStr = String(humidity, 1) + " %";
    String tempStr = String(temp, 1) + " C";
    u8g2.drawStr(21, 36, humStr.c_str());
    u8g2.drawStr(21, 25, tempStr.c_str());

    // Hiển thị dự báo ngày hôm sau
    String tomorrowHumStr = String(tomorrowHumidity, 1) + " %";
    String tomorrowTempStr = String(tomorrowTemp, 1) + " C";
    u8g2.drawStr(88, 36, tomorrowHumStr.c_str());
    u8g2.drawStr(88, 25, tomorrowTempStr.c_str());

    // Hiển thị thành phố và trạng thái thời tiết
    u8g2.setFont(u8g2_font_profont12_tr);
    u8g2.drawStr(7, 62, config.city);
    u8g2.drawStr(73, 62, tomorrowWeather.c_str());

    // Icon XBM
    u8g2.drawXBM(66, 17, 17, 17, image_weather_cloud_sunny_bits);
    u8g2.drawXBM(2, 17, 15, 17, image_menu_home_bits);

    u8g2.drawLine(62, 50, 62, 63);
    u8g2.drawLine(7, 42, 119, 42);
  } else {
    // Layout mới: Hiển thị icon thời tiết
    String timeStr = timeClient.getFormattedTime();
    String dateStr = String(day()) + "." + String(month()) + "." + String(year() % 100);
    String timeDate = timeStr + " - " + dateStr;
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(7, 7, timeDate.c_str());

    u8g2.drawXBM(6, 13, 16, 16, image_Layer_2_bits);

    u8g2.setFont(u8g2_font_helvB08_tr);
    String tempStr = String(temp, 1) + " C";
    String tomorrowTempStr = String(tomorrowTemp, 1) + " C";
    String humStr = String(humidity, 1) + " %";
    String tomorrowHumStr = String(tomorrowHumidity, 1) + " %";

    u8g2.drawStr(24, 24, tempStr.c_str());
    u8g2.drawStr(91, 24, tomorrowTempStr.c_str());
    u8g2.drawStr(23, 43, humStr.c_str());
    u8g2.drawStr(90, 43, tomorrowHumStr.c_str());

    u8g2.drawLine(0, 9, 127, 9);
    u8g2.drawLine(63, 13, 63, 63);

    u8g2.drawXBM(90, 48, 17, 16, getWeatherIcon(tomorrowWeather));

    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(3, 59, config.city);

    u8g2.drawXBM(72, 13, 16, 16, image_Layer_2_bits);
    u8g2.drawXBM(5, 30, 11, 16, image_Layer_12_bits);
    u8g2.drawXBM(71, 30, 11, 16, image_Layer_12_bits);

    u8g2.drawLine(0, 47, 127, 47);
    u8g2.drawLine(24, 29, 54, 29);
    u8g2.drawLine(91, 29, 121, 29);
  }

  u8g2.sendBuffer();
}