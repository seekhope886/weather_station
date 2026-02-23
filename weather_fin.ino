/*
 *   esp32 c3 super mini + ST7789 240*240 TFT 
 *   collect weather data from Open-Meteo then show on ST7789
 *   2026.02.23
 *    
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <OneButton.h>

// 定義腳位
#define TFT_CS    8
#define TFT_DC    9
#define TFT_RST   0
#define TOUCH_PIN 2
#define BUZZER_PIN 3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
OneButton btn(TOUCH_PIN, true);

float myLat = 25.04;  // 台北緯度
float myLon = 121.56; // 台北經度

int   iconrate = 2;   //32*32圖示的倍率,2=64*64

// 任務控制
TaskHandle_t weatherTaskHandle;

// 模擬圖示繪製 (簡化版)
void drawWeatherIcon(int x, int y, String weatherType) {
    if (weatherType == "Sunny") {
      uint16_t color = ST77XX_YELLOW;
      // 中心圓形 (半徑 7)
      tft.fillCircle(x + 16*iconrate, y + 16*iconrate, 7*iconrate, color);
      // 八個方向的光芒
      for (int i = 0; i < 360; i += 45) {
        float angle = i * PI / 180;
        int x1 = x + 16*iconrate + cos(angle) * 10*iconrate;
        int y1 = y + 16*iconrate + sin(angle) * 10*iconrate;
        int x2 = x + 16*iconrate + cos(angle) * 15*iconrate;
        int y2 = y + 16*iconrate + sin(angle) * 15*iconrate;
        tft.drawLine(x1, y1, x2, y2, color);
        if(iconrate != 1){tft.drawLine(x1 + 1, y1, x2 + 1, y2, color);}
        }
    }
    if (weatherType == "Cloud") {
        uint16_t color = 0xAD55;
        // 組合三個圓形形成雲朵
        tft.fillCircle(x + 10*iconrate, y + 20*iconrate, 6*iconrate, color); // 左小圓
        tft.fillCircle(x + 16*iconrate, y + 16*iconrate, 8*iconrate, color); // 中大圓
        tft.fillCircle(x + 23*iconrate, y + 20*iconrate, 6*iconrate, color); // 右小圓
        // 底部填平
        tft.fillRect(x + 10*iconrate, y + 20*iconrate, 13*iconrate, 6*iconrate, color); 
    }
    if (weatherType == "Rain") {
        uint16_t color = 0x7BEF;
        // 組合三個圓形形成雲朵
        tft.fillCircle(x + 10*iconrate, y + 20*iconrate-5, 6*iconrate, color); // 左小圓
        tft.fillCircle(x + 16*iconrate, y + 16*iconrate-5, 8*iconrate, color); // 中大圓
        tft.fillCircle(x + 23*iconrate, y + 20*iconrate-5, 6*iconrate, color); // 右小圓
        // 底部填平
        tft.fillRect(x + 10*iconrate, y + 20*iconrate-5, 13*iconrate, 6*iconrate, color); 
        uint16_t rainColor = 0x5DFF; // 淺藍色
        // 畫三行雨滴
        for (int i = 0; i < 3; i++) {
          int rx = x + 12*iconrate + (i * 5);
          int ry = y + 26*iconrate;
          tft.drawLine(rx, ry, rx - 2*iconrate, ry + 4*iconrate, rainColor);
        }
          
    }
    
}

// 蜂鳴器提示音
void playBeep() {
    tone(BUZZER_PIN, 4000);
    vTaskDelay(pdMS_TO_TICKS(100));
    noTone(BUZZER_PIN);
}

// 點擊事件處理
void handleClick() {
    playBeep();
}

// 從 Open-Meteo 獲取資料
void fetchWeather(void *pvParameters) {
    while (1) {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            // 以台北為例：緯度 25.04, 經度 121.53
            
String url = String("https://api.open-meteo.com/v1/forecast") 
           + "?latitude=" + String(myLat) 
           + "&longitude=" + String(myLon)
           + "&daily=sunrise,sunset"    //取得三天的日昇日落時間
           + "&current=temperature_2m,relative_humidity_2m,windspeed,weather_code"   //取得現在的溫溼度
           + "&daily=temperature_2m_max,temperature_2m_min,weather_code"  //取得三天的高低溫及天候代碼
           + "&forecast_days=4"
           + "&timezone=Asia/Taipei";

/*
String url = String("https://api.open-meteo.com/v1/forecast") 
           + "?latitude=" + String(myLat) 
           + "&longitude=" + String(myLon)
           + "&current_weather=true"
           + "&current=temperature_2m,relative_humidity_2m"
           + "&forecast_days=3"
           + "&timezone=Asia/Taipei";
*/
//String url ="https://api.open-meteo.com/v1/forecast?latitude=25.0375&longitude=121.5654&hourly=temperature_2m,relative_humidity_2m,precipitation_probability,weather_code&daily=temperature_2m_max,temperature_2m_min,weather_code&timezone=Asia/Taipei&forecast_days=1";

                       
            http.begin(url);
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                playBeep();  
//    int totalLength = http.getSize(); // 取得 Content-Length
//    Serial.print("Total Content Length: ");
//    Serial.println(totalLength);
              
                String payload = http.getString();
    int len = payload.length(); // 取得字串總長度
    Serial.print("Payload Length: ");
    Serial.println(len);
                JsonDocument doc;
