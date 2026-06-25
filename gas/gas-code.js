// ============================================================
//  Smart Eye-Care System — Google Apps Script
//  シート構成：
//    「ログ」       … doPostで記録が蓄積されるデータシート
//    「週間グラフ」  … 過去7日間の1日あたり記録回数 + 棒グラフ
//    「今日のグラフ」… 今日の時間帯別記録回数 + 棒グラフ
// ============================================================

const LOG_SHEET_NAME    = "ログ";
const WEEKLY_SHEET_NAME = "週間グラフ";
const DAILY_SHEET_NAME  = "今日のグラフ";

// ------------------------------------------------------------
//  Arduino または Python からの POST を受け取り、ログを記録してグラフを更新する
// ------------------------------------------------------------
function doPost(e) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();

  // ログシートがなければ作成してヘッダを追加
  let logSheet = ss.getSheetByName(LOG_SHEET_NAME);
  if (!logSheet) {
    logSheet = ss.insertSheet(LOG_SHEET_NAME);
    logSheet.getRange(1, 1, 1, 3).setValues([["タイムスタンプ", "メッセージ", "トリガー"]]);
    logSheet.getRange(1, 1, 1, 3).setFontWeight("bold");
    logSheet.setFrozenRows(1);
  }

  const data = JSON.parse(e.postData.contents);
  const client = data.client || ""; // "pc" または "arduino"
  const trigger = data.trigger || "";

  // 日付変更を検知して前日分をアーカイブ化
  const lastRow = logSheet.getLastRow();
  const today = new Date();
  let archiveData = null;

  if (lastRow > 1) {
    const lastLogDate = new Date(logSheet.getRange(lastRow, 1).getValue());
    if (toDateKey_(lastLogDate) !== toDateKey_(today)) {
      try {
        // 1. 前日のデータで一度グラフを描画して確定させる
        updateDashboard_(lastLogDate);
        
        // 2. 送信元がPCの場合のみ、前日のグラフ画像をBase64で取得してレスポンスに含める
        // Arduino直接送信時はメモリ不足（ハングアップ）を防ぐため画像を送らない
        if (client === "pc") {
          archiveData = getDailyChartAsBase64_(lastLogDate);
        }
      } catch (err) {
        console.error("Archiving failed: " + err.toString());
      }
    }
  }

  // ログを追記
  logSheet.appendRow([today, data.message, trigger]);

  // 今日のグラフを更新
  updateDashboard_();

  // レスポンスを返却
  const response = {
    status: "OK",
    archive: archiveData // { filename: "...", data: "Base64..." } または null
  };

  return ContentService.createTextOutput(JSON.stringify(response))
                       .setMimeType(ContentService.MimeType.JSON);
}

// ------------------------------------------------------------
//  手動でグラフを再描画したいときはこの関数を実行する
// ------------------------------------------------------------
function updateDashboard() {
  updateDashboard_();
}

function updateDashboard_(targetDate) {
  updateWeeklyChart_(targetDate);
  updateDailyChart_(targetDate);
}

// ============================================================
//  週間グラフ — 過去7日間の1日あたり記録回数（縦棒グラフ）
// ============================================================
function updateWeeklyChart_(targetDate) {
  const ss       = SpreadsheetApp.getActiveSpreadsheet();
  const logSheet = ss.getSheetByName(LOG_SHEET_NAME);
  if (!logSheet) return;

  let weeklySheet = ss.getSheetByName(WEEKLY_SHEET_NAME);
  if (!weeklySheet) {
    weeklySheet = ss.insertSheet(WEEKLY_SHEET_NAME);
  }
  weeklySheet.clearContents();

  const baseDate = targetDate || new Date();
  const dates = [];
  for (let i = 6; i >= 0; i--) {
    const d = new Date(baseDate);
    d.setDate(baseDate.getDate() - i);
    dates.push(d);
  }

  const counts = {};
  dates.forEach(d => { counts[toDateKey_(d)] = 0; });

  const lastRow = logSheet.getLastRow();
  if (lastRow > 1) {
    const logData = logSheet.getRange(2, 1, lastRow - 1, 1).getValues();
    logData.forEach(row => {
      if (!row[0]) return;
      const key = toDateKey_(new Date(row[0]));
      if (key in counts) counts[key]++;
    });
  }

  weeklySheet.getRange(1, 1, 1, 2).setValues([["日付", "記録回数"]]);
  weeklySheet.getRange(1, 1, 1, 2).setFontWeight("bold");
  const rows = dates.map(d => [toDateLabelJP_(d), counts[toDateKey_(d)]]);
  weeklySheet.getRange(2, 1, rows.length, 2).setValues(rows);
  weeklySheet.setColumnWidth(1, 120);

  weeklySheet.getCharts().forEach(c => weeklySheet.removeChart(c));

  const chart = weeklySheet.newChart()
    .setChartType(Charts.ChartType.COLUMN)
    .addRange(weeklySheet.getRange(2, 1, rows.length, 2))
    .setPosition(2, 4, 0, 0)
    .setOption("title", "週間アイケア回数（過去7日間）")
    .setOption("titleTextStyle", { fontSize: 14, bold: true })
    .setOption("hAxis.title", "日付")
    .setOption("vAxis.title", "記録回数")
    .setOption("vAxis.minValue", 0)
    .setOption("vAxis.viewWindow.min", 0)
    .setOption("vAxis.format", "0")
    .setOption("legend.position", "none")
    .setOption("colors", ["#0288D1"])
    .setOption("bar.groupWidth", "60%")
    .setOption("width",  520)
    .setOption("height", 320)
    .build();

  weeklySheet.insertChart(chart);
}

