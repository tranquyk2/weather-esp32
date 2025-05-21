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
#include <ESPmDNS.h>
#include <Update.h>

// Cấu trúc lưu trữ cấu hình trong EEPROM
struct Config {
  char ssid[32];
  char password[32];
  char city[32];
  bool configured;
  int displayLayout; // 0: Original, 1: New layout, 2: Clock layout
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
  0x80,0x00,0x48,0x01,0x38,0x02,0x98,0x04,0x48,0x09,0x24,0x12,0x12,0x24,0x09,0x48,
  0x66,0x30,0x64,0x10,0x04,0x17,0x04,0x15,0x04,0x17,0x04,0x15,0xfc,0x1f,0x00,0x00
};

// Dữ liệu ảnh XBM cho layout mới
static const unsigned char image_Layer_12_bits[] = {
  0x20,0x00,0x20,0x00,0x30,0x00,0x50,0x00,0x48,0x00,0x88,0x00,0x04,0x01,0x04,0x01,
  0x82,0x02,0x02,0x03,0x01,0x05,0x01,0x04,0x02,0x02,0x02,0x02,0x0c,0x01,0xf0,0x00
};

static const unsigned char image_Layer_2_bits[] = {
  0x38,0x00,0x44,0x40,0xd4,0xa0,0x54,0x40,0xd4,0x1c,0x54,0x06,0x54,0x02,0x54,0x02,
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

static const unsigned char image_Battery_bits[] = {
  0xfe,0xff,0x7f,0x00,0x01,0x00,0x80,0x00,0x01,0x00,0x80,0x03,0x01,0x00,0x80,0x02,
  0x01,0x00,0x80,0x02,0x01,0x00,0x80,0x03,0x01,0x00,0x80,0x00,0xfe,0xff,0x7f,0x00};
static const unsigned char image_Layer_57_bits[] = {0x03,0x03};

static const unsigned char image_Background_bits[] = {
  0xfe,0xff,0xff,0xff,0x7f,0x00,0x00,0x80,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01,
  0xff,0xff,0xff,0xff,0xff,0x00,0x00,0xc0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x03,
  0x01,0x00,0x00,0x00,0x80,0x01,0x00,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,
  0xad,0xff,0xff,0xff,0x36,0xff,0xff,0x3f,0xfe,0xbf,0xaa,0xff,0xff,0xff,0xff,0x7c,
  0x01,0x00,0x00,0x00,0x40,0xfe,0xff,0x1f,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xf9,
  0xd5,0xff,0xff,0xff,0x9f,0x00,0x00,0x80,0xfc,0xff,0x7f,0x55,0xff,0xff,0x7f,0x82,
  0x01,0x00,0x00,0x00,0x20,0xff,0x57,0x6d,0x02,0x00,0x00,0x00,0x00,0x00,0x80,0xbc,
  0xfe,0xff,0xff,0xff,0x47,0x00,0x00,0x00,0xf1,0xff,0xff,0xff,0xff,0xff,0x3f,0x81,
  0x00,0x00,0x00,0x00,0x8c,0xaa,0xed,0xff,0x18,0x00,0x00,0x00,0x00,0x00,0x60,0xbe,
  0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0xc0,0x80,
  0x00,0x00,0x00,0x00,0xf0,0xff,0xff,0xff,0x07,0x00,0x00,0x00,0x00,0x00,0x80,0x7f};
static const unsigned char image_Background_1_bits[] = {
  0xfe,0x01,0x00,0x00,0x00,0x00,0x00,0xe0,0xff,0xff,0xff,0x0f,0x00,0x00,0x00,0x00,
  0x01,0x03,0x00,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,
  0x7d,0x06,0x00,0x00,0x00,0x00,0x00,0x18,0xff,0xb7,0x55,0x31,0x00,0x00,0x00,0x00,
  0x81,0xfc,0xff,0xff,0xff,0xff,0xff,0x8f,0x00,0x00,0x00,0xe2,0xff,0xff,0xff,0x7f,
  0x3d,0x01,0x00,0x00,0x00,0x00,0x00,0x40,0xb6,0xea,0xff,0x04,0x00,0x00,0x00,0x80,
  0x41,0xfe,0xff,0xff,0xaa,0xfe,0xff,0x3f,0x01,0x00,0x00,0xf9,0xff,0xff,0xff,0xab,
  0x9f,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xf8,0xff,0x7f,0x02,0x00,0x00,0x00,0x80,
  0x3e,0xff,0xff,0xff,0xff,0x55,0xfd,0x7f,0xfc,0xff,0xff,0x6c,0xff,0xff,0xff,0xb5,
  0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x80,0x01,0x00,0x00,0x00,0x80,
  0xc0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0xff,
  0x80,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01,0x00,0x00,0xfe,0xff,0xff,0xff,0x7f};
static const unsigned char image_EviSmile1_bits[] = {
  0x0c,0xc0,0x00,0x06,0x80,0x01,0x07,0x80,0x03,0xcf,0xcf,0x03,0xff,0xff,0x03,0xff,
  0xff,0x03,0xfe,0xff,0x01,0xfe,0xff,0x01,0xfe,0xff,0x01,0xf7,0xbf,0x03,0xe7,0x9f,
  0x03,0xc7,0x8f,0x03,0x87,0x87,0x03,0x8f,0xc7,0x03,0xff,0xff,0x03,0xfe,0xff,0x01,
  0xde,0xef,0x01,0xbc,0xf4,0x00,0x78,0x78,0x00,0xf0,0x3f,0x00,0xc0,0x0f,0x00};

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

// Biến cho OTA
const char* host = "weatherstation";
String updateStatus = "";
bool isUpdating = false;

// Trang HTML cho đăng nhập OTA
const char* loginIndex =
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>Weather Station OTA Login</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td>Username:</td>"
            "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/ota')"
    "}"
    "else"
    "{"
    " alert('Sai Username hoặc Password')"
    "}"
    "}"
"</script>";

// Trang HTML cho giao diện OTA
const char* otaIndex =
"<script src='https://cdnjs.cloudflare.com/ajax/libs/jquery/3.6.0/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
    "<table width='100%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td align='center'><input type='file' name='update'></td>"
        "</tr>"
        "<tr>"
            "<td align='center'><input type='submit' value='Cập nhật'></td>"
        "</tr>"
        "<tr>"
            "<td align='center'><div id='prg'></div></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    " $.ajax({"
    "url: '/update',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "var progress = Math.round(per*100);"
    "$('#prg').html('Tiến trình: ' + progress + '%');"
    "$('#prg').css('width', progress + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('Thành công!')"
    "},"
    "error: function (a, b, c) {"
    "console.log('Lỗi!')"
    "}"
    "});"
    "});"
"</script>";

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
  if (weather == "troi nang") return image_weather_cloud_sunny_bits;
  return image_weather_cloud_sunny_bits;
}

// Hàm đọc cấu hình từ EEPROM
void loadConfig() {
  EEPROM.begin(sizeof(Config));
  EEPROM.get(0, config);
  EEPROM.end();
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
  u8g2.setFontMode(0);
  u8g2.setBitmapMode(1);
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

// Hàm hiển thị trạng thái OTA trên OLED
void displayOTAStatus(String status, int progress = 0, String filename = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont11_tr);
  u8g2.setFontMode(0);
  u8g2.setBitmapMode(1);
  u8g2.drawStr(10, 20, "OTA Update");
  u8g2.drawStr(10, 35, status.c_str());
  if (filename != "") {
    u8g2.drawStr(10, 50, ("File: " + filename.substring(0, 12)).c_str());
  }
  if (progress > 0) {
    u8g2.drawFrame(10, 55, 108, 8);
    u8g2.drawBox(10, 55, progress * 108 / 100, 8);
    u8g2.setFont(u8g2_font_profont10_tr);
    u8g2.drawStr(50, 63, (String(progress) + "%").c_str());
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
  html += "<button class='tablinks' onclick=\"openTab('ota')\">OTA Update</button>";
  html += "</div>";
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
  html += "<div id='location' class='tabcontent'>";
  html += "<h2>Location Settings</h2>";
  html += "<form action='/savelocation' method='post'>";
  html += "<label>City:</label>";
  html += "<input type='text' name='city' value='" + String(config.city) + "' placeholder='Enter city name'>";
  html += "<button type='submit'>Save Location</button>";
  html += "</form>";
  html += "</div>";
  html += "<div id='display' class='tabcontent'>";
  html += "<h2>Display Settings</h2>";
  html += "<form action='/savedisplay' method='post'>";
  html += "<label>Display Layout:</label>";
  html += "<select name='layout'>";
  html += "<option value='0'" + String(config.displayLayout == 0 ? " selected" : "") + ">Original Layout</option>";
  html += "<option value='1'" + String(config.displayLayout == 1 ? " selected" : "") + ">New Layout</option>";
  html += "<option value='2'" + String(config.displayLayout == 2 ? " selected" : "") + ">Clock Layout</option>";
  html += "<option value='3'" + String(config.displayLayout == 3 ? " selected" : "") + ">Clock 2</option>";
  html += "<option value='4'" + String(config.displayLayout == 4 ? " selected" : "") + ">Clock 3</option>";
  html += "</select>";
  html += "<button type='submit'>Save Display</button>";
  html += "</form>";
  html += "</div>";
  html += "<div id='ota' class='tabcontent'>";
  html += "<h2>OTA Update</h2>";
  html += "<form action='/otalogin'>";
  html += "<button type='submit'>Go to OTA Login</button>";
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

// Xử lý trang đăng nhập OTA
void handleOTALogin() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", loginIndex);
  displayOTAStatus("OTA Login Page");
}

// Xử lý trang OTA
void handleOTA() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", otaIndex);
  displayOTAStatus("OTA Upload Page", 0, "Ready");
}

// Xử lý cập nhật OTA
void handleOTAUpdate() {
  server.sendHeader("Connection", "close");
  bool updateSuccess = !Update.hasError();
  server.send(200, "text/plain", updateSuccess ? "Cập nhật THÀNH CÔNG! Thiết bị đang khởi động lại..." : "Cập nhật THẤT BẠI!");
  displayOTAStatus(updateSuccess ? "THANH CONG!" : "THAT BAI!");
  if (updateSuccess) {
    displayOTAStatus("Dang khoi dong lai...");
    delay(1000);
    ESP.restart();
  }
}

// Hàm lấy dữ liệu thời tiết
void getWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi connection - cannot get weather data");
    return;
  }
  HTTPClient http;
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
        Serial.println("Tomorrow Weather: " + tomorrowWeather);
        break;
      }
    }
  } else {
    Serial.println("Failed to get forecast data");
  }
  http.end();
}

