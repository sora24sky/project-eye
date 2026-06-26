# --- 設定 ---
# GoogleスプレッドシートからデプロイしたWebアプリ（GAS）のURLを設定してください
$GAS_URL = "YOUR_GAS_SCRIPT_URL"

# ログ保存先ディレクトリ（マイドキュメント配下のフォルダを指定）
$docPath = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
$ARCHIVE_DIR = Join-Path $docPath "Arduino\eyecare-0624\archive"
$LOG_FILE = Join-Path $ARCHIVE_DIR "download.log"

if (-not (Test-Path $ARCHIVE_DIR)) {
    New-Item -ItemType Directory -Force -Path $ARCHIVE_DIR | Out-Null
}

$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
"[$timestamp] Script started" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8

try {
    "[$timestamp] Fetching from GAS..." | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
    $response = Invoke-RestMethod -Uri $GAS_URL -Method Get -TimeoutSec 20
    "[$timestamp] Response received. Status: $($response.status)" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
    
    if ($response.status -eq "OK" -and $response.archive) {
        $filename = $response.archive.filename
        $b64_data = $response.archive.data
        "[$timestamp] Filename: $filename, Data length: $($b64_data.Length)" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
        
        $filepath = Join-Path $ARCHIVE_DIR $filename
        if (-not (Test-Path $filepath)) {
            "[$timestamp] Decoding base64 and writing to $filepath" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
            $bytes = [System.Convert]::FromBase64String($b64_data)
            [System.IO.File]::WriteAllBytes($filepath, $bytes)
            "[$timestamp] Success" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
        } else {
            "[$timestamp] File already exists: $filepath" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
        }
    } else {
        "[$timestamp] No archive found or status not OK" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
    }
} catch {
    $err = $_.Exception.Message
    "[$timestamp] ERROR: $err" | Out-File -FilePath $LOG_FILE -Append -Encoding UTF8
}
