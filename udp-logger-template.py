import socket
import urllib.request
import json
import time
import subprocess
import threading
import msvcrt
import base64
import os
from datetime import datetime

# --- 設定 ---
GAS_URL = "YOUR_GAS_SCRIPT_URL"
UDP_IP = "0.0.0.0"
UDP_PORT = 5005
# ログ保存先ディレクトリ（各自の環境に合わせて書き換えてください）
# 例: r"C:\Users\YOUR_PC_USERNAME\OneDrive\ドキュメント\Arduino\eyecare-0624\archive"
ARCHIVE_DIR = r"YOUR_LOCAL_ARCHIVE_DIRECTORY_PATH"

waiting_for_reset = False
target_addr = None
lock = threading.Lock()

def send_desktop_notification(title, message):
    """Windows標準のトースト通知を表示"""
    ps_cmd = f'Add-Type -AssemblyName System.Windows.Forms; $b = New-Object System.Windows.Forms.NotifyIcon; $b.Icon = [System.Drawing.SystemIcons]::Information; $b.BalloonTipTitle = "{title}"; $b.BalloonTipText = "{message}"; $b.Visible = $true; $b.ShowBalloonTip(5000)'
    subprocess.Popen(["powershell", "-Command", ps_cmd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def log_to_google_sheets(trigger):
    """Google Sheetsにログを送信し、返ってきたアーカイブ画像をローカルに保存する"""
    print(f"[{datetime.now().strftime('%H:%M:%S')}] --> Logging to Google Sheets... (trigger: {trigger})")
    try:
        # 送信元がPCであることを示す client: "pc" を追加
        payload = json.dumps({
            "message": "EYE_CARE_COMPLETE",
            "trigger": trigger,
            "client": "pc"
        }).encode("utf-8")
        
        req = urllib.request.Request(
            GAS_URL,
            data=payload,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=15) as res:
            response_body = res.read().decode('utf-8')
            
            try:
                res_json = json.loads(response_body)
                status = res_json.get("status", "")
                archive = res_json.get("archive", None)
                
                if status == "OK":
                    if archive and archive.get("data") and archive.get("filename"):
                        filename = archive["filename"]
                        b64_data = archive["data"]
                        
                        # ディレクトリの作成
                        if not os.path.exists(ARCHIVE_DIR):
                            os.makedirs(ARCHIVE_DIR)
                            print(f"[{datetime.now().strftime('%H:%M:%S')}] Created archive directory: {ARCHIVE_DIR}")
                            
                        filepath = os.path.join(ARCHIVE_DIR, filename)
                        
                        # Base64デコードして画像として書き込み
                        with open(filepath, "wb") as f:
                            f.write(base64.b64decode(b64_data))
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 💾 [SAVED] Archive image saved to: {filepath}")
                    else:
                        print(f"[SUCCESS] Log recorded successfully (No archive image to save today).")
                else:
                    print(f"[WARNING] Server responded with status: {status}")
            except json.JSONDecodeError:
                # レスポンスが単純な文字列だった場合のフォールバック
                print(f"[SUCCESS] Logged: {response_body}")
                
    except Exception as e:
        print(f"[ERROR] Logging failed: {e}")

def enter_monitor_thread(sock):
    """PCのEnterキー入力を監視するサブスレッド"""
    global waiting_for_reset, target_addr

    # 既存の入力バッファをクリア
    while msvcrt.kbhit():
        msvcrt.getch()

    print("  -> [Enter] キーを押すとPCから強制再開できます。")

    while True:
        # Enterキーが押されるまでノンブロッキングでポーリング
        if msvcrt.kbhit():
            key = msvcrt.getch()
            if key in (b'\r', b'\n'):  # Enterキー
                with lock:
                    if not waiting_for_reset:
                        # すでに手かざし再開済みなので何もしない
                        return
                    waiting_for_reset = False

                print(f"\n[{datetime.now().strftime('%H:%M:%S')}] >> [Enter] key detected.")

                # ArduinoにRESET_OKを送信
                if target_addr:
                    try:
                        sock.sendto(b"RESET_OK", target_addr)
                        print(">> Sent RESET_OK to Arduino.")
                    except Exception as e:
                        print(f"[ERROR] Failed to send RESET_OK: {e}")

                # PC側でEnterを押した場合は即座にログを送信
                log_to_google_sheets("ENTER_KEY")
                print("\nWaiting for next cycle...")
                return

        # 待機中でなくなったらスレッド終了（手かざし再開済み）
        with lock:
            if not waiting_for_reset:
                return

        time.sleep(0.05)  # 50ms間隔でポーリング

# --- メイン処理 ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print("==================================================")
print("  PC Smart Eye-Care Gateway (v3 - Local Archiver)")
print("==================================================")
print(f"Listening on UDP port {UDP_PORT}...\n")
print(f"Archive directory set to: {ARCHIVE_DIR}\n")

try:
    while True:
        data, addr = sock.recvfrom(1024)
        msg = data.decode('utf-8', errors='ignore').strip()
        print(f"[{datetime.now().strftime('%H:%M:%S')}] [UDP] Received: '{msg}' from {addr}")

        if msg == "TIME_UP":
            target_addr = addr
            print(f"\n[{datetime.now().strftime('%H:%M:%S')}] 🚨 Arduino: TIME_UP received.")
            send_desktop_notification("【Eye Care】20分経過", "遠くを20秒間眺めて、目を休めてください。")

            print("\n--------------------------------------------------")
            print(" 【アイケア時間】20秒間、遠くを眺めてください。")
            print("--------------------------------------------------")

            for remaining in range(20, -1, -1):
                print(f" ⏳ あと {remaining} 秒 ", end="\r")
                time.sleep(1)

            print("\n\n休憩完了。再開を待機しています...")
            print("  -> 超音波センサーに1秒間手をかざす (手かざし再開)")

            with lock:
                waiting_for_reset = True

            # バックグラウンドでEnterキーを監視するスレッドを起動
            t = threading.Thread(target=enter_monitor_thread, args=(sock,), daemon=True)
            t.start()

        elif msg == "RESTART_EVENT":
            with lock:
                if not waiting_for_reset:
                    # すでにEnterキー再開済み、または重複パケットなので無視
                    print("  (RESTART_EVENT ignored: already handled)")
                    continue
                waiting_for_reset = False

            print(f"[{datetime.now().strftime('%H:%M:%S')}] 🖐 Arduino: RESTART_EVENT received (hand gesture).")
            # 手かざし再開の場合はArduinoからのイベントを受け取ってログ送信
            log_to_google_sheets("HAND_GESTURE")
            print("\nWaiting for next cycle...")

except KeyboardInterrupt:
    print("\nGateway stopped.")
finally:
    sock.close()