// Hàm hiển thị trên OLED
void displayOLED() {
  u8g2.clearBuffer();
  u8g2.setFontMode(0);
  u8g2.setBitmapMode(1);

  if (config.displayLayout == 0) {
    String timeStr = timeClient.getFormattedTime();
    String dateStr = String(day()) + "." + String(month()) + "." + String(year() % 100);
    String timeDate = timeStr + " - " + dateStr;
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(7, 7, timeDate.c_str());
    u8g2.drawLine(7, 11, 119, 11);
    u8g2.drawLine(62, 17, 62, 55);
    u8g2.setFont(u8g2_font_helvB08_tr);
    String humStr = String(humidity, 1) + " %";
    String tempStr = String(temp, 1) + " C";
    u8g2.drawStr(21, 36, humStr.c_str());
    u8g2.drawStr(21, 25, tempStr.c_str());
    String tomorrowHumStr = String(tomorrowHumidity, 1) + " %";
    String tomorrowTempStr = String(tomorrowTemp, 1) + " C";
    u8g2.drawStr(88, 36, tomorrowHumStr.c_str());
    u8g2.drawStr(88, 25, tomorrowTempStr.c_str());
    u8g2.setFont(u8g2_font_profont12_tr);
    u8g2.drawStr(7, 62, config.city);
    u8g2.drawStr(73, 62, tomorrowWeather.c_str());
    u8g2.drawXBM(66, 17, 17, 17, getWeatherIcon(tomorrowWeather));
    u8g2.drawXBM(2, 17, 15, 17, image_menu_home_bits);
    u8g2.drawLine(62, 50, 62, 63);
    u8g2.drawLine(7, 42, 119, 42);
  } else if (config.displayLayout == 1) {
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
  } else if (config.displayLayout == 2) {
    // Giao diện đồng hồ mới
    String timeStr = timeClient.getFormattedTime().substring(0, 5); // Lấy HH:MM
    u8g2.setFontMode(1); // Trong suốt để phù hợp với đồng hồ
    u8g2.setFont(u8g2_font_timR24_tr);
    u8g2.drawStr(25, 43, timeStr.c_str());
  } else if (config.displayLayout == 3) {
    u8g2.clearBuffer();
    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);

    // Hiển thị thời gian
    String timeStr = timeClient.getFormattedTime().substring(0, 5); // Lấy HH:MM
    u8g2.setFont(u8g2_font_profont29_tr);
    u8g2.drawStr(19, 40, timeStr.c_str());

    // Hiển thị giây
    u8g2.setFont(u8g2_font_profont17_tr);
    u8g2.drawStr(101, 40, timeClient.getFormattedTime().substring(6, 8).c_str());

    // Hiển thị ngày
    String dateStr = String(day()) + "/" + String(month()) + "/" + String(year());
    u8g2.setFont(u8g2_font_profont10_tr);
    u8g2.drawStr(12, 56, dateStr.c_str());

    // Hiển thị ngày trong tuần
    u8g2.setDrawColor(2);
    u8g2.setFont(u8g2_font_helvB08_tr);
    String dayOfWeek = String(timeClient.getDay());
    switch (timeClient.getDay()) {
      case 0: dayOfWeek = "Sun"; break;
      case 1: dayOfWeek = "Mon"; break;
      case 2: dayOfWeek = "Tue"; break;
      case 3: dayOfWeek = "Wed"; break;
      case 4: dayOfWeek = "Thu"; break;
      case 5: dayOfWeek = "Fri"; break;
      case 6: dayOfWeek = "Sat"; break;
    }
    u8g2.drawStr(73, 53, dayOfWeek.c_str());

    // Vẽ các đường line, frame, box, và hình XBM như bạn cung cấp
    u8g2.setDrawColor(1);
    u8g2.drawLine(13, 23, 21, 15);
    u8g2.drawLine(21, 15, 99, 15);
    u8g2.drawLine(99, 15, 109, 23);
    u8g2.drawLine(109, 23, 119, 23);
    u8g2.drawLine(119, 23, 122, 26);
    u8g2.drawLine(122, 26, 122, 48);
    u8g2.drawLine(122, 48, 116, 54);
    u8g2.drawLine(115, 54, 68, 54);
    u8g2.drawLine(13, 24, 13, 44);
    u8g2.drawLine(14, 45, 58, 45);
    u8g2.drawLine(59, 45, 68, 54);
    u8g2.drawLine(96, 43, 107, 54);
    u8g2.drawLine(58, 45, 60, 43);
    u8g2.drawLine(61, 43, 95, 43);
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.setDrawColor(2);
    u8g2.drawBox(68, 44, 30, 10);
    u8g2.setDrawColor(1);
    u8g2.drawBox(60, 43, 9, 4);
    u8g2.drawFrame(64, 49, 7, 2);
    u8g2.drawBox(62, 47, 6, 2);
    u8g2.drawFrame(64, 49, 4, 2);
    u8g2.drawFrame(66, 51, 3, 2);
    u8g2.drawFrame(66, 51, 7, 2);
    u8g2.drawFrame(97, 53, 10, 2);
    u8g2.drawFrame(95, 51, 10, 2);
    u8g2.drawFrame(99, 50, 4, 2);
    u8g2.drawFrame(97, 49, 6, 2);
    u8g2.drawFrame(97, 47, 4, 2);
    u8g2.drawFrame(95, 45, 4, 2);
    u8g2.drawEllipse(5, 5, 2, 2);
    u8g2.drawLine(5, 3, 5, 7);
    u8g2.drawEllipse(122, 5, 2, 2);
    u8g2.drawEllipse(5, 58, 2, 2);
    u8g2.drawEllipse(122, 58, 2, 2);
    u8g2.drawLine(120, 5, 123, 5);
    u8g2.drawLine(122, 56, 122, 59);
    u8g2.drawLine(3, 58, 6, 58);
    u8g2.drawXBM(89, 4, 26, 8, image_Battery_bits);
    u8g2.drawBox(91, 6, 20, 4);
    u8g2.drawFrame(11, 9, 51, 2);
    u8g2.drawBox(62, 9, 25, 5);
    u8g2.drawLine(59, 11, 61, 13);
    u8g2.drawXBM(60, 11, 2, 2, image_Layer_57_bits);
    u8g2.drawLine(55, 13, 4, 13);
    u8g2.drawLine(16, 15, 4, 15);
    u8g2.drawLine(13, 17, 4, 17);
    u8g2.drawLine(9, 19, 4, 19);
    u8g2.drawBox(11, 4, 2, 4);
    u8g2.setDrawColor(2);
    u8g2.drawRBox(98, 26, 22, 17, 5);
    u8g2.setDrawColor(1);
    u8g2.drawLine(86, 3, 86, 13);
    u8g2.drawLine(68, 54, 63, 59);
    u8g2.drawLine(7, 51, 7, 55);
    u8g2.drawLine(11, 59, 63, 59);
    u8g2.drawLine(13, 45, 7, 51);
    u8g2.drawLine(7, 55, 11, 59);

  }else if (config.displayLayout == 4) {
    u8g2.clearBuffer();
    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);

    // Hiển thị thời gian
    String timeStr = timeClient.getFormattedTime().substring(0, 5); // Lấy HH:MM
    u8g2.setFont(u8g2_font_profont29_tr);
    u8g2.drawStr(27, 41, timeStr.substring(0, 2).c_str()); // Giờ
    u8g2.drawStr(57, 39, ":"); // Dấu hai chấm
    u8g2.drawStr(70, 41, timeStr.substring(3, 5).c_str()); // Phút

    // Hiển thị giây
    u8g2.setFont(u8g2_font_profont15_tr);
    u8g2.drawStr(110, 44, timeClient.getFormattedTime().substring(6, 8).c_str());

    // Hiển thị AM/PM
    u8g2.setFont(u8g2_font_haxrcorp4089_tr);
    u8g2.drawStr(110, 32, timeClient.getHours() >= 12 ? "PM" : "AM");

    // Vẽ các đường kẻ
    u8g2.drawLine(27, 44, 40, 44);
    u8g2.drawLine(43, 44, 56, 44);
    u8g2.drawLine(70, 44, 83, 44);
    u8g2.drawLine(86, 44, 99, 44);
    u8g2.drawLine(110, 22, 122, 22);

    // Vẽ hình ảnh XBM
    u8g2.drawXBM(0, 0, 128, 11, image_Background_bits);
    u8g2.drawXBM(-2, 53, 128, 11, image_Background_1_bits);
    u8g2.drawXBM(4, 23, 18, 21, image_EviSmile1_bits);

  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(sizeof(Config));
  loadConfig();
  myWire.begin(26, 27);
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setFontMode(0);
  u8g2.setBitmapMode(1);
  connectWiFi();
  if (MDNS.begin(host)) {
    Serial.println("mDNS started: http://" + String(host) + ".local");
  }
  updateApiUrls();
  timeClient.begin();
  timeClient.update();
  setTime(timeClient.getEpochTime());
  server.on("/", handleRoot);
  server.on("/savewifi", HTTP_POST, handleSaveWifi);
  server.on("/savelocation", HTTP_POST, handleSaveLocation);
  server.on("/savedisplay", HTTP_POST, handleSaveDisplay);
  server.on("/otalogin", HTTP_GET, handleOTALogin);
  server.on("/ota", HTTP_GET, handleOTA);
  server.on("/update", HTTP_POST, handleOTAUpdate, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Cập nhật: %s\n", upload.filename.c_str());
      isUpdating = true;
      updateStatus = "Dang cap nhat...";
      displayOTAStatus("Dang cap nhat...", 0, upload.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        updateStatus = "Loi: Khong the bat dau!";
        displayOTAStatus(updateStatus);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      int progress = (upload.totalSize > 0) ? (upload.currentSize * 100 / upload.totalSize) : 0;
      if (progress % 10 == 0) {
        displayOTAStatus("Dang cap nhat...", progress, upload.filename);
      }
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
        updateStatus = "Loi: Ghi du lieu!";
        displayOTAStatus(updateStatus);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("Cập nhật thành công: %u bytes\n", upload.totalSize);
        updateStatus = "Cap nhat thanh cong!";
        displayOTAStatus(updateStatus, 100, upload.filename);
      } else {
        Update.printError(Serial);
        updateStatus = "Loi: Hoan tat cap nhat!";
        displayOTAStatus(updateStatus);
      }
      isUpdating = false;
    }
  });
  server.begin();
  Serial.println("HTTP server started");
  getWeatherData();
}

void loop() {
  server.handleClient();
  if (isUpdating) {
    return;
  }
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
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.setFontMode(0);
      u8g2.setBitmapMode(1);
      u8g2.drawStr(10, 20, "No WiFi connection");
      u8g2.drawStr(10, 35, "Connect to AP:");
      u8g2.drawStr(10, 50, "WeatherStation");
      u8g2.sendBuffer();
    }
  }
  delay(1000);
}
