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

// --- 音階の定義 ---
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_B5  988
#define NOTE_C6  1046
#define NOTE_CS6 1108
#define NOTE_D5  588
#define NOTE_D6  1175
#define NOTE_E6  1319

// --- WiFi / UDP 設定 (各自の環境に合わせて書き換えてください) ---
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* pc_ip    = "192.168.x.x"; // PC通知を使う場合のみ必要
const int   udp_port = 5005;

// --- Google Apps Script 設定 (各自のURLに書き換えてください) ---
const char* GAS_HOST = "script.google.com";
const char* GAS_PATH = "/macros/s/YOUR_GAS_SCRIPT_ID/exec";

// --- ピン設定 ---
const int trigPin   = 2;
const int echoPin   = 3;
const int ledGreen  = 5;
const int ledRed    = 6;
const int buzzerPin = 7;

// --- タイマー設定 ---
const unsigned long WORK_LIMIT  = 1200000UL; // 本番用：20分 (20 * 60 * 1000)
const unsigned long RELAX_LIMIT = 20000;      // 休憩20秒
const float DISTANCE_THRESHOLD = 80.0f;

// --- 手かざし再開の設定 ---
const float          HAND_GESTURE_THRESHOLD = 15.0f;
const unsigned long  GESTURE_DURATION       = 1000;
unsigned long handGestureStartTime = 0;
bool isHandDetected = false;

// --- 状態管理 ---
enum SystemState { STATE_MONITORING, STATE_RELAXING, STATE_WAIT_ENTER, STATE_PAUSED };
SystemState currentState = STATE_MONITORING;

unsigned long accumulatedTime = 0;
unsigned long lastCheckTime   = 0;
unsigned long relaxStartTime  = 0;
int validDetectionCounter     = 0;

WiFiUDP       udp;
WiFiSSLClient sslClient;

// ============================================================
//  OLED文字列中央寄せ描画関数
// ============================================================
void drawCenteredString(const char* buf, int y, int size) {
    int len = strlen(buf);
    int charWidth = 6 * size;
    int x = (128 - (len * charWidth)) / 2;
    if (x < 0) x = 0;
    display.setTextSize(size);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    display.print(buf);
}

// ============================================================
//  メロディ
// ============================================================
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

void playPokemonHealMelody() {
    Serial.println(">>> Playing: Pokemon Heal Melody");
    tone(buzzerPin, NOTE_B5, 450);  delay(460);
    tone(buzzerPin, NOTE_B5, 450);  delay(460);
    tone(buzzerPin, NOTE_B5, 225);  delay(235);
    tone(buzzerPin, NOTE_GS5, 225); delay(235);
    tone(buzzerPin, NOTE_E6, 900);  delay(900);
    noTone(buzzerPin);
}

