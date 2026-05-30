// ============================================================
//  web_ui.h  —  Embedded Single-Page Web GUI
//  2D Spatial Mapping System
//
//  This header contains the entire HTML/CSS/JS interface as a
//  C++11 raw string literal (R"rawstr(...)rawstr").  It is
//  served by ESPAsyncWebServer on the root "/" route.
//
//  Design:  Industrial / Scientific — dark background, amber
//           accent, monospaced readouts, subtle grid texture.
//  Font:    "Share Tech Mono" (Google Fonts CDN) for displays;
//           "Barlow Condensed" for labels/headings.
//  AJAX:    Fetch API — no page reloads.  Polling is opt-in via
//           the manual "Get Current Distances" button only.
// ============================================================
#pragma once

// The R"UI( ... )UI" delimiter avoids escaping every quote and backslash.
static const char INDEX_HTML[] PROGMEM = R"UI(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Spatial Scanner</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Barlow+Condensed:wght@300;400;600;700&display=swap">
<style>
  /* ── Design Tokens ─────────────────────────────────────── */
  :root {
    --bg:           #0a0c0f;
    --bg2:          #0e1217;
    --bg3:          #141920;
    --border:       #1e2730;
    --border-bright:#2d3c4a;
    --amber:        #f5a623;
    --amber-dim:    #9b6a16;
    --amber-glow:   rgba(245,166,35,0.12);
    --cyan:         #00d4ff;
    --cyan-dim:     rgba(0,212,255,0.15);
    --red:          #ff4b4b;
    --green:        #2dff7e;
    --text-primary: #e8edf2;
    --text-muted:   #4a5a6a;
    --text-label:   #8a9aaa;
    --font-mono:    'Share Tech Mono', monospace;
    --font-ui:      'Barlow Condensed', sans-serif;
    --radius:       4px;
    --glow-amber:   0 0 12px rgba(245,166,35,0.4);
    --glow-cyan:    0 0 12px rgba(0,212,255,0.35);
  }

  /* ── Reset & Base ──────────────────────────────────────── */
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background-color: var(--bg);
    color: var(--text-primary);
    font-family: var(--font-ui);
    min-height: 100vh;
    /* Subtle engineering grid background */
    background-image:
      linear-gradient(rgba(0,212,255,0.025) 1px, transparent 1px),
      linear-gradient(90deg, rgba(0,212,255,0.025) 1px, transparent 1px);
    background-size: 40px 40px;
    padding: 24px 16px 48px;
  }

  /* ── Header ────────────────────────────────────────────── */
  .header {
    display: flex;
    align-items: flex-end;
    gap: 20px;
    margin-bottom: 32px;
    padding-bottom: 20px;
    border-bottom: 1px solid var(--border);
    position: relative;
  }
  .header::after {
    content: '';
    position: absolute;
    bottom: -1px; left: 0;
    width: 120px; height: 2px;
    background: var(--amber);
    box-shadow: var(--glow-amber);
  }
  .header-icon {
    width: 48px; height: 48px;
    border: 2px solid var(--amber);
    border-radius: var(--radius);
    display: flex; align-items: center; justify-content: center;
    box-shadow: var(--glow-amber), inset 0 0 12px var(--amber-glow);
    flex-shrink: 0;
  }
  .header-icon svg { width: 26px; height: 26px; }
  .header-text h1 {
    font-size: clamp(1.4rem, 3.5vw, 2rem);
    font-weight: 700;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    color: var(--text-primary);
    line-height: 1;
  }
  .header-text p {
    font-size: 0.78rem;
    letter-spacing: 0.2em;
    text-transform: uppercase;
    color: var(--text-muted);
    margin-top: 4px;
    font-weight: 300;
  }
  .status-pill {
    margin-left: auto;
    display: flex; align-items: center; gap: 8px;
    font-family: var(--font-mono);
    font-size: 0.72rem;
    color: var(--text-label);
    border: 1px solid var(--border);
    padding: 6px 12px;
    border-radius: var(--radius);
    background: var(--bg2);
    flex-shrink: 0;
  }
  .status-dot {
    width: 7px; height: 7px;
    border-radius: 50%;
    background: var(--green);
    box-shadow: 0 0 6px var(--green);
    animation: pulse-dot 2s ease-in-out infinite;
  }
  @keyframes pulse-dot {
    0%,100% { opacity:1; } 50% { opacity:0.4; }
  }

  /* ── Layout Grid ───────────────────────────────────────── */
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 16px;
    max-width: 900px;
    margin: 0 auto;
  }
  @media (max-width: 620px) { .grid { grid-template-columns: 1fr; } }
  .col-full { grid-column: 1 / -1; }

  /* ── Panel / Card ──────────────────────────────────────── */
  .panel {
    background: var(--bg2);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 20px;
    position: relative;
    overflow: hidden;
  }
  .panel::before {
    /* Top-left corner accent */
    content: '';
    position: absolute;
    top: 0; left: 0;
    width: 32px; height: 3px;
    background: var(--amber);
  }
  .panel-title {
    font-size: 0.65rem;
    font-weight: 600;
    letter-spacing: 0.22em;
    text-transform: uppercase;
    color: var(--amber);
    margin-bottom: 16px;
    display: flex; align-items: center; gap: 8px;
  }
  .panel-title::after {
    content: '';
    flex: 1;
    height: 1px;
    background: var(--border);
  }

  /* ── Buttons ────────────────────────────────────────────── */
  .btn-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
  }
  .btn {
    font-family: var(--font-ui);
    font-size: 0.82rem;
    font-weight: 600;
    letter-spacing: 0.1em;
    text-transform: uppercase;
    padding: 12px 10px;
    border: 1px solid;
    border-radius: var(--radius);
    cursor: pointer;
    transition: all 0.15s ease;
    position: relative;
    overflow: hidden;
    white-space: nowrap;
  }
  .btn::before {
    content: '';
    position: absolute;
    inset: 0;
    background: currentColor;
    opacity: 0;
    transition: opacity 0.15s;
  }
  .btn:hover::before { opacity: 0.08; }
  .btn:active { transform: translateY(1px); }
  .btn:disabled {
    opacity: 0.35;
    cursor: not-allowed;
    transform: none;
  }
  .btn:disabled::before { display: none; }

  /* CW = amber themed */
  .btn-cw {
    background: transparent;
    border-color: var(--amber);
    color: var(--amber);
  }
  .btn-cw:not(:disabled):hover {
    background: var(--amber-glow);
    box-shadow: var(--glow-amber);
  }
  /* CCW = cyan themed */
  .btn-ccw {
    background: transparent;
    border-color: var(--cyan);
    color: var(--cyan);
  }
  .btn-ccw:not(:disabled):hover {
    background: var(--cyan-dim);
    box-shadow: var(--glow-cyan);
  }

  /* Motor busy indicator */
  .motor-status {
    margin-top: 14px;
    display: flex; align-items: center; gap: 10px;
    font-family: var(--font-mono);
    font-size: 0.7rem;
    color: var(--text-muted);
    min-height: 22px;
  }
  .motor-status.busy { color: var(--amber); }
  .motor-status.busy .m-dot {
    background: var(--amber);
    box-shadow: 0 0 6px var(--amber);
  }
  .m-dot {
    width: 6px; height: 6px;
    border-radius: 50%;
    background: var(--text-muted);
    flex-shrink: 0;
  }
  /* Spinner for motor busy */
  @keyframes spin { to { transform: rotate(360deg); } }
  .spinner {
    width: 12px; height: 12px;
    border: 2px solid var(--amber-dim);
    border-top-color: var(--amber);
    border-radius: 50%;
    animation: spin 0.7s linear infinite;
    flex-shrink: 0;
  }

  /* ── Sensor Readout ─────────────────────────────────────── */
  .sensor-fetch-btn {
    width: 100%;
    background: transparent;
    border: 1px solid var(--border-bright);
    color: var(--text-primary);
    padding: 13px;
    font-family: var(--font-ui);
    font-size: 0.82rem;
    font-weight: 600;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    border-radius: var(--radius);
    cursor: pointer;
    transition: all 0.15s ease;
    margin-bottom: 18px;
    display: flex; align-items: center; justify-content: center; gap: 10px;
  }
  .sensor-fetch-btn:hover {
    border-color: var(--cyan);
    color: var(--cyan);
    box-shadow: var(--glow-cyan);
    background: var(--cyan-dim);
  }
  .sensor-fetch-btn:disabled { opacity: 0.4; cursor: not-allowed; }

  .sensor-table {
    width: 100%;
    border-collapse: collapse;
  }
  .sensor-table tr { border-bottom: 1px solid var(--border); }
  .sensor-table tr:last-child { border-bottom: none; }
  .sensor-table td {
    padding: 12px 4px;
    font-size: 0.82rem;
  }
  .sensor-table .s-icon {
    width: 32px;
    text-align: center;
  }
  .sensor-table .s-icon span {
    display: inline-flex; align-items: center; justify-content: center;
    width: 26px; height: 26px;
    border: 1px solid var(--border);
    border-radius: 3px;
    background: var(--bg3);
    font-size: 0.7rem;
    font-family: var(--font-mono);
    color: var(--text-muted);
  }
  .sensor-table .s-name {
    color: var(--text-label);
    font-weight: 300;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    font-size: 0.75rem;
    padding-left: 8px;
  }
  .sensor-table .s-type {
    color: var(--text-muted);
    font-size: 0.65rem;
    display: block;
    margin-top: 2px;
    font-family: var(--font-mono);
  }
  .sensor-table .s-val {
    text-align: right;
    font-family: var(--font-mono);
    font-size: 1.05rem;
    font-weight: 400;
    color: var(--text-primary);
    min-width: 90px;
    transition: color 0.3s;
  }
  .sensor-table .s-val.updated { color: var(--cyan); }
  .sensor-table .s-val.error   { color: var(--red); font-size: 0.75rem; }
  .sensor-table .s-bar-wrap {
    padding-left: 10px;
    width: 80px;
  }
  .s-bar-bg {
    height: 3px;
    background: var(--bg3);
    border-radius: 2px;
    overflow: hidden;
  }
  .s-bar-fill {
    height: 100%;
    background: var(--cyan);
    border-radius: 2px;
    width: 0%;
    transition: width 0.5s ease, background 0.3s;
  }
  .s-bar-fill.warn { background: var(--amber); }

  .last-updated {
    margin-top: 14px;
    font-family: var(--font-mono);
    font-size: 0.65rem;
    color: var(--text-muted);
    text-align: right;
    letter-spacing: 0.05em;
  }

  /* ── Position tracker ──────────────────────────────────── */
  .pos-display {
    display: flex;
    align-items: baseline;
    gap: 8px;
    margin-top: 4px;
  }
  .pos-value {
    font-family: var(--font-mono);
    font-size: 1.8rem;
    color: var(--amber);
    line-height: 1;
    letter-spacing: -0.02em;
  }
  .pos-unit {
    font-size: 0.75rem;
    color: var(--text-muted);
    letter-spacing: 0.1em;
    font-family: var(--font-mono);
  }
  .pos-sub {
    font-size: 0.7rem;
    color: var(--text-muted);
    font-family: var(--font-mono);
    margin-top: 6px;
  }

  /* ── Radar Arc SVG ─────────────────────────────────────── */
  .radar-wrap {
    display: flex;
    justify-content: center;
    align-items: center;
    padding: 12px 0 4px;
  }
  #radarSvg { overflow: visible; }
  .radar-arm {
    transform-origin: 100px 100px;
    transition: transform 0.4s ease;
  }

  /* ── Notification toast ─────────────────────────────────── */
  #toast {
    position: fixed;
    bottom: 24px; right: 24px;
    background: var(--bg3);
    border: 1px solid var(--border-bright);
    color: var(--text-primary);
    font-family: var(--font-mono);
    font-size: 0.72rem;
    padding: 10px 16px;
    border-radius: var(--radius);
    opacity: 0;
    transform: translateY(8px);
    transition: opacity 0.2s, transform 0.2s;
    pointer-events: none;
    max-width: 260px;
    z-index: 999;
  }
  #toast.show { opacity: 1; transform: translateY(0); }
  #toast.err  { border-color: var(--red); color: var(--red); }
