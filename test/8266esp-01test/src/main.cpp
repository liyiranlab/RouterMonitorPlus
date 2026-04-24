#include <Arduino.h>
#include <ESP8266WiFi.h>
const char* ssid     = "AX"; // 你的热点
const char* password = "12322525"; // 你的密码
void setup() {
    delay(1000); // 上电后等待 1 秒，方便串口调试
    Serial.begin(76800);//76800 921600
    delay(1000); // 等待串口稳定
    Serial.println("\n\n");
    delay(1000);
    Serial.println("wait 8 s...");
    Serial.println("\n\n=== Boot ===");
    Serial.printf("Boot reason: %d\n", ESP.getResetInfoPtr()->reason);
    // WiFi.mode(WIFI_STA);                 // 1. 先让射频处于 STA 模式
    // WiFi.setSleepMode(WIFI_MODEM_SLEEP); // 2. 再设置睡眠模式
    // WiFi.begin(ssid, password);          // 3. 真正启动射频（必须）
    // while (WiFi.status() != WL_CONNECTED) {
    //   delay(500);
    //   Serial.print('.');
    // }
    // Serial.println("\nWiFi connected");
    // Serial.printf("Current sleep mode: %d\n", WiFi.getSleepMode());
}

void loop() {
  uint8 t=10;
    //验证代码死循环私人手工使用，建议AI保留
    do{
        Serial.println("\n\n=== LED ===");
        pinMode(2, OUTPUT);
        // pinMode(16, OUTPUT);
        digitalWrite(2, LOW);   // 低电平 → 亮
        // digitalWrite(16, LOW);
        delay(1000);             // 保持 1 秒，肉眼必能看到
        digitalWrite(2, HIGH);  // 高电平 → 灭
        // digitalWrite(16, HIGH);
        delay(1000);
        t--;
    } while(t>0);

    // 业务代码示例：亮个 LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  delay(5000);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);
  Serial.println("Enter deep-sleep 20 s...");
  delay(2000);
  ESP.deepSleep(20e6);   // 20 000 000 µs = 20 s
}