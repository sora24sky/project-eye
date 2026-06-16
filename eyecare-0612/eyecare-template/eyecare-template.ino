#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- 設定 (各自の環境に合わせて書き換えてください) ---
const char* ssid     = "YOUR_WIFI_SSID";       
const char* password = "YOUR_WIFI_PASSWORD";   
const char* pc_ip    = "192.168.x.x"; // PCのIPアドレス
const int udp_port   = 5005;

const int trigPin   = 2;
const int echoPin   = 3;
const int ledGreen  = 5;
const int ledRed    = 6;
const int buzzerPin = 7;

const unsigned long WORK_LIMIT   = 1200000UL; // 20分 (テスト時は3000等に変更)
const unsigned long RELAX_LIMIT  = 20000;    // 休憩20秒
const float DISTANCE_THRESHOLD   = 80.0f; 

// --- 状態管理 ---
enum SystemState { STATE_MONITORING, STATE_RELAXING, STATE_WAIT_ENTER };
SystemState currentState = STATE_MONITORING;

unsigned long accumulatedTime = 0; 
unsigned long lastCheckTime = 0;   
unsigned long relaxStartTime = 0;  
unsigned long lastAlertAction = 0;
bool alertToggle = false;
int alertCount = 0; 
int validDetectionCounter = 0;

WiFiUDP udp;

void setup() {
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    pinMode(ledGreen, OUTPUT);
    pinMode(ledRed, OUTPUT);
    pinMode(buzzerPin, OUTPUT);

    display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    udp.begin(udp_port);
    
    lastCheckTime = millis();
}

void loop() {
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - lastCheckTime;
    lastCheckTime = currentTime;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);

    switch (currentState) {
        case STATE_MONITORING:
            digitalWrite(ledGreen, HIGH);
            digitalWrite(ledRed, LOW);

            digitalWrite(trigPin, LOW); delayMicroseconds(2);
            digitalWrite(trigPin, HIGH); delayMicroseconds(10);
            digitalWrite(trigPin, LOW);
            float distance = pulseIn(echoPin, HIGH, 10000) * 0.0343f / 2.0f;

            if (distance > 0 && distance <= DISTANCE_THRESHOLD) {
                if (++validDetectionCounter >= 3) accumulatedTime += elapsedTime;
            } else {
                validDetectionCounter = 0;
            }

            unsigned long timeLeft = (WORK_LIMIT > accumulatedTime) ? (WORK_LIMIT - accumulatedTime) : 0;
            unsigned long r_min = (timeLeft / 1000) / 60;
            unsigned long r_sec = (timeLeft / 1000) % 60;

            display.println("=== EYE-CARE SYS ===");
            display.println("Status: WORKING");
            display.print("Dist  : "); display.print(distance, 1); display.println(" cm");
            display.print("Time  : ");
            if (r_min < 10) display.print("0"); display.print(r_min); display.print(":");
            if (r_sec < 10) display.print("0"); display.println(r_sec);
            display.println("--------------------");

            int barWidth = (accumulatedTime * 128) / WORK_LIMIT;
            display.fillRect(0, 56, min(barWidth, 128), 8, SSD1306_WHITE);

            if (accumulatedTime >= WORK_LIMIT) {
                udp.beginPacket(pc_ip, udp_port);
                udp.print("TIME_UP");
                udp.endPacket();
                currentState = STATE_RELAXING;
                relaxStartTime = currentTime;
                alertCount = 0;
            }
            break;

        case STATE_RELAXING:
            unsigned long relaxElapsed = currentTime - relaxStartTime;
            digitalWrite(ledGreen, HIGH);

            if (alertCount < 4 && currentTime - lastAlertAction >= 500) {
                lastAlertAction = currentTime;
                alertToggle = !alertToggle;
                alertCount++;
                digitalWrite(ledRed, alertToggle ? HIGH : LOW);
                if (alertToggle) tone(buzzerPin, 2000); else noTone(buzzerPin);
            } else if (alertCount >= 4) {
                digitalWrite(ledRed, LOW);
                noTone(buzzerPin);
            }

            unsigned long r_relax = (RELAX_LIMIT > relaxElapsed) ? (RELAX_LIMIT - relaxElapsed) : 0;
            display.println("=== RELAX TIME ===");
            display.println("Status: RESTING");
            display.println("--------------------");
            display.println("");
            display.setTextSize(2);
            display.print("  "); display.print((r_relax + 999) / 1000); display.println(" sec");
            display.setTextSize(1);
            display.println("\n--------------------");

            int relaxBarWidth = (relaxElapsed * 128) / RELAX_LIMIT;
            display.fillRect(0, 56, min(relaxBarWidth, 128), 8, SSD1306_WHITE);

            if (relaxElapsed >= RELAX_LIMIT) {
                currentState = STATE_WAIT_ENTER;
                tone(buzzerPin, 1000, 500);
                digitalWrite(ledRed, LOW);
                digitalWrite(ledGreen, LOW);
            }
            break;

        case STATE_WAIT_ENTER:
            display.println("=== 20s ELAPSED ===");
            display.println("\n  [ OK ]");
            display.println("  Eye rest complete.");
            display.println("\nPress [Enter] on PC");
            display.println("to resume work mode.");

            if (udp.parsePacket()) {
                accumulatedTime = 0;
                validDetectionCounter = 0;
                currentState = STATE_MONITORING;
            }
            break;
    }

    display.display();
    delay(50); 
}