</style>
</head>
<body>

<!-- ══════════════════ HEADER ══════════════════════════════ -->
<header class="header" style="max-width:900px;margin:0 auto 32px;">
  <div class="header-icon">
    <svg viewBox="0 0 24 24" fill="none" stroke="#f5a623" stroke-width="1.5" stroke-linecap="round">
      <circle cx="12" cy="12" r="3"/>
      <path d="M12 2v3M12 19v3M2 12h3M19 12h3"/>
      <path d="M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M5.6 18.4l2.1-2.1M16.3 7.7l2.1-2.1"/>
      <circle cx="12" cy="12" r="8" stroke-dasharray="3 4" opacity=".4"/>
    </svg>
  </div>
  <div class="header-text">
    <h1>Spatial Scanner</h1>
    <p>ESP32 &bull; 2D Mapping System &bull; AP Mode</p>
  </div>
  <div class="status-pill">
    <div class="status-dot"></div>
    ONLINE &mdash; <span id="apSsid">ESP32_Scanner</span>
  </div>
</header>

<!-- ══════════════════ MAIN GRID ═══════════════════════════ -->
<main class="grid" style="max-width:900px;margin:0 auto;">

  <!-- ── Motor Controls ─────────────────────────────────── -->
  <div class="panel">
    <div class="panel-title">Motor Control</div>

    <div class="btn-grid">
      <button class="btn btn-cw" onclick="motorCmd('/motor/cw360')">
        &#8635; 360° CW
      </button>
      <button class="btn btn-ccw" onclick="motorCmd('/motor/ccw360')">
        &#8634; 360° CCW
      </button>
      <button class="btn btn-cw" onclick="motorCmd('/motor/cw45')">
        &#8635; 45° CW
      </button>
      <button class="btn btn-ccw" onclick="motorCmd('/motor/ccw45')">
        &#8634; 45° CCW
      </button>
    </div>

    <div class="motor-status" id="motorStatus">
      <div class="m-dot"></div>
      <span>IDLE</span>
    </div>
  </div>

  <!-- ── Position / Radar ───────────────────────────────── -->
  <div class="panel">
    <div class="panel-title">Estimated Position</div>

    <div class="radar-wrap">
      <svg id="radarSvg" width="140" height="100" viewBox="0 0 200 110">
        <defs>
          <radialGradient id="rg" cx="50%" cy="100%" r="100%">
            <stop offset="0%"   stop-color="#00d4ff" stop-opacity="0.18"/>
            <stop offset="100%" stop-color="#00d4ff" stop-opacity="0"/>
          </radialGradient>
        </defs>
        <!-- Concentric arc rings -->
        <path d="M 20 100 A 80 80 0 0 1 180 100" fill="none" stroke="#1e2730" stroke-width="1"/>
        <path d="M 47 100 A 53 53 0 0 1 153 100" fill="none" stroke="#1e2730" stroke-width="1"/>
        <path d="M 74 100 A 26 26 0 0 1 126 100" fill="none" stroke="#1e2730" stroke-width="1"/>
        <!-- Reference spokes -->
        <line x1="100" y1="100" x2="20"  y2="100" stroke="#1e2730" stroke-width="1"/>
        <line x1="100" y1="100" x2="180" y2="100" stroke="#1e2730" stroke-width="1"/>
        <line x1="100" y1="100" x2="100" y2="20"  stroke="#1e2730" stroke-width="1"/>
        <line x1="100" y1="100" x2="43"  y2="43"  stroke="#1e2730" stroke-width="1" stroke-dasharray="2 3"/>
        <line x1="100" y1="100" x2="157" y2="43"  stroke="#1e2730" stroke-width="1" stroke-dasharray="2 3"/>
        <!-- Sweep fill -->
        <path id="radarFill" d="M 100 100 L 100 20 A 80 80 0 0 1 100 20 Z"
              fill="url(#rg)" opacity="0"/>
        <!-- Rotating arm -->
        <g class="radar-arm" id="radarArm">
          <line x1="100" y1="100" x2="100" y2="22"
                stroke="#f5a623" stroke-width="1.5" opacity="0.9"/>
          <circle cx="100" cy="23" r="3" fill="#f5a623" opacity="0.8"/>
        </g>
        <!-- Origin dot -->
        <circle cx="100" cy="100" r="3" fill="#f5a623"/>
        <!-- Angle labels -->
        <text x="100" y="16"  text-anchor="middle" fill="#4a5a6a" font-size="8" font-family="Share Tech Mono">0°</text>
        <text x="186" y="104" text-anchor="start"  fill="#4a5a6a" font-size="8" font-family="Share Tech Mono">90°</text>
        <text x="14"  y="104" text-anchor="end"    fill="#4a5a6a" font-size="8" font-family="Share Tech Mono">-90°</text>
      </svg>
    </div>

    <div class="pos-display">
      <span class="pos-value" id="posAngle">0.0</span>
      <span class="pos-unit">DEG</span>
    </div>
    <div class="pos-sub" id="posSteps">0 / 3200 steps &bull; 0 rev</div>
  </div>

  <!-- ── Sensor Readings ─────────────────────────────────── -->
  <div class="panel col-full">
    <div class="panel-title">Distance Sensors</div>

    <button class="sensor-fetch-btn" id="fetchBtn" onclick="fetchSensors()">
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
        <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>
      </svg>
      Get Current Distances
    </button>

    <table class="sensor-table">
      <tbody>
        <tr>
          <td class="s-icon"><span>LD</span></td>
          <td class="s-name">
            TF-Luna LiDAR
            <span class="s-type">UART &bull; 0.2m – 8m</span>
          </td>
          <td class="s-bar-wrap">
            <div class="s-bar-bg"><div class="s-bar-fill" id="bar0"></div></div>
          </td>
          <td class="s-val" id="val0">-- cm</td>
        </tr>
        <tr>
          <td class="s-icon"><span>IR</span></td>
          <td class="s-name">
            SHARP GP2Y
            <span class="s-type">Analog IR &bull; 20cm – 150cm</span>
          </td>
          <td class="s-bar-wrap">
            <div class="s-bar-bg"><div class="s-bar-fill" id="bar1"></div></div>
          </td>
          <td class="s-val" id="val1">-- cm</td>
        </tr>
        <tr>
          <td class="s-icon"><span>US</span></td>
          <td class="s-name">
            HC-SR04 Sonar
            <span class="s-type">Ultrasonic &bull; 2cm – 400cm</span>
          </td>
          <td class="s-bar-wrap">
            <div class="s-bar-bg"><div class="s-bar-fill" id="bar2"></div></div>
          </td>
          <td class="s-val" id="val2">-- cm</td>
        </tr>
      </tbody>
    </table>

    <div class="last-updated" id="lastUpdated">Last updated: never</div>
  </div>

