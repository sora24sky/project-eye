# Smart Eye-Care System

このプロジェクトは、超音波センサーでユーザーの着席を検知し、20分の作業ごとに20秒の目の休憩を促す「スマート眼精疲労防止ガジェット」です。Arduino (UNOR4 WiFi) と Python (Windowsゲートウェイ) が連携して動作します。

## 動作に必要なもの

| コンポーネント | 役割 | 必須度 |
|---|---|---|
| **Arduino** (`eyecare-template.ino` に必要な情報を書き込んだもの) | 着席検知・タイマー・休憩誘導・一時停止・手かざし検知・ログ送信 | **必須** |
| **Python スクリプト** (`udp-logger.py`) | Windowsデスクトップ通知の表示 | 任意 |

> **Pythonスクリプトがなくても、Googleスプレッドシートへのログ記録は動作します。**
> Arduino が直接 Google Apps Script に HTTPS 送信するため、PC は不要です。

## システム連携図

```mermaid
flowchart TD
    %% ノードのスタイル定義
    classDef arduino fill:#E1F5FE,stroke:#0288D1,stroke-width:2px;
    classDef python fill:#FFF9C4,stroke:#FBC02D,stroke-width:2px;
    classDef user fill:#FFE0B2,stroke:#F57C00,stroke-width:2px;
    classDef gas fill:#E8F5E9,stroke:#388E3C,stroke-width:2px;

    User["👥 ユーザー"]:::user
    GAS["📊 Googleスプレッドシート<br/>(GAS 直接送信)"]:::gas

    subgraph Hardware ["🤖 Arduino (本体・必須)"]
        Arduino["Arduino UNO R4"]:::arduino
    end

    subgraph Software ["💻 PC (任意連携)"]
        Python["Python (udp-logger.py)"]:::python
    end

    %% フロー関係
    Arduino -->|① 20分経過<br/>UDP: TIME_UP| Python
    Python -->|② Windows通知を表示| User
    User -->|③ 20秒休憩| User

    %% 再開分岐
    User -->|④-A センサーに手かざし 1秒| Arduino
    User -->|④-B PCでEnterキー入力| Python
    Python -->|⑤-B 再開信号<br/>UDP: RESET_OK| Arduino

    %% ログ記録
    Arduino -->|⑥ ログ送信<br/>HTTPS POST| GAS
    GAS -.->|⑦ 次のサイクルへ| Arduino
```

## 各コンポーネントの役割

### 1. Arduino (`eyecare-template.ino` / `eyecare-0621.ino`)
*   **着席検知**: 超音波センサーで80cm以内に人がいるか監視します。
*   **タイマー管理**: 人がいる間だけ作業時間を累積します。
*   **休憩誘導**: 休憩時間になるとOLEDに残り秒数を表示し、赤LEDとブザー（ドラクエ・ポケモン回復風メロディ）でアラートを出します。
*   **再開検知**: センサーへの手かざし（15cm以内で1秒）、またはPC側からの再開合図を受信して計測モードに復帰します。
*   **一時停止（中断）機能**: 作業中にセンサーに手をかざす（15cm以内で1秒）と、タイマーのカウントを一時停止できます。再度手をかざすと、一時停止した時点の累積時間から計測を再開します。

### 2. Python Gateway (`udp-logger.py`) ※任意
*   **通知**: Arduinoからの信号を受け取り、Windowsのデスクトップ通知を表示します。
*   **休憩管理**: 20秒のカウントダウンを画面に表示します。
*   **制御**: ユーザーがEnterキーを押すと、Arduinoに再開の信号（UDP: `RESET_OK`）を返します（バックアップ操作）。

## 導入方法

### ハードウェア構成
*   Arduino UNO R4 WiFi
*   超音波センサー (HC-SR04)
*   OLEDディスプレイ (SSD1306, 128x64)
*   LED（赤・緑）
*   パッシブブザー（回路図では都合上piezo buzzerに置き換えています）

### セットアップ
1.  **Arduino**:
    *   `ssid` と `password` を自分のWi-Fi環境に合わせて書き換えます。
    *   `pc_ip` をPCのIPアドレスに書き換えます。
    *   Arduino IDEでスケッチを書き込みます。
2.  **Python（任意・Windows通知が必要な場合のみ）**:
    *   Pythonをインストール済みであることを確認します。
    *   `python udp-logger.py` を実行します。
    *   ファイアウォールの警告が出た場合は、UDP 5005ポートの通信を許可してください。

## 使い方
1.  Arduinoデバイスを起動します。緑LEDが点灯しカウントダウンが始まります。（離席すると中断）
2.  20分経過するとArduinoが休憩モード（20秒）に入ります。（PCでPythonを起動している場合はWindows通知も届きます）
3.  20秒経過後、超音波センサーに手をかざす（15cm以内で1秒間）と、次のサイクルが始まりGoogleスプレッドシートに自動でログが記録されます。PCでEnterキーを押すことでも再開できます。

[アイケアスプレッドシート　ログ](https://docs.google.com/spreadsheets/d/1GVeTNaiIqg9THnKGMKAm8ZsvyVecBAWqQ8LLO331-pg/edit?usp=sharing)  
[アイケアテスト動画](https://youtube.com/shorts/iXKd-fRjQpk?feature=share)

![回路図（6/15更新）](image.png)
