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

// --- 音階 of 定義 ---
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_B5  988
#define NOTE_C6  1046
#define NOTE_CS6 1108
#define NOTE_D5  588
#define NOTE_D6  1175
#define NOTE_E6  1319

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

const unsigned long WORK_LIMIT   = 1200000UL; // 本番用：20分
const unsigned long RELAX_LIMIT  = 20000; // 休憩20秒
const float DISTANCE_THRESHOLD   = 80.0f; 

// --- 手かざし再開用の設定と状態 ---
const float HAND_GESTURE_THRESHOLD = 15.0f; // 手をかざす距離 (15cm以内)
const unsigned long GESTURE_DURATION = 1000; // かざす時間 (1秒)
unsigned long handGestureStartTime = 0;
bool isHandDetected = false;

// --- 状態管理 ---
enum SystemState { STATE_MONITORING, STATE_RELAXING, STATE_WAIT_ENTER };
SystemState currentState = STATE_MONITORING;

unsigned long accumulatedTime = 0; 
unsigned long lastCheckTime = 0;   
unsigned long relaxStartTime = 0;  
int validDetectionCounter = 0;

WiFiUDP udp;

// ドラクエ宿屋風メロディ（開始時）
void playInnMelody() {
    Serial.println(">>> Playing: Dragon Quest Inn Melody");
    tone(buzzerPin, NOTE_D6, 312);  delay(312);
    tone(buzzerPin, NOTE_CS6, 312); delay(312);
    tone(buzzerPin, NOTE_C6, 312);  delay(312);
    tone(buzzerPin, NOTE_B5, 312);  delay(312);
    tone(buzzerPin, NOTE_A5, 156);  delay(312);
    tone(buzzerPin, NOTE_D5, 156);  delay(312);
    tone(buzzerPin, NOTE_D6, 936);  delay(936);
    noTone(buzzerPin);
}

// ポケモン回復音（終了時）
void playPokemonHealMelody() {
    Serial.println(">>> Playing: Pokemon Heal Melody");
    // シ シ シ ソ# ミー
    tone(buzzerPin, NOTE_B5, 450);  delay(460);
    tone(buzzerPin, NOTE_B5, 450);  delay(460);
    tone(buzzerPin, NOTE_B5, 225);  delay(235);
    tone(buzzerPin, NOTE_GS5, 225); delay(235);
    tone(buzzerPin, NOTE_E6, 900);  delay(900);
    noTone(buzzerPin);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Smart Eye-Care System (eyecare-template) ---");

    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    pinMode(ledGreen, OUTPUT);
    pinMode(ledRed, OUTPUT);
    pinMode(buzzerPin, OUTPUT);

    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("OLED allocation failed!");
        for(;;);
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("System Booting...");
    display.display();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nWiFi Connected!");
    display.println("WiFi Connected!");
    display.display();

    udp.begin(udp_port);
    lastCheckTime = millis();
    delay(1000);
}