</main>

<!-- Toast notification -->
<div id="toast"></div>

<script>
// ── Shared State ─────────────────────────────────────────────
let totalSteps = 0;         // Accumulated signed step count (tracks angle)
const MAX_RANGE = [800, 150, 400];  // Per-sensor max range for bar scaling (cm)

// ── Motor Command ─────────────────────────────────────────────
function motorCmd(endpoint) {
  const btns = document.querySelectorAll('.btn');
  btns.forEach(b => b.disabled = true);
  setMotorBusy(true);

  fetch(endpoint)
    .then(r => r.json())
    .then(data => {
      if (data.status === 'ok') {
        totalSteps += data.steps;   // Server echoes signed step delta
        updatePosition();
        pollMotorUntilDone();
      } else {
        showToast('Command rejected: ' + (data.msg || 'unknown'), true);
        setMotorBusy(false);
        btns.forEach(b => b.disabled = false);
      }
    })
    .catch(err => {
      showToast('Network error: ' + err.message, true);
      setMotorBusy(false);
      btns.forEach(b => b.disabled = false);
    });
}

// Poll /motor/status every 300 ms until motor reports idle
function pollMotorUntilDone() {
  const interval = setInterval(() => {
    fetch('/motor/status')
      .then(r => r.json())
      .then(data => {
        if (!data.busy) {
          clearInterval(interval);
          setMotorBusy(false);
          const btns = document.querySelectorAll('.btn');
          btns.forEach(b => b.disabled = false);
        }
      })
      .catch(() => clearInterval(interval));
  }, 300);
}

