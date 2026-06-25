# Smart Eye-Care System

このプロジェクトは、超音波センサーでユーザーの着席を検知し、20分の作業ごとに20秒の目の休憩を促す「スマート眼精疲労防止ガジェット」です。Arduino (UNO R4 WiFi) と Python (Windowsゲートウェイ)、およびGoogleスプレッドシート（GAS）が連携して動作します。

---

## 主な機能と特徴

### 1. 着席検知＆タイマー
超音波センサーでデスクの前に人がいる（距離が80cm以内）ときだけ作業時間を累積します。離席すると自動で一時停止し、戻ると再開します。

### 2. リッチな休憩アラート
作業が20分に達すると、赤LED、OLEDディスプレイのカウントダウン、およびブザーのメロディ（ドラクエ宿屋風・ポケモン回復風など）で目の休憩（20秒間）を促します。PCにデスクトップ通知も送信されます。

### 3. ジェスチャー操作（一時停止・再開）
* **一時停止**: 作業中にセンサーに手をかざす（15cm以内で1秒）と、マリオのポーズ音（「ミドミド」）が鳴り、タイマーが一時停止状態（`PAUSED`）になります。
* **再開**: ポーズ中または休憩完了後（`READY?`）に再度手をかざす、あるいはPCでEnterキーを押すと、マリオのポーズ音（または完了音）とともに計測が再開します。

### 4. OLED自動スリープ（消灯）機能
ディスプレイの焼き付き防止および省電力のため、80cm以内に人がいなくなって **5分（テスト時は10秒）** 経過すると、OLED画面を完全に消灯します。人が戻ってくると瞬時に復帰します（※休憩中は表示を維持します）。

### 5. 前日グラフ画像のPCローカル自動保存
日付が変わって最初のログ送信が行われた際、GASとPython（`udp-logger.py`）が自動連携し、前日分の時間帯別アイケア回数グラフ（PNG画像）をPCのローカルフォルダー `eyecare-0624/archive/` に自動保存します。Googleドライブの権限設定が不要なため、セキュリティエラーが起きません。

---

## システム連携図

```mermaid
flowchart TD
    %% ノードのスタイル定義
    classDef arduino fill:#E1F5FE,stroke:#0288D1,stroke-width:2px;
    classDef python fill:#FFF9C4,stroke:#FBC02D,stroke-width:2px;
    classDef user fill:#FFE0B2,stroke:#F57C00,stroke-width:2px;
    classDef gas fill:#E8F5E9,stroke:#388E3C,stroke-width:2px;

    User["👥 ユーザー"]:::user
    GAS["📊 Googleスプレッドシート<br/>(GAS)"]:::gas

    subgraph Hardware ["🤖 Arduino (本体)"]
        Arduino["Arduino UNO R4"]:::arduino
    end

    subgraph Software ["💻 PC ゲートウェイ (推奨)"]
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

    %% ログ記録とローカル画像保存
    Arduino -->|⑥-A (PC経由再開時) 再開イベント<br/>UDP: RESTART_EVENT| Python
    Python -->|⑦-A ログ送信<br/>POST {client: 'pc'}| GAS
    GAS -->|⑧-A 前日のグラフ画像を返却<br/>Base64| Python
    Python -->|⑨-A ローカルフォルダにPNG保存<br/>archive/| Python

    %% Arduino直接送信（バックアップ）
    Arduino -.->|⑥-B (PC未起動時) 直接ログ送信<br/>POST {client: 'arduino'}| GAS
```

---

## ファイル構成

* **Arduino スケッチ**: `C:\Users\sora2\OneDrive\ドキュメント\Arduino\eyecare-0624\eyecare-0624.ino`
* **Arduino テンプレート**: `C:\work2\eyecare\eyecare-template\eyecare-template.ino`
* **Python スクリプト**: `C:\Users\sora2\OneDrive\ドキュメント\udp-logger.py`
* **Python テンプレート**: `C:\work2\udp-logger-template.py`
* **GAS（スプレッドシート）コード**: `C:\work2\gas\gas-code.js`
* **アーカイブ画像保存先**: `C:\Users\sora2\OneDrive\ドキュメント\Arduino\eyecare-0624\archive\`

---

## 導入とセットアップ

### 1. Arduinoのセットアップ
1. `eyecare-0624.ino` をArduino IDEで開きます。
2. ご自身の環境に合わせて `ssid`、`password`、`pc_ip` (PCのローカルIPアドレス) を書き換えます。
3. `GAS_PATH` にGoogleスプレッドシートからデプロイしたWebアプリのURLのIDを設定します。
4. Arduinoにスケッチを書き込みます。

### 2. GAS（Googleスプレッドシート）のセットアップ
1. [Googleスプレッドシート](https://docs.google.com/spreadsheets/d/1GVeTNaiIqg9THnKGMKAm8ZsvyVecBAWqQ8LLO331-pg/edit?usp=sharing) の「拡張機能」 ＞ 「Apps Script」を開きます。
2. エディタ内のコードを `gas-code.js` の内容で上書きし、保存します。
3. 「デプロイ」 ＞ 「デプロイの管理」からバージョンを「新バージョン」にして再デプロイします。

### 3. Python（PC）のセットアップ
1. `udp-logger.py` 内の `GAS_URL` を、ご自身のWebアプリデプロイURLに書き換えます。
2. コマンドプロンプトやPowerShellで `python udp-logger.py` を実行してゲートウェイを起動します。