void loop() {
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - lastCheckTime;
    lastCheckTime = currentTime;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);

    switch (currentState) {
        case STATE_MONITORING: {
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

            display.setCursor(4, 0);
            display.println("=== EYE-CARE SYS ===");
            display.setCursor(19, 10);
            display.println("Status: WORKING");
            
            display.setCursor(25, 22);
            display.print("Dist: "); display.print(distance, 1); display.println(" cm");
            
            display.setCursor(34, 34);
            display.print("Time: ");
            if (r_min < 10) display.print("0"); display.print(r_min); display.print(":");
            if (r_sec < 10) display.print("0"); display.println(r_sec);
            
            display.setCursor(4, 46);
            display.println("--------------------");

            int barWidth = (accumulatedTime * 128) / WORK_LIMIT;
            display.fillRect(0, 56, min(barWidth, 128), 8, SSD1306_WHITE);

            if (accumulatedTime >= WORK_LIMIT) {
                playInnMelody(); 
                udp.beginPacket(pc_ip, udp_port);
                udp.print("TIME_UP");
                udp.endPacket();
                currentState = STATE_RELAXING;
                relaxStartTime = millis();
                lastCheckTime = millis();
            }
            break;
        }

        case STATE_RELAXING: {
            digitalWrite(ledGreen, LOW);
            digitalWrite(ledRed, HIGH);

            unsigned long relaxElapsed = currentTime - relaxStartTime;
            unsigned long r_relax = (RELAX_LIMIT > relaxElapsed) ? (RELAX_LIMIT - relaxElapsed) : 0;
            
            display.setCursor(10, 0);
            display.println("=== RELAX TIME ===");
            display.setCursor(19, 10);
            display.println("Status: RESTING");
            display.setCursor(4, 20);
            display.println("--------------------");

            display.setTextSize(2);
            display.setCursor(28, 30);
            display.print((r_relax + 999) / 1000); display.println(" sec");
            
            display.setTextSize(1);
            display.setCursor(4, 46);
            display.println("--------------------");

            int relaxBarWidth = (relaxElapsed * 128) / RELAX_LIMIT;
            display.fillRect(0, 56, min(relaxBarWidth, 128), 8, SSD1306_WHITE);

            if (relaxElapsed >= RELAX_LIMIT) {
                // ポケモン回復音を鳴らす（通信前の安全なタイミング）
                playPokemonHealMelody();
                
                // 待機モードに入る前に、溜まっている古いパケットをすべて破棄する
                while (udp.parsePacket() > 0) {
                    while (udp.available() > 0) udp.read();
                }

                currentState = STATE_WAIT_ENTER;
                lastCheckTime = millis();
                isHandDetected = false; // 手かざし検出状態を初期化
            }
            break;
        }

        case STATE_WAIT_ENTER: {
            digitalWrite(ledGreen, LOW);
            digitalWrite(ledRed, LOW);

            // 超音波センサーで距離を測定
            digitalWrite(trigPin, LOW); delayMicroseconds(2);
            digitalWrite(trigPin, HIGH); delayMicroseconds(10);
            digitalWrite(trigPin, LOW);
            float distance = pulseIn(echoPin, HIGH, 10000) * 0.0343f / 2.0f;

            // 手かざし検出（15cm以内）
            if (distance > 0 && distance <= HAND_GESTURE_THRESHOLD) {
                if (!isHandDetected) {
                    isHandDetected = true;
                    handGestureStartTime = millis();
                } else {
                    if (millis() - handGestureStartTime >= GESTURE_DURATION) {
                        // 1秒経過で再開
                        tone(buzzerPin, 1500, 300); // 完了音（ピッ）
                        accumulatedTime = 0;
                        validDetectionCounter = 0;
                        currentState = STATE_MONITORING;
                        isHandDetected = false;
                    }
                }
            } else {
                isHandDetected = false;
            }

            display.setCursor(22, 0);
            display.println("=== READY? ===");

            display.setTextSize(2);
            display.setCursor(34, 20);
            display.println("HOLD");
            display.setCursor(34, 40);
            display.println("HAND");
            display.setTextSize(1);
            
            // 進捗バーの描画
            if (isHandDetected) {
                unsigned long elapsed = millis() - handGestureStartTime;
                int barWidth = (elapsed * 128) / GESTURE_DURATION;
                display.fillRect(0, 56, min(barWidth, 128), 8, SSD1306_WHITE);
            } else {
                display.setCursor(4, 56);
                display.println("or press Enter on PC");
            }
            
            // 従来のPCからのUDP受信による再開
            if (udp.parsePacket()) {
                while (udp.available()) udp.read(); 
                accumulatedTime = 0;
                validDetectionCounter = 0;
                currentState = STATE_MONITORING;
                isHandDetected = false;
            }
            break;
        }
    }

    display.display();
    delay(50); 
}