// ============================================================
//  Google Sheets への直接ログ送信
//  GASは302リダイレクトを返すため2段階の接続が必要
// ============================================================
void sendLogToGAS(const char* trigger) {
    // --- OLEDにLogging表示 ---
    display.clearDisplay();
    drawCenteredString("Logging to", 20, 1);
    drawCenteredString("Google Sheets...", 32, 1);
    display.display();

    Serial.print(">>> Logging to GAS (trigger=");
    Serial.print(trigger);
    Serial.println(")");

    String body = String("{\"message\":\"EYE_CARE_COMPLETE\",\"trigger\":\"") + trigger + "\"}";
    String location = "";

    // --- Step 1: GASにPOSTして Locationヘッダ（リダイレクト先）を取得 ---
    if (sslClient.connect(GAS_HOST, 443)) {
        sslClient.print("POST "); sslClient.print(GAS_PATH); sslClient.println(" HTTP/1.1");
        sslClient.print("Host: "); sslClient.println(GAS_HOST);
        sslClient.println("Content-Type: application/json");
        sslClient.print("Content-Length: "); sslClient.println(body.length());
        sslClient.println("Connection: close");
        sslClient.println();
        sslClient.println(body);

        unsigned long deadline = millis() + 10000;
        while (millis() < deadline) {
            if (sslClient.available()) {
                String line = sslClient.readStringUntil('\n');
                line.trim();
                if (line.startsWith("Location: ")) location = line.substring(10);
                if (line.length() == 0) break; // ヘッダ終端
            }
            if (!sslClient.connected()) break;
        }
        sslClient.stop();
        delay(200);
    } else {
        Serial.println(">>> [GAS Step1] Connect failed");
        return;
    }

    if (location.length() == 0) {
        Serial.println(">>> [GAS] No redirect received. Log may not have been recorded.");
        return;
    }
    Serial.println(">>> Redirect → " + location);

    // --- Step 2: リダイレクト先にGETリクエストを送りスクリプトを実行させる ---
    String redirectHost = "";
    String redirectPath = "/";

    if (location.startsWith("https://")) {
        String url = location.substring(8); // "https://" を除去
        int si = url.indexOf('/');
        if (si >= 0) {
            redirectHost = url.substring(0, si);
            redirectPath = url.substring(si);
        } else {
            redirectHost = url;
        }
    }

    if (redirectHost.length() == 0) {
        Serial.println(">>> [GAS] Failed to parse redirect URL");
        return;
    }

    char rhBuf[128];
    redirectHost.toCharArray(rhBuf, sizeof(rhBuf));

    if (sslClient.connect(rhBuf, 443)) {
        sslClient.print("GET "); sslClient.print(redirectPath); sslClient.println(" HTTP/1.1");
        sslClient.print("Host: "); sslClient.println(redirectHost);
        sslClient.println("Connection: close");
        sslClient.println();

        String responseBody = "";
        bool   headersDone  = false;
        unsigned long deadline = millis() + 10000;
        while (millis() < deadline) {
            if (sslClient.available()) {
                String line = sslClient.readStringUntil('\n');
                if (!headersDone) {
                    line.trim();
                    if (line.length() == 0) headersDone = true;
                } else {
                    responseBody += line;
                }
            }
            if (!sslClient.connected()) break;
        }
        sslClient.stop();
        Serial.println(">>> GAS response: " + responseBody);
        Serial.println(">>> [GAS] Log sent successfully!");
    } else {
        Serial.println(">>> [GAS Step2] Connect failed");
    }
}

// ============================================================
//  setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Smart Eye-Care System (eyecare-template) ---");

    pinMode(trigPin,   OUTPUT);
    pinMode(echoPin,   INPUT);
    pinMode(ledGreen,  OUTPUT);
    pinMode(ledRed,    OUTPUT);
    pinMode(buzzerPin, OUTPUT);

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("OLED allocation failed!");
        for (;;);
    }
    display.clearDisplay();
    drawCenteredString("System Booting...", 20, 1);
    display.display();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    display.clearDisplay();
    drawCenteredString("WiFi Connected!", 28, 1);
    display.display();

    udp.begin(udp_port);
    lastCheckTime = millis();
    delay(1000);
}