// ── Motor Busy UI ─────────────────────────────────────────────
function setMotorBusy(busy) {
  const el = document.getElementById('motorStatus');
  if (busy) {
    el.className = 'motor-status busy';
    el.innerHTML = '<div class="spinner"></div><span>RUNNING&hellip;</span>';
  } else {
    el.className = 'motor-status';
    el.innerHTML = '<div class="m-dot"></div><span>IDLE</span>';
  }
}

// ── Position Display ──────────────────────────────────────────
function updatePosition() {
  // Normalise steps to [-1800, 1800] for display (±180°)
  let deg = (totalSteps % 3200) * (360 / 3200);
  const revs = Math.floor(Math.abs(totalSteps) / 3200);

  document.getElementById('posAngle').textContent = deg.toFixed(1);
  document.getElementById('posSteps').textContent =
    `${totalSteps} steps total \u2022 ${revs} full rev`;

  // Rotate radar arm: 0° = up (−90° in SVG terms from right)
  // SVG coordinate: arm points up at 0 deg. Positive step = CW.
  const svgAngle = deg;  // degrees CW from top
  document.getElementById('radarArm').style.transform =
    `rotate(${svgAngle}deg)`;
}

// ── Sensor Fetch ──────────────────────────────────────────────
function fetchSensors() {
  const btn = document.getElementById('fetchBtn');
  btn.disabled = true;
  btn.innerHTML = `
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor"
         stroke-width="2.5" style="animation:spin 0.7s linear infinite">
      <path d="M23 4v6h-6"/><path d="M1 20v-6h6"/>
      <path d="M3.51 9a9 9 0 0114.36-3.36L23 10M1 14l5.13 5.13A9 9 0 0020.49 15"/>
    </svg>
    Reading sensors&hellip;`;

  fetch('/sensors/read')
    .then(r => r.json())
    .then(data => {
      updateSensorRow(0, data.lidar,  MAX_RANGE[0]);
      updateSensorRow(1, data.sharp,  MAX_RANGE[1]);
      updateSensorRow(2, data.hcsr04, MAX_RANGE[2]);

      const now = new Date();
      document.getElementById('lastUpdated').textContent =
        'Last updated: ' + now.toLocaleTimeString();
    })
    .catch(err => showToast('Sensor read failed: ' + err.message, true))
    .finally(() => {
      btn.disabled = false;
      btn.innerHTML = `
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
          <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>
        </svg>
        Get Current Distances`;
    });
}

// Update one row: value cell + progress bar
function updateSensorRow(index, valueCM, maxCM) {
  const valEl = document.getElementById('val' + index);
  const barEl = document.getElementById('bar' + index);

  if (valueCM < 0) {
    valEl.textContent = 'ERR';
    valEl.className = 's-val error';
    barEl.style.width = '0%';
    barEl.className = 's-bar-fill';
  } else {
    const pct = Math.min((valueCM / maxCM) * 100, 100);
    valEl.textContent = valueCM.toFixed(1) + ' cm';
    valEl.className = 's-val updated';
    barEl.style.width = pct + '%';
    barEl.className = 's-bar-fill' + (pct > 80 ? ' warn' : '');
    // Flash effect: revert colour class after transition
    setTimeout(() => { valEl.className = 's-val'; }, 1200);
  }
}

// ── Toast Notification ────────────────────────────────────────
let toastTimer;
function showToast(msg, isError = false) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className = 'show' + (isError ? ' err' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.className = ''; }, 3500);
}

// ── CSS keyframes for inline spinner in JS ────────────────────
const style = document.createElement('style');
style.textContent = '@keyframes spin { to { transform: rotate(360deg); } }';
document.head.appendChild(style);
</script>
</body>
</html>
)UI";