//                DynamicJsonDocument doc(3072); 
                DeserializationError error = deserializeJson(doc, payload);

                    if (error) {
                    Serial.print(F("JSON 解析失敗: "));
                    Serial.println(error.f_str());
                      
                    } else {
  Serial.println("--- 完整的 JSON 結構如下 ---");
  serializeJsonPretty(doc, Serial); 
  Serial.println("\n---------------------------");
                     
//  if (!doc["current"]["time"].is<JsonArray>() || !doc["current"]["temperature_2m"].is<JsonArray>() || !doc["current"]["relative_humidity_2m"].is<JsonArray>()) {
//    Serial.println("錯誤: 找不到氣象數據欄位");
    
//  } else {
                    
                // 更新顯示畫面邏輯
                tft.fillScreen(ST77XX_BLACK);
                tft.setTextSize(2);
                tft.setCursor(10, 10);
                tft.setTextColor(ST77XX_RED);
                tft.printf("%s", doc["current"]["time"].as<const char*>());
                tft.setTextSize(3);
                tft.setCursor(10, 30);
                tft.setTextColor(ST77XX_YELLOW);
                tft.printf("%.1f C", doc["current"]["temperature_2m"].as<float>());
                tft.setCursor(10, 60);
                tft.setTextColor(ST77XX_BLUE);
                tft.printf("%.1f %%", doc["current"]["relative_humidity_2m"].as<float>());
                tft.setCursor(10, 90);
                tft.setTextColor(ST77XX_GREEN);
                tft.printf("%.1f km/h", doc["current"]["windspeed"].as<float>());
//                Serial.println(payload);
                int code = doc["current"]["weather_code"];
                iconrate = 2;

                switch(code){
                  case 0 ...3://晴天，主要晴朗,部分多雲,陰天
                    drawWeatherIcon(160, 40, "Sunny"); 
                    break;
                  case 45 ...55://霧和沈積霧.毛毛雨:輕度、中度和密集強度
                    drawWeatherIcon(160, 40, "Cloud"); 
                    break;
                  default:  //激烈天氣
                    drawWeatherIcon(160, 40, "Rain"); 
                    break;  
                }   //end switch
//======================預測三日天氣顯示================================================
// --- 預報顯示區設定 ---
int screenWidth = tft.width();
int colWidth = screenWidth / 3; 
int startY = 135;               
int totalHeight = tft.height() - startY; 
int lineSpacing = 14; // 調整行距，讓 5 個項目均勻分布

// 定義每一列的背景顏色 (RGB565)
uint16_t bgColors[] = {
    tft.color565(30, 30, 30),   // 深灰
    tft.color565(10, 25, 45),   // 深藍
    tft.color565(35, 15, 35)    // 深紫
};

tft.setTextSize(1);

for (int i = 0; i < 3; i++) {
    int colX = i * colWidth;
    
    // 0. 繪製該列背景底色 (先清空舊內容)
    tft.fillRect(colX, startY, colWidth, totalHeight, bgColors[i]);
    
    // 1. 取得資料
    String dateStr = String(doc["daily"]["time"][i+1].as<const char*>()).substring(5);
    float maxT = doc["daily"]["temperature_2m_max"][i+1].as<float>();
    float minT = doc["daily"]["temperature_2m_min"][i+1].as<float>();
    String sunrise = String(doc["daily"]["sunrise"][i+1].as<const char*>()).substring(11);
    String sunset = String(doc["daily"]["sunset"][i+1].as<const char*>()).substring(11);
    int dayCode = doc["daily"]["weather_code"][i+1]; // 確保 JSON 有請求此欄位

    int textX = colX + 4;

    // --- 開始垂直繪製 ---
    
    // 1. 日期
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(textX, startY + 5);
    tft.print(dateStr);

    // 2. 溫度 (低溫-高溫)
    tft.setCursor(textX, startY + 5 + lineSpacing);
    tft.setTextColor(tft.color565(200, 255, 200)); // 淺綠色
    tft.printf("%.0f-%.0f", minT, maxT);

    // 3. 日昇時間
    tft.setCursor(textX, startY + 5 + lineSpacing * 2);
    tft.setTextColor(ST77XX_YELLOW);
    tft.printf("R:%s", sunrise.c_str());

    // 4. 日落時間
    tft.setCursor(textX, startY + 5 + lineSpacing * 3);
    tft.setTextColor(tft.color565(255, 150, 0)); // 橘色
    tft.printf("S:%s", sunset.c_str());

    // 5. 圖示 (放在最下方)
    // 注意：圖示 X 座標通常是圖形中心，故設在欄位中央
    int iconX = colX + (colWidth / 3); 
    int iconY = startY + 5 + lineSpacing * 4 + 10;
    
    const char* iconName;
    if (dayCode <= 3) iconName = "Sunny";
    else if (dayCode <= 55) iconName = "Cloud";
    else iconName = "Rain";
    iconrate = 1;    
    // 確保在畫完背景色後才呼叫圖示函式
    drawWeatherIcon(iconX, iconY, iconName);
}

//===============================================


//          }
                
                    }
            } else {
                Serial.printf("HTTP 請求失敗，錯誤碼: %d\n", httpCode);
            }
            http.end();
        }
        vTaskDelay(pdMS_TO_TICKS(600000)); // 每10分鐘更新一次
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    // 初始化顯示器
    tft.init(240, 240); 
    tft.setRotation(2);
    tft.fillScreen(ST77XX_BLACK);
    
    // WiFiManager 配網
    WiFi.begin();
    WiFi.mode(WIFI_STA); // Wi-Fi設置成STA模式；預設模式為STA+AP
    WiFi.setTxPower(WIFI_POWER_8_5dBm);     
    WiFiManager wm;
    if (!wm.autoConnect("ESP32_Weather_AP")) {
        ESP.restart();
    }

    // 初始化按鈕與蜂鳴器
    btn.attachClick(handleClick);
//    ledcAttachPin(BUZZER_PIN, 0);
//    ledcSetup(0, 2000, 8);

    // 建立 FreeRTOS 任務
    xTaskCreate(fetchWeather, "WeatherTask", 8192, NULL, 1, &weatherTaskHandle);
}

void loop() {
    btn.tick(); // 持續偵測按鈕狀態
    vTaskDelay(pdMS_TO_TICKS(10));
}
