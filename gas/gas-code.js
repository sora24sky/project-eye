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
//  Arduino からの POST を受け取り、ログを記録してグラフを更新する
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

  // 日付変更を検知して前日分をアーカイブ化
  const lastRow = logSheet.getLastRow();
  const today = new Date();
  if (lastRow > 1) {
    const lastLogDate = new Date(logSheet.getRange(lastRow, 1).getValue());
    if (toDateKey_(lastLogDate) !== toDateKey_(today)) {
      // 1. 前日のデータで一度グラフを描画して確定させる
      updateDashboard_(lastLogDate);
      // 2. そのグラフシートをアーカイブとして複製・固定化する
      archiveDailyChart_(lastLogDate);
    }
  }

  // ログを追記
  const data = JSON.parse(e.postData.contents);
  logSheet.appendRow([today, data.message, data.trigger]);

  // 今日のグラフを更新
  updateDashboard_();

  return ContentService.createTextOutput("OK");
}

// ------------------------------------------------------------
//  手動でグラフを再描画したいときはこの関数を実行する
// ------------------------------------------------------------
// 引数なしで実行されたときは自動的に「今日」を基準にする
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

  // シートを取得または作成
  let weeklySheet = ss.getSheetByName(WEEKLY_SHEET_NAME);
  if (!weeklySheet) {
    weeklySheet = ss.insertSheet(WEEKLY_SHEET_NAME);
  }
  weeklySheet.clearContents();

  // 指定日（または当日）を基準に過去7日分の日付を古い順に生成
  const baseDate = targetDate || new Date();
  const dates = [];
  for (let i = 6; i >= 0; i--) {
    const d = new Date(baseDate);
    d.setDate(baseDate.getDate() - i);
    dates.push(d);
  }

  // ログデータを取得してカウント
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

  // ヘッダ + データをシートに書き込み
  weeklySheet.getRange(1, 1, 1, 2).setValues([["日付", "記録回数"]]);
  weeklySheet.getRange(1, 1, 1, 2).setFontWeight("bold");
  const rows = dates.map(d => [toDateLabelJP_(d), counts[toDateKey_(d)]]);
  weeklySheet.getRange(2, 1, rows.length, 2).setValues(rows);
  weeklySheet.setColumnWidth(1, 120);

  // 既存グラフを削除して再作成
  weeklySheet.getCharts().forEach(c => weeklySheet.removeChart(c));

  const chart = weeklySheet.newChart()
    .setChartType(Charts.ChartType.COLUMN)
    .addRange(weeklySheet.getRange(2, 1, rows.length, 2)) // A2セル以降から描画
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

  // シートを取得または作成
  let dailySheet = ss.getSheetByName(DAILY_SHEET_NAME);
  if (!dailySheet) {
    dailySheet = ss.insertSheet(DAILY_SHEET_NAME);
  }
  dailySheet.clearContents();

  // 指定日（または当日）の時間帯別カウント（0〜23時）
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

  // ヘッダ + データをシートに書き込み
  dailySheet.getRange(1, 1, 1, 2).setValues([["時間帯(時)", "記録回数"]]);
  dailySheet.getRange(1, 1, 1, 2).setFontWeight("bold");
  const rows = hourCounts.map((count, h) => [`'${h}-${h+1}`, count]); // 先頭にシングルクォーテーションを付けてテキストとして強制
  dailySheet.getRange(2, 1, 24, 2).setValues(rows);
  dailySheet.setColumnWidth(1, 80);

  // 既存グラフを削除して再作成
  dailySheet.getCharts().forEach(c => dailySheet.removeChart(c));

  const chart = dailySheet.newChart()
    .setChartType(Charts.ChartType.COLUMN)
    .addRange(dailySheet.getRange(2, 1, 24, 2)) // A2セル以降から描画
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
//  前日のダッシュボードグラフを「グラフ_YYYYMMDD」として保存・固定化する
// ------------------------------------------------------------
function archiveDailyChart_(archiveDate) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const dailySheet = ss.getSheetByName(DAILY_SHEET_NAME);
  if (!dailySheet) return;

  const formattedDate = toDateKey_(archiveDate).replace(/-/g, ""); // 例: "20260622"
  const archiveName = `グラフ_${formattedDate}`;

  // 既に同名のアーカイブシートがあれば作成しない
  if (ss.getSheetByName(archiveName)) return;

  // 「今日のグラフ」を複製
  const archiveSheet = dailySheet.copyTo(ss);
  archiveSheet.setName(archiveName);

  // 複製されたグラフが、コピー先のシートのデータを参照するように紐付け直す（固定化）
  const charts = archiveSheet.getCharts();
  for (let i = 0; i < charts.length; i++) {
    const chart = charts[i];
    const ranges = chart.getRanges();
    const newChartBuilder = chart.modify();
    newChartBuilder.clearRanges();
    for (let j = 0; j < ranges.length; j++) {
      const range = ranges[j];
      const newRange = archiveSheet.getRange(range.getRow(), range.getColumn(), range.getNumRows(), range.getNumColumns());
      newChartBuilder.addRange(newRange);
    }
    archiveSheet.updateChart(newChartBuilder.build());
  }
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