// ============================================================
//  loop
// ============================================================
void loop() {
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - lastCheckTime;
    lastCheckTime = currentTime;

    display.clearDisplay();

    switch (currentState) {

        // ---- 作業監視モード ----
        case STATE_MONITORING: {
            digitalWrite(ledGreen, HIGH);
            digitalWrite(ledRed,   LOW);

            // 超音波センサーで距離測定
            digitalWrite(trigPin, LOW);  delayMicroseconds(2);
            digitalWrite(trigPin, HIGH); delayMicroseconds(10);
            digitalWrite(trigPin, LOW);
            float distance = pulseIn(echoPin, HIGH, 10000) * 0.0343f / 2.0f;

            // --- 手かざしによる一時停止（中断）検知 (15cm以内を1秒間) ---
            if (distance > 0 && distance <= HAND_GESTURE_THRESHOLD) {
                if (!isHandDetected) {
                    isHandDetected        = true;
                    handGestureStartTime  = millis();
                } else if (millis() - handGestureStartTime >= GESTURE_DURATION) {
                    // 一時停止音 (ピッと低めの音)
                    tone(buzzerPin, 1000, 300);
                    delay(300);
                    noTone(buzzerPin);

                    currentState          = STATE_PAUSED;
                    isHandDetected        = false;
                    lastCheckTime         = millis();
                    break;
                }
            } else {
                isHandDetected = false;
            }

            // --- 通常の着席検知と作業時間累積 ---
            // 手かざし中（15cm以内）は作業時間累積をスキップし、それ以外（15cm超〜80cm以下）で累積する
            if (distance > HAND_GESTURE_THRESHOLD && distance <= DISTANCE_THRESHOLD) {
                if (++validDetectionCounter >= 3) accumulatedTime += elapsedTime;
            } else if (distance <= HAND_GESTURE_THRESHOLD && distance > 0) {
                // 手かざし中は着席カウンターは維持しつつ時間の累積だけスキップ
            } else {
                validDetectionCounter = 0;
            }

            unsigned long timeLeft = (WORK_LIMIT > accumulatedTime) ? (WORK_LIMIT - accumulatedTime) : 0;
            unsigned long r_min = (timeLeft / 1000) / 60;
            unsigned long r_sec = (timeLeft / 1000) % 60;

            drawCenteredString("=== EYE-CARE SYS ===", 0, 1);
            drawCenteredString("Status: WORKING", 10, 1);
            drawCenteredString(("Dist: " + String(distance, 1) + " cm").c_str(), 22, 1);

            char timeBuf[16];
            sprintf(timeBuf, "Time: %02lu:%02lu", r_min, r_sec);
            drawCenteredString(timeBuf, 34, 1);

            drawCenteredString("--------------------", 46, 1);

            int barWidth = (accumulatedTime * 128) / WORK_LIMIT;
            display.fillRect(0, 56, min(barWidth, 128), 8, SSD1306_WHITE);

            if (accumulatedTime >= WORK_LIMIT) {
                display.display(); // 画面を確定してからブザー開始（I2C書き込みとブザー音を重ねない）
                playInnMelody();
                udp.beginPacket(pc_ip, udp_port);
                udp.print("TIME_UP"); // PC通知スクリプトを使う場合のために送信
                udp.endPacket();
                currentState   = STATE_RELAXING;
                relaxStartTime = millis();
                lastCheckTime  = millis();
            }
            break;
        }

        // ---- 一時停止モード ----
        case STATE_PAUSED: {
            digitalWrite(ledGreen, LOW);
            digitalWrite(ledRed,   LOW);

            // 超音波センサーで距離測定
            digitalWrite(trigPin, LOW);  delayMicroseconds(2);
            digitalWrite(trigPin, HIGH); delayMicroseconds(10);
            digitalWrite(trigPin, LOW);
            float distance = pulseIn(echoPin, HIGH, 10000) * 0.0343f / 2.0f;

            // --- 手かざしによる再開検知 (15cm以内を1秒間) ---
            if (distance > 0 && distance <= HAND_GESTURE_THRESHOLD) {
                if (!isHandDetected) {
                    isHandDetected        = true;
                    handGestureStartTime  = millis();
                } else if (millis() - handGestureStartTime >= GESTURE_DURATION) {
                    // 再開音 (ピッと高めの音)
                    tone(buzzerPin, 1500, 300);
                    delay(300);
                    noTone(buzzerPin);

                    currentState          = STATE_MONITORING;
                    isHandDetected        = false;
                    lastCheckTime         = millis();
                    break;
                }
            } else {
                isHandDetected = false;
            }

            drawCenteredString("=== PAUSED ===", 0, 1);
            drawCenteredString("HOLD", 20, 2);
            drawCenteredString("HAND", 40, 2);

            if (isHandDetected) {
                unsigned long elapsed = millis() - handGestureStartTime;
                int barWidth = (elapsed * 128) / GESTURE_DURATION;
                display.fillRect(0, 56, min(barWidth, 128), 8, SSD1306_WHITE);
            } else {
                drawCenteredString("to Resume Monitoring", 56, 1);
            }
            break;
        }

        // ---- 休憩モード ----
        case STATE_RELAXING: {
            digitalWrite(ledGreen, LOW);
            digitalWrite(ledRed,   HIGH);

            unsigned long relaxElapsed = currentTime - relaxStartTime;
            unsigned long r_relax = (RELAX_LIMIT > relaxElapsed) ? (RELAX_LIMIT - relaxElapsed) : 0;

            drawCenteredString("=== RELAX TIME ===", 0, 1);
            drawCenteredString("Status: RESTING", 10, 1);
            drawCenteredString("--------------------", 20, 1);

            String relaxStr = String((r_relax + 999) / 1000) + " sec";
            drawCenteredString(relaxStr.c_str(), 30, 2);

            drawCenteredString("--------------------", 46, 1);

            int relaxBarWidth = (relaxElapsed * 128) / RELAX_LIMIT;
            display.fillRect(0, 56, min(relaxBarWidth, 128), 8, SSD1306_WHITE);

            if (relaxElapsed >= RELAX_LIMIT) {
                display.display(); // 画面を確定してからブザー開始（I2C書き込みとブザー音を重ねない）
                playPokemonHealMelody();

                // 古いパケットを破棄してから待機モードへ
                while (udp.parsePacket() > 0) {
                    while (udp.available() > 0) udp.read();
                }
                currentState = STATE_WAIT_ENTER;
                lastCheckTime = millis();
                isHandDetected = false;
            }
            break;
        }

        // ---- 再開待機モード ----
        case STATE_WAIT_ENTER: {
            digitalWrite(ledGreen, LOW);
            digitalWrite(ledRed,   LOW);

            // 超音波センサーで距離測定
            digitalWrite(trigPin, LOW);  delayMicroseconds(2);
            digitalWrite(trigPin, HIGH); delayMicroseconds(10);
            digitalWrite(trigPin, LOW);
            float distance = pulseIn(echoPin, HIGH, 10000) * 0.0343f / 2.0f;

            // --- 手かざし再開 (15cm以内を1秒間) ---
            if (distance > 0 && distance <= HAND_GESTURE_THRESHOLD) {
                if (!isHandDetected) {
                    isHandDetected        = true;
                    handGestureStartTime  = millis();
                } else if (millis() - handGestureStartTime >= GESTURE_DURATION) {
                    tone(buzzerPin, 1500, 300); // 完了音
                    delay(300);                 // 音が終わるまで待機
                    noTone(buzzerPin);

                    // GASにログを送信してから作業モードへ
                    sendLogToGAS("HAND_GESTURE");

                    accumulatedTime       = 0;
                    validDetectionCounter = 0;
                    currentState          = STATE_MONITORING;
                    isHandDetected        = false;
                    lastCheckTime         = millis();
                    break;
                }
            } else {
                isHandDetected = false;
            }

            // --- PCからのUDP受信による再開（バックアップ） ---
            if (udp.parsePacket()) {
                while (udp.available()) udp.read();

                // GASにログを送信してから作業モードへ
                sendLogToGAS("PC_RESET");

                accumulatedTime       = 0;
                validDetectionCounter = 0;
                currentState          = STATE_MONITORING;
                isHandDetected        = false;
                lastCheckTime         = millis();
                break;
            }

            drawCenteredString("=== READY? ===", 0, 1);
            drawCenteredString("HOLD", 20, 2);
            drawCenteredString("HAND", 40, 2);

            if (isHandDetected) {
                unsigned long elapsed = millis() - handGestureStartTime;
                int barWidth = (elapsed * 128) / GESTURE_DURATION;
                display.fillRect(0, 56, min(barWidth, 128), 8, SSD1306_WHITE);
            } else {
                drawCenteredString("or UDP from PC", 56, 1);
            }
            break;
        }
    }

    display.display();
    delay(50);
}