// ============================================================
//  今日のグラフ — 時間帯別記録回数（縦棒グラフ）
// ============================================================
function updateDailyChart_(targetDate) {
  const ss       = SpreadsheetApp.getActiveSpreadsheet();
  const logSheet = ss.getSheetByName(LOG_SHEET_NAME);
  if (!logSheet) return;

  let dailySheet = ss.getSheetByName(DAILY_SHEET_NAME);
  if (!dailySheet) {
    dailySheet = ss.insertSheet(DAILY_SHEET_NAME);
  }
  dailySheet.clearContents();

  const baseDate = targetDate || new Date();
  const baseKey  = toDateKey_(baseDate);
  const hourCounts = new Array(24).fill(0);

  const lastRow = logSheet.getLastRow();
  if (lastRow > 1) {
    const logData = logSheet.getRange(2, 1, lastRow - 1, 1).getValues();
    logData.forEach(row => {
      if (!row[0]) return;
      const d = new Date(row[0]);
      if (toDateKey_(d) === baseKey) hourCounts[d.getHours()]++;
    });
  }

  dailySheet.getRange(1, 1, 1, 2).setValues([["時間帯(時)", "記録回数"]]);
  dailySheet.getRange(1, 1, 1, 2).setFontWeight("bold");
  const rows = hourCounts.map((count, h) => ["'" + h + "-" + (h + 1), count]);
  dailySheet.getRange(2, 1, 24, 2).setValues(rows);
  dailySheet.setColumnWidth(1, 80);

  dailySheet.getCharts().forEach(c => dailySheet.removeChart(c));

  const chart = dailySheet.newChart()
    .setChartType(Charts.ChartType.COLUMN)
    .addRange(dailySheet.getRange(2, 1, 24, 2))
    .setPosition(2, 4, 0, 0)
    .setOption("title", `今日のアイケア記録（${toDateLabelJP_(baseDate)}）`)
    .setOption("titleTextStyle", { fontSize: 14, bold: true })
    .setOption("hAxis.title", "時間帯(時)")
    .setOption("vAxis.title", "記録回数")
    .setOption("vAxis.minValue", 0)
    .setOption("vAxis.viewWindow.min", 0)
    .setOption("vAxis.format", "0")
    .setOption("legend.position", "none")
    .setOption("colors", ["#388E3C"])
    .setOption("bar.groupWidth", "70%")
    .setOption("width",  620)
    .setOption("height", 320)
    .build();

  dailySheet.insertChart(chart);
}

// ------------------------------------------------------------
//  ダッシュボードのグラフを画像Blobとして取得し、Base64文字列に変換する
// ------------------------------------------------------------
function getDailyChartAsBase64_(archiveDate) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const dailySheet = ss.getSheetByName(DAILY_SHEET_NAME);
  if (!dailySheet) return null;

  const charts = dailySheet.getCharts();
  if (charts.length === 0) return null;
  const chart = charts[0];

  const formattedDate = toDateKey_(archiveDate).replace(/-/g, ""); // 例: "20260623"
  const fileName = `グラフ_${formattedDate}.png`;

  // グラフを画像Blobとして取得し、Base64エンコード
  const imageBlob = chart.getAs('image/png');
  const base64Data = Utilities.base64Encode(imageBlob.getBytes());

  return {
    filename: fileName,
    data: base64Data
  };
}


// ============================================================
//  ユーティリティ
// ============================================================

// "YYYY-MM-DD" 形式のキーを返す（日付の比較用）
function toDateKey_(d) {
  return `${d.getFullYear()}-${pad_(d.getMonth() + 1)}-${pad_(d.getDate())}`;
}

// "M/D(曜)" 形式のラベルを返す（グラフ軸表示用）
function toDateLabelJP_(d) {
  const DOW = ["日", "月", "火", "水", "木", "金", "土"];
  return `${d.getMonth() + 1}/${d.getDate()}(${DOW[d.getDay()]})`;
}

// 2桁ゼロ埋め
function pad_(n) {
  return String(n).padStart(2, "0");
}

// 今すぐ現在のグラフを画像としてBase64デバッグ出力する関数
function testArchive() {
  const today = new Date();
  const res = getDailyChartAsBase64_(today);
  if (res) {
    console.log("テストアーカイブ取得成功: " + res.filename);
    console.log("Base64データ長: " + res.data.length);
  } else {
    console.log("テストアーカイブ取得失敗");
  }
}

// 昨日のダッシュボードを集計し、画像として手動保存する（旧Googleドライブ保存機能は廃止されました）
function archiveYesterday() {
  console.log("マニュアル画像保存はPythonゲートウェイ経由で自動処理されます。");
}
