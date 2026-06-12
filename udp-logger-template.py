import socket
import urllib.request
import json
import time
import os
import subprocess
from datetime import datetime

# --- 設定 (各自の環境に合わせて書き換えてください) ---
GAS_URL = "YOUR_GOOGLE_APPS_SCRIPT_URL"
UDP_IP = "0.0.0.0"
UDP_PORT = 5005

def send_desktop_notification(title, message):
    """Windows標準のトースト通知を表示"""
    ps_cmd = f'Add-Type -AssemblyName System.Windows.Forms; $b = New-Object System.Windows.Forms.NotifyIcon; $b.Icon = [System.Drawing.SystemIcons]::Information; $b.BalloonTipTitle = "{title}"; $b.BalloonTipText = "{message}"; $b.Visible = $true; $b.ShowBalloonTip(5000)'
    subprocess.Popen(["powershell", "-Command", ps_cmd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def flush_input_buffer():
    """Windows用の入力バッファクリア"""
    import msvcrt
    while msvcrt.kbhit(): msvcrt.getch()

# --- メイン処理 ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print("==================================================")
print("  PC Smart Eye-Care Gateway (Template)")
print("==================================================")
print(f"Listening on UDP port {UDP_PORT}...\n")

try:
    while True:
        data, addr = sock.recvfrom(1024)
        if data.decode('utf-8', errors='ignore') == "TIME_UP":
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 🚨 Arduino: TIME_UP received.")
            
            send_desktop_notification("【Eye Care】20分経過", "遠くを20秒間眺めて、目を休めてください。")

            print("\n--------------------------------------------------")
            print(" 【アイケア時間】20秒間、遠くを眺めてください。")
            print("--------------------------------------------------")

            for remaining in range(20, -1, -1):
                print(f" ⏳ あと {remaining} 秒 ", end="\r")
                time.sleep(1)
            
            flush_input_buffer()
            input("\n\n20秒経過しました。[Enter] を押して再開してください。")

            # 先に Arduino をリセットして待機状態に戻す
            sock.sendto(b"RESET_OK", addr)
            print(">> Sent RESET signal to Arduino.")

            # Google Sheets に記録
            print("--> Logging to Google Sheets...")
            try:
                payload = json.dumps({"message": "EYE_CARE_COMPLETE"}).encode("utf-8")
                req = urllib.request.Request(GAS_URL, data=payload, headers={"Content-Type": "application/json"}, method="POST")
                with urllib.request.urlopen(req, timeout=5) as res:
                    print(f"[SUCCESS] Logged: {res.read().decode('utf-8')}")
            except Exception as e:
                print(f"[ERROR] Logging failed: {e}")
            
            print("\nWaiting for next cycle...")

except KeyboardInterrupt:
    print("\nGateway stopped.")
finally:
    sock.close()
