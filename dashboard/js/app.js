// Page glue: load the config, fetch readings, draw. The logic it relies on is
// in readings.js and format.js (tested in Node); this file only touches the
// DOM, Supabase and Chart.js.
//
// index.html?demo          made-up data, no Supabase needed
// index.html?demo=offline  the same, with the newest reading 3 h old

import { chartData, eventsFrom, statusOf } from "./readings.js";
import { deg, formatAge, localTime } from "./format.js";

const params = new URLSearchParams(location.search);
const demo = params.has("demo");
const $ = (id) => document.getElementById(id);

async function loadConfig() {
  try {
    return (await import("../config.js")).CONFIG;
  } catch (e) {
    if (demo) return (await import("../config.example.js")).CONFIG;
    throw new Error("dashboard/config.js is missing. Copy config.example.js to config.js and fill it in (see dashboard/README.md).");
  }
}

// Only the columns the page uses.
const COLUMNS = "ts,time_synced,trigger,pitch_d,roll_d,tilt_d,temp_c,level,event";

async function fetchReadings(cfg, client) {
  if (demo) {
    const { demoReadings } = await import("./demo.js");
    return demoReadings(new Date(), { offline: params.get("demo") === "offline" });
  }
  const { data, error } = await client
    .from("real_readings")
    .select(COLUMNS)
    .eq("device_id", cfg.deviceId)
    .order("ts", { ascending: false })
    .limit(cfg.maxReadings);
  if (error) throw new Error(`Supabase: ${error.message}`);
  return data;
}

function showMessage(text) {
  $("message").textContent = text;
  $("message").hidden = !text;
}

function renderStatus(s) {
  const level = $("level");
  const name = s.latest ? s.latest.level : "–";
  level.textContent = name;
  level.className = `level level-${s.latest ? name : "unknown"}`;

  const r = s.latest;
  $("last-time").textContent = r ? `${localTime(r.ts)} (${formatAge(s.ageMinutes)} ago)` : "–";
  $("last-tilt").textContent = r ? deg(r.tilt_d) : "–";
  $("last-pr").textContent = r ? `${deg(r.pitch_d)} / ${deg(r.roll_d)}` : "–";
  $("last-temp").textContent = r && r.temp_c != null ? `${r.temp_c.toFixed(1)} °C` : "–";
  if (r && r.event === "sensor_error") $("last-tilt").textContent = "sensor error";

  const off = $("offline");
  off.hidden = !s.offline;
  off.textContent = r
    ? `OFFLINE: no reading for ${formatAge(s.ageMinutes)}. The level shown may be out of date.`
    : "OFFLINE: no readings yet.";
}

const EVENT_TEXT = {
  shock: () => "Shock (motion, no tilt change)",
  sensor_error: () => "Sensor error",
  level: (e) => `Level ${e.from} → ${e.to}`,
};

function renderEvents(events) {
  const list = $("events");
  list.replaceChildren();
  for (const e of events.slice(0, 100)) {
    const li = document.createElement("li");
    const time = document.createElement("time");
    time.dateTime = e.ts;
    time.textContent = localTime(e.ts);
    const text = document.createElement("span");
    const tag = document.createElement("span");
    tag.className = `tag tag-${e.kind === "level" ? e.to : e.kind}`;
    tag.textContent = EVENT_TEXT[e.kind](e);
    text.append(tag);
    if (e.kind !== "sensor_error" && e.tilt != null) text.append(` · tilt ${deg(e.tilt)}`);
    li.append(time, text);
    list.append(li);
  }
  $("no-events").hidden = events.length > 0;
}

const charts = {};

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function timeAxis() {
  return {
    type: "linear",
    ticks: {
      maxTicksLimit: 5,
      color: cssVar("--muted"),
      callback: (v) => new Date(v).toLocaleString(undefined, { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }),
    },
    grid: { color: cssVar("--line") },
  };
}

function line(label, data, color, extra = {}) {
  return { label, data, borderColor: color, backgroundColor: color, borderWidth: 1.5, pointRadius: 0, pointHitRadius: 6, spanGaps: false, ...extra };
}

function drawChart(id, datasets) {
  const Chart = window.Chart;
  const options = {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    parsing: false,
    interaction: { mode: "nearest", axis: "x", intersect: false },
    scales: { x: timeAxis(), y: { ticks: { color: cssVar("--muted") }, grid: { color: cssVar("--line") } } },
    plugins: {
      legend: { labels: { color: cssVar("--text"), boxWidth: 12 } },
      tooltip: { callbacks: { title: (items) => (items.length ? localTime(items[0].parsed.x) : "") } },
    },
  };
  if (charts[id]) charts[id].destroy();
  charts[id] = new Chart($(id), { type: "line", data: { datasets }, options });
}

function renderCharts(d, cfg) {
  const dash = { borderDash: [6, 4], borderWidth: 1.5 };
  drawChart("tilt-chart", [
    line("Combined", d.tilt, cssVar("--tilt"), { borderWidth: 2 }),
    line("Pitch", d.pitch, cssVar("--pitch")),
    line("Roll", d.roll, cssVar("--roll")),
    line(`WATCH ${cfg.watchDeg}°`, d.watch, cssVar("--watch"), dash),
    line(`ALERT ${cfg.alertDeg}°`, d.alert, cssVar("--alert"), dash),
  ]);
  drawChart("temp-chart", [line("Temperature", d.temp, cssVar("--temp"), { borderWidth: 2 })]);
}

async function main() {
  let cfg;
  try {
    cfg = await loadConfig();
  } catch (e) {
    showMessage(e.message);
    return;
  }
  $("device").textContent = demo ? `${cfg.deviceId} (demo data)` : cfg.deviceId;

  const client = demo ? null : window.supabase.createClient(cfg.supabaseUrl, cfg.supabaseKey, {
    auth: { persistSession: false, autoRefreshToken: false },
  });

  async function refresh() {
    try {
      const rows = await fetchReadings(cfg, client);
      const now = new Date();
      showMessage("");
      renderStatus(statusOf(rows, now, cfg));
      renderCharts(chartData(rows, cfg), cfg);
      renderEvents(eventsFrom(rows));
      $("updated").textContent = localTime(now);
    } catch (e) {
      // Keep whatever was shown last; say why it isn't fresh.
      showMessage(`Couldn't load readings: ${e.message}`);
      console.warn(e);
    }
  }

  $("refresh").addEventListener("click", refresh);
  // Also refresh when the phone brings the page back to the front.
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") refresh();
  });
  setInterval(refresh, cfg.refreshMinutes * 60 * 1000);
  await refresh();
}

main();
