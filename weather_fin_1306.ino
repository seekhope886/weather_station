/*
 * 專案：ESP32-C3 Super Mini + ST7789 天氣工作站
 * 功能：從 Open-Meteo 抓取氣象資訊，並以向量圖形顯示於 128*64 SSD1306
 * AI自動從freeRTOS模式改回loop模式
 * 原來的weather_fin是彩色條列版
 * 優化：記憶體管理(失敗..無法採用getstream)、座標自動縮放、程式碼結構化
 * 2026.02.25
 * SSD1306黑白版本weather_fin_1306改自weather_fin_ai
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <OneButton.h>

// --- 硬體腳位定義 (ESP32-C3 Super Mini 建議) ---
#define TOUCH_PIN 2
#define BUZZER_PIN 3

// 建立物件
// SSD1306 128x64 I2C (可依你的接法改腳位)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);    //Low spped I2C
OneButton btn(TOUCH_PIN, true);

// 設定參數
const float myLat = 25.04;
const float myLon = 121.56;
const float iconScale = 2.5; // 圖示縮放倍率

// --- 向量天氣圖示繪製函式 (優化縮放與顏色) ---

// 太陽：橘色圓心與黃色光芒
void drawSun(int x, int y, float s, bool flag) {
    int cx = x + (16 * s), cy = y + (16 * s);
    
    if(!flag){u8g2.drawCircle(cx, cy, 6 * s, U8G2_DRAW_ALL);} else {u8g2.drawDisc(cx, cy, 6 * s);};             
    for (int i = 0; i < 360; i += 45) {
        float rad = i * PI / 180;
        u8g2.drawLine(cx + cos(rad)*8*s, cy + sin(rad)*8*s, 
                     cx + cos(rad)*13*s, cy + sin(rad)*13*s);
    }
}

// 雲朵：多個圓形組合
void drawCloud(int x, int y, float s) {
    u8g2.drawDisc(x + 10*s, y + 18*s, 5*s);
    u8g2.drawDisc(x + 16*s, y + 14*s, 7*s);
    u8g2.drawDisc(x + 22*s, y + 18*s, 5*s);
    u8g2.drawBox(x + 10*s, y + 18*s, 13*s, 5*s+1);
}

// 雨滴
void drawRain(int x, int y, float s, int count) {
    for (int i = 0; i < count; i++) {
        int rx = x + (10*s) + (i*6*s);
        int ry = y + (24*s);
        u8g2.drawLine(rx, ry, rx - 2*s, ry + 5*s);
    }
}

// 雪花
void drawSnowFlakes(int x, int y, float s) {
  u8g2.drawPixel(x+12*s, y+25*s);
  u8g2.drawPixel(x+16*s, y+27*s);
  u8g2.drawPixel(x+20*s, y+25*s);
}

// 閃電
void drawLightning(int x, int y, float s) {
    int cx = x + 16*s, cy = y + 18*s;
    u8g2.drawLine(cx, cy, cx - 3*s, cy + 5*s);
    u8g2.drawLine(cx - 3*s, cy + 5*s, cx + 1*s, cy + 5*s);
    u8g2.drawLine(cx + 1*s, cy + 5*s, cx - 2*s, cy + 10*s);
}

// 霧線
void drawFogLines(int x, int y, float s) {
  for (int i = 0; i < 3; i++) {
    u8g2.drawLine(x + 8* s, y +( 14 + i * 4)* s, 16* s, y +( 14 + i * 4)* s);
  }
}

// --- 核心邏輯：根據 WMO 代碼顯示天氣 ---
void renderWeatherIcon(int code, int x, int y, float s) {
    switch (code) {
        case 0: // 晴
            drawSun(x, y, s,true); 
            break;
        case 1: case 2: // 多雲時晴
            drawSun(x + 5*s, y - 3*s, s,false);
            drawCloud(x, y, s);
            break;
        case 3: // 陰
            drawCloud(x, y, s);
            break;
        case 45: case 48: // Fog
            drawFogLines(x, y, s);
            break;
        case 51: case 53: case 55:
        case 61: case 63: case 65: // 雨
            drawCloud(x, y, s);
            drawRain(x, y, s, (code % 10 == 5) ? 3 : 2);
            break;
        case 56: case 57: // Freezing Drizzle
        case 66: case 67: // Freezing Rain
        case 71: case 73: case 75: // Snow fall
        case 77: // Snow grains
            drawCloud(x, y, s);
            drawSnowFlakes(x, y, s);
            break;
    case 80: case 81: case 82: // Rain showers
      drawSun(x + (4 * s), y - (4 * s), s,false);
      drawCloud(x, y, s);
      drawRain(x, y, s, 2);
      break;

    case 85: case 86: // Snow showers
      drawSun(x + (4 * s), y - (4 * s), s,false);
      drawCloud(x, y, s);
      drawSnowFlakes(x, y, s);
      break;
            
        case 95: case 96: case 99: // 雷雨
            drawCloud(x, y, s);
            drawLightning(x, y, s);
            break;
        default:
            drawCloud(x, y, s); // 預設顯示雲朵
            break;
    }
}

// 蜂鳴器回饋
void playBeep() {
    tone(BUZZER_PIN, 4000, 50); // 頻率 4KHz, 持續 50ms
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.clearBuffer();
    u8g2.setCursor(10, 32);
    u8g2.print("Connecting WiFi...");
    u8g2.sendBuffer();
    delay(2000);
    // WiFiManager 自動配網
    WiFi.begin();
    WiFi.mode(WIFI_STA); // Wi-Fi設置成STA模式；預設模式為STA+AP
    WiFi.setTxPower(WIFI_POWER_8_5dBm);     
    WiFiManager wm;
    if (!wm.autoConnect("ESP32_Weather_AP")) {
        Serial.println("連線失敗，重啟中...");
        ESP.restart();
    }
    
    u8g2.clearBuffer();
    u8g2.setCursor(10, 32);
    u8g2.print("WiFi Connected!");
    u8g2.sendBuffer();
    delay(2000);
    playBeep();

    // 綁定按鈕事件
    btn.attachClick(playBeep);
}

void loop() {
    btn.tick();
    static uint32_t lastUpdate = 0;

    // 每 15 分鐘更新一次
    if (millis() - lastUpdate > 900000 || lastUpdate == 0) {
      
        updateWeather();
        lastUpdate = millis();
    }
}

void updateWeather() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
String url = String("https://api.open-meteo.com/v1/forecast") 
           + "?latitude=" + String(myLat) 
           + "&longitude=" + String(myLon)
           + "&daily=sunrise,sunset"    //取得三天的日昇日落時間
           + "&current=temperature_2m,relative_humidity_2m,windspeed,weather_code"   //取得現在的溫溼度
           + "&daily=temperature_2m_max,temperature_2m_min,weather_code"  //取得三天的高低溫及天候代碼
           + "&forecast_days=4"
           + "&timezone=Asia/Taipei";
    
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
                playBeep();  
                String payload = http.getString();
        // 使用 ArduinoJson 7 處理大資料量
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          
  Serial.println("--- 完整的 JSON 結構如下 ---");
  serializeJsonPretty(doc, Serial); 
  Serial.println("\n---------------------------");

            float temp = doc["current"]["temperature_2m"];
            int humi = doc["current"]["relative_humidity_2m"];
            int code = doc["current"]["weather_code"];

            // 繪製畫面
            u8g2.clearBuffer();
//=========================================================
// --- A. 顯示「當前」天氣 (上方) ---
            // 顯示圖示 (置中顯示)
            renderWeatherIcon(code, 68, 10, 1.2);//0,1,51,65,80,85 

            // 顯示數值
            u8g2.setFont(u8g2_font_5x7_tf);
            u8g2.setCursor(0, 10);
            u8g2.print(doc["current"]["time"].as<const char*>());//更新時間
            
            u8g2.setCursor(0, 18);
            u8g2.printf("%.1f-%.1f C", doc["daily"]["temperature_2m_min"][0].as<float>(), doc["daily"]["temperature_2m_max"][0].as<float>());//當日最低高溫
            
            u8g2.setFont(u8g2_font_logisoso16_tr);//15*23
            u8g2.setCursor(0, 38);
            u8g2.printf("%.1fC", temp);//現在溫度
            
            u8g2.setFont(u8g2_font_5x7_tf);
            u8g2.setCursor(0, 47);
            u8g2.printf("H:%.0f%% W:%.0fKm", doc["current"]["relative_humidity_2m"].as<float>(),doc["current"]["windspeed"].as<float>());//現在濕度            
            // 1. 取得完整時區字串 (例如 "Asia/Taipei")
            String fullTZ = doc["timezone"].as<String>();
            // 2. 尋找最後一個 '/' 的位置
            int slashIndex = fullTZ.lastIndexOf('/');
            String city = (slashIndex != -1) ? fullTZ.substring(slashIndex + 1) : fullTZ;
            u8g2.setCursor(83, 10);
            u8g2.print(city); //地點
            
//=========================================================
// --- B. 顯示「未來三天」預報 (下方) ---
        u8g2.drawLine(0, 50, 127, 50); // 分隔線

        for (int i = 0; i < 3; i++) {
            int baseX = i * 43; // 每格寬度 43
            int dCode = doc["daily"]["weather_code"][i+1];
            float maxT = doc["daily"]["temperature_2m_max"][i+1];
            float minT = doc["daily"]["temperature_2m_min"][i+1];
//            const char* date = doc["daily"]["time"][i+1];

            // 顯示小圖示
            renderWeatherIcon(dCode, baseX, 52, .4); 
            
            // 顯示日期 (取月/日)
//            u8g2.setCursor(baseX + 15, 180);
//            u8g2.printf("%s", String(date).substring(5).c_str());
            
            // 顯示高低溫
            u8g2.setCursor(baseX+14, 63);
            u8g2.printf("%.0f/%.0f", minT, maxT);
        }
//=========================================================

            u8g2.sendBuffer();            
            Serial.println("天氣資訊已更新");
        }else {
                Serial.printf("JSON 解碼失敗");
            }
    }
    http.end();
}
