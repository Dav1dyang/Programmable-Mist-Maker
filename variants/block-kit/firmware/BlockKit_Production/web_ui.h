// Single-page web UI for the Mist Maker, embedded as one PROGMEM string.
// Vanilla HTML/CSS/JS — no external CDN, no LittleFS — so the UI loads with
// just one HTTP roundtrip and works offline on the LAN.
//
// Tabs: Status (quick-control tiles + live cards + sparkline), Config
// (advanced sliders collapsed by default), Debug (commands + log), About
// (device info + admin password).
//
// Update strategy: SSE subscription via EventSource('/api/events') drives
// the Status + Debug tabs at 4 Hz; Config does a one-shot GET on tab show
// and re-GETs after a successful save.

#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"==(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light">
<meta name="theme-color" content="#fcfbf8">
<title>Mist Maker</title>
<style>
:root{
  --bg:#fcfbf8; --card:#ffffff; --fg:#2d3848; --mut:#7a8395;
  --line:#eef0f3; --line-strong:#e2e5ea;
  --accent:#7a9bb9; --accent-soft:#e7eff7;
  --ok:#7fb89a; --ok-soft:#e6f1ec;
  --warn:#d6a865; --warn-soft:#f7ecd9;
  --err:#c47878; --err-soft:#f4dfdf;
  --r:14px; --r-sm:10px;
  --shadow:0 1px 2px rgba(40,55,80,.04), 0 4px 14px rgba(40,55,80,.05);
  --serif:Georgia,Cambria,"Times New Roman",serif;
  --sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,sans-serif;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0;background:var(--bg);color:var(--fg);
  font-family:var(--sans);font-size:16px;line-height:1.55;
  -webkit-font-smoothing:antialiased}
a{color:var(--accent);text-decoration:none}
a:hover{text-decoration:underline}
header{padding:24px 20px 16px;max-width:960px;margin:0 auto;display:flex;align-items:baseline;gap:14px;flex-wrap:wrap}
h1{font-family:var(--serif);font-size:26px;margin:0;font-weight:400;letter-spacing:-.2px;color:var(--fg)}
.meta{color:var(--mut);font-size:14px;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.meta>*+*::before{content:"·";margin-right:10px;color:var(--line-strong)}
.dot{width:8px;height:8px;border-radius:50%;background:#bcc0c8;display:inline-block;vertical-align:middle;transition:background .2s}
.dot.ok{background:var(--ok);box-shadow:0 0 0 4px var(--ok-soft)}
.dot.warn{background:var(--warn);box-shadow:0 0 0 4px var(--warn-soft)}
.dot.err{background:var(--err);box-shadow:0 0 0 4px var(--err-soft)}
nav.tabs{display:flex;gap:2px;padding:0 20px;border-bottom:1px solid var(--line);
  overflow-x:auto;-webkit-overflow-scrolling:touch;max-width:960px;margin:0 auto}
nav.tabs button{background:transparent;border:0;color:var(--mut);
  padding:14px 18px;font:inherit;font-size:15px;cursor:pointer;
  border-bottom:2px solid transparent;white-space:nowrap;margin-bottom:-1px}
nav.tabs button:hover{color:var(--fg)}
nav.tabs button.active{color:var(--fg);border-bottom-color:var(--accent)}
main{padding:24px 20px 40px;max-width:960px;margin:0 auto}
section[hidden]{display:none!important}
h2{font-family:var(--serif);font-size:20px;font-weight:400;margin:32px 0 12px;color:var(--fg);letter-spacing:-.1px}
h2:first-child{margin-top:8px}
p{margin:8px 0}
.lede{color:var(--mut);font-size:15px;margin-bottom:16px}

/* Cards (generic + status grid) */
.card{background:var(--card);border-radius:var(--r);padding:20px;box-shadow:var(--shadow)}
.status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;margin:14px 0}
.status-card{background:var(--card);border-radius:var(--r);padding:18px;box-shadow:var(--shadow);display:flex;flex-direction:column;gap:6px;min-height:120px}
.status-card .lbl{color:var(--mut);font-size:13px;font-weight:500;text-transform:uppercase;letter-spacing:.6px}
.status-card .v{font-size:22px;font-weight:500;line-height:1.2;color:var(--fg)}
.status-card .v.small{font-size:16px;font-weight:400}
.status-card .sub{color:var(--mut);font-size:13px}
.status-card .ctrl{margin-top:auto;display:flex;gap:6px;flex-wrap:wrap}
.status-card.ok    {background:var(--ok-soft)}
.status-card.warn  {background:var(--warn-soft)}
.status-card.err   {background:var(--err-soft)}

/* Quick-control tiles */
.tile-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px;margin:8px 0 18px}
/* When the pair has a link toggle between them, switch to an explicit
   3-column grid (tile | chip | tile) so the chip lives in the flow instead
   of floating over either tile's content. On narrow viewports the row
   wraps so each item gets its own line. */
.tile-grid.linkable{grid-template-columns:minmax(0,1fr) auto minmax(0,1fr)}
.link-toggle{background:#fff;border:1px solid var(--line-strong);border-radius:999px;
  padding:6px 14px 6px 10px;display:inline-flex;align-items:center;gap:8px;
  font:inherit;font-size:13px;color:var(--mut);cursor:pointer;align-self:center;
  white-space:nowrap;box-shadow:var(--shadow);
  transition:color .15s,border-color .15s,background .15s}
.link-toggle:hover{color:var(--fg);border-color:var(--accent)}
.link-toggle.linked{color:var(--accent);border-color:var(--accent-soft);background:var(--accent-soft)}
.link-toggle .link-icon{display:inline-block;width:12px;height:12px;border-radius:50%;
  border:2px solid currentColor;position:relative}
.link-toggle.linked .link-icon{background:currentColor}
.link-toggle:not(.linked) .link-icon::after{content:"";position:absolute;left:50%;top:50%;
  transform:translate(-50%,-50%) rotate(45deg);width:14px;height:2px;background:currentColor;
  border-radius:1px}
@media(max-width:720px){
  /* Stack vertically on narrow viewports: tiles full-width, link chip
     centered between them. */
  .tile-grid.linkable{grid-template-columns:1fr}
  .link-toggle{justify-self:center;margin:-2px 0}
}
.tile{background:var(--card);border-radius:var(--r);padding:20px;box-shadow:var(--shadow)}
.tile h3{margin:0 0 4px;font:inherit;font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:.6px;font-weight:500;
  display:flex;align-items:center;justify-content:space-between;gap:8px}
.tile .pct{font-family:var(--serif);font-size:34px;font-weight:400;line-height:1.1;margin:6px 0 4px;color:var(--fg)}
.tile .pct .unit{font-size:18px;color:var(--mut);margin-left:4px}
.tile .sub{color:var(--mut);font-size:13px;margin-bottom:14px;min-height:1.2em}
.tile input[type=range]{width:100%;margin:0}

/* Toggle switch */
.sw{position:relative;width:44px;height:26px;background:#dde1e7;border-radius:13px;cursor:pointer;flex-shrink:0;
  transition:background .18s;border:0;padding:0}
.sw.on{background:var(--accent)}
.sw::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;background:#fff;border-radius:50%;
  box-shadow:0 1px 3px rgba(0,0,0,.15);transition:left .18s}
.sw.on::after{left:21px}

/* Segmented control (e.g. Container source, status-LED override) */
.seg{display:inline-flex;background:var(--accent-soft);border-radius:999px;padding:3px;gap:0}
.seg button{background:transparent;border:0;color:var(--mut);font:inherit;font-size:13px;cursor:pointer;
  padding:6px 14px;border-radius:999px;font-weight:500;line-height:1.2;white-space:nowrap}
.seg button.active{background:#fff;color:var(--fg);box-shadow:0 1px 2px rgba(40,55,80,.08)}

/* Pretty range slider (light theme) */
input[type=range]{-webkit-appearance:none;appearance:none;height:6px;background:var(--line);border-radius:3px;outline:none}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:18px;height:18px;background:var(--card);
  border:2px solid var(--accent);border-radius:50%;cursor:pointer;box-shadow:0 1px 3px rgba(40,55,80,.18)}
input[type=range]::-moz-range-thumb{width:18px;height:18px;background:var(--card);border:2px solid var(--accent);border-radius:50%;cursor:pointer}

/* Number inputs */
input[type=number],input[type=password],input[type=text]{background:#fff;color:var(--fg);
  border:1px solid var(--line-strong);border-radius:8px;padding:7px 10px;font:inherit;font-size:14px}
input[type=number]:focus,input[type=password]:focus,input[type=text]:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}

/* Buttons */
button.btn{background:var(--accent);color:#fff;border:0;padding:10px 18px;
  border-radius:var(--r-sm);font:inherit;font-size:14px;font-weight:500;cursor:pointer;
  transition:opacity .15s}
button.btn:hover{opacity:.88}
button.btn.ghost{background:#fff;color:var(--fg);border:1px solid var(--line-strong)}
button.btn.ghost:hover{background:var(--accent-soft);border-color:var(--accent)}
button.btn.danger{background:var(--err-soft);color:var(--err);border:1px solid var(--err)}
button.btn:disabled{opacity:.5;cursor:not-allowed}
button.btn.mini{padding:6px 12px;font-size:13px;border-radius:8px}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}

/* Toast */
.toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);
  background:#fff;color:var(--fg);padding:12px 18px;border-radius:var(--r-sm);
  box-shadow:0 10px 30px rgba(40,55,80,.14);opacity:0;pointer-events:none;
  transition:opacity .2s;z-index:9;font-size:14px}
.toast.show{opacity:1}
.toast.ok{box-shadow:0 0 0 1px var(--ok),0 10px 30px rgba(40,55,80,.14)}
.toast.err{box-shadow:0 0 0 1px var(--err),0 10px 30px rgba(40,55,80,.14)}

/* Log + diagnostics */
pre.log{background:#fafbfc;border:1px solid var(--line);border-radius:var(--r-sm);
  padding:14px;max-height:340px;overflow:auto;font:13px/1.55 ui-monospace,Menlo,Consolas,monospace;
  white-space:pre-wrap;color:#525a6a}
.kv{display:grid;grid-template-columns:max-content 1fr;gap:8px 18px;font-size:14px;color:var(--mut)}
.kv b{color:var(--fg);font-weight:500}
.row{display:flex;flex-wrap:wrap;gap:8px 18px;color:var(--mut);font-size:14px;margin:8px 0}
.row span b{color:var(--fg);font-weight:500}
.spark{width:100%;height:64px;background:#fafbfc;border:1px solid var(--line);border-radius:var(--r-sm);margin-top:10px}

/* Config form */
.cfg-group{background:var(--card);border-radius:var(--r);padding:18px 20px;margin:14px 0;box-shadow:var(--shadow)}
.cfg-group>h3{margin:0 0 10px;color:var(--fg);font-family:var(--serif);font-size:18px;font-weight:400;letter-spacing:-.1px}
.field{display:grid;grid-template-columns:1fr 1fr 90px;gap:8px 14px;align-items:center;padding:12px 0;border-top:1px solid var(--line)}
.field:first-of-type{border-top:0;padding-top:4px}
.field label{color:var(--fg);font-size:14px}
.field .hint{color:var(--mut);font-size:12px;margin-top:2px}
.field input[type=range]{width:100%}
.field input[type=number]{width:90px}

/* Setup banner (WiFi captive portal) */
.setup-banner{background:var(--warn-soft);color:#7a5a20;border-radius:var(--r-sm);padding:14px 18px;margin-bottom:14px;font-size:14px}

/* Advanced collapse */
details.adv{margin:14px 0;background:var(--card);border-radius:var(--r);box-shadow:var(--shadow)}
details.adv>summary{padding:14px 18px;cursor:pointer;color:var(--fg);font-size:14px;font-weight:500;list-style:none}
details.adv>summary::after{content:" ▾";color:var(--mut);float:right}
details.adv[open]>summary::after{content:" ▴"}
details.adv>div{padding:0 18px 18px}

@media(max-width:560px){
  header{padding:16px}
  main{padding:16px 14px 32px}
  h1{font-size:22px}
  .field{grid-template-columns:1fr 1fr}
  .field .hint{grid-column:1/-1}
  .meta{font-size:13px}
}
</style>
</head>
<body>
<header>
  <h1>Mist Maker</h1>
  <span class="meta">
    <span><span id="connDot" class="dot"></span> <span id="connTxt">connecting…</span></span>
    <span id="hostShow">—</span>
    <span id="ipShow">—</span>
    <span id="verShow">—</span>
  </span>
</header>
<nav class="tabs">
  <button data-tab="status" class="active">Status</button>
  <button data-tab="config">Config</button>
  <button data-tab="debug">Debug</button>
  <button data-tab="about">About</button>
</nav>
<main>

<section id="status">
  <div id="setupBanner" class="setup-banner" hidden>
    Device is in WiFi setup mode. Join the
    <code>MistMaker-Setup-XXXX</code> network (password <code>mistmaker-setup</code>).
  </div>

  <!-- Primary controls — Mist + LED wave tiles separated by a chain-link
       toggle. Click the link to decouple the two sliders so you can dim the
       LEDs without throttling the mist (or vice-versa). -->
  <div class="tile-grid linkable" id="tilePair">
    <div class="tile">
      <h3>Mist <button class="sw" id="swMist" aria-label="Toggle mist"></button></h3>
      <div class="pct"><span id="mistPct">0</span><span class="unit">%</span></div>
      <div class="sub" id="mistSub">—</div>
      <input type="range" min="0" max="100" step="1" id="mistSlider" value="0">
    </div>
    <button class="link-toggle linked" id="bLink" type="button"
      aria-label="Toggle mist and LED link" title="Mist and LED levels move together — click to unlink">
      <span class="link-icon" aria-hidden="true"></span>
      <span class="link-label">Linked</span>
    </button>
    <div class="tile">
      <h3>LED wave <button class="sw" id="swWave" aria-label="Toggle LED wave"></button></h3>
      <div class="pct"><span id="wavePct">0</span><span class="unit">%</span></div>
      <div class="sub" id="waveSub">—</div>
      <input type="range" min="0" max="100" step="1" id="waveSlider" value="0">
    </div>
  </div>

  <!-- How does the device know a container is docked? -->
  <div class="card" style="display:flex;align-items:center;justify-content:space-between;gap:14px;flex-wrap:wrap;margin:14px 0">
    <div>
      <div style="font-size:14px;font-weight:500">Container detection</div>
      <div style="color:var(--mut);font-size:13px;margin-top:2px" id="srcHint">How the diffuser knows the dispenser is placed.</div>
    </div>
    <div class="seg" id="srcSeg">
      <button data-src="reed">Magnet (reed)</button>
      <button data-src="sense">Current sense</button>
    </div>
  </div>

  <!-- Three at-a-glance status cards -->
  <div class="status-grid">
    <div class="status-card" id="cardContainer">
      <div class="lbl">Container</div>
      <div class="v" id="vContainer">—</div>
      <div class="sub" id="vContainerSub">—</div>
    </div>
    <div class="status-card" id="cardWater">
      <div class="lbl">Water &amp; disc</div>
      <div class="v" id="vWater">—</div>
      <div class="sub" id="vWaterMa">—</div>
      <div class="ctrl">
        <button class="btn mini ghost" id="bCalWater" title="Capture the current with water present, then suggest a low-water threshold. Run while mist is flowing normally.">Calibrate</button>
      </div>
    </div>
    <div class="status-card" id="cardStatLed">
      <div class="lbl">Status light</div>
      <div class="v small" id="vStat">—</div>
      <div class="ctrl">
        <div class="seg" id="statSeg">
          <button data-mode="auto">Auto</button>
          <button data-mode="on">On</button>
          <button data-mode="off">Off</button>
        </div>
      </div>
    </div>
  </div>

  <!-- Quiet override row -->
  <div class="actions" style="margin:6px 0 18px">
    <button class="btn ghost mini" id="bForceRun">Start mist manually</button>
    <button class="btn ghost mini" id="bForceIdle">Stop mist manually</button>
  </div>

  <!-- Mist pulse depth -->
  <details class="adv" id="quickPulse"><summary>Mist pulse depth — how visible is the breathing wave</summary>
    <div>
      <p class="lede" style="margin:6px 0 12px">
        0% means the mist runs steady. 100% swings the full range with the LED wave.
        Default 64% matches the LED's own visible range.
      </p>
      <div class="field" style="grid-template-columns:1fr 90px;border-top:0">
        <input type="range" min="0" max="100" step="1" id="pulseSlider" value="64">
        <input type="number" min="0" max="100" step="1" id="pulseNum" value="64">
      </div>
      <div class="actions"><button class="btn" id="bPulseSave">Save pulse depth</button></div>
    </div>
  </details>

  <!-- Live readings -->
  <h2>Live readings</h2>
  <div class="status-grid">
    <div class="status-card"><div class="lbl">State</div><div class="v" id="vState">—</div><div class="sub" id="vUp">—</div></div>
    <div class="status-card"><div class="lbl">Current draw</div><div class="v"><span id="vCurMa">—</span> mA</div><div class="sub">Sampled at 1 kHz</div></div>
    <div class="status-card"><div class="lbl">Signal</div><div class="v"><span id="vRssi">—</span> dBm</div><div class="sub"><span id="vHeap">—</span> bytes free</div></div>
  </div>
  <svg class="spark" id="spark" viewBox="0 0 300 60" preserveAspectRatio="none">
    <polyline id="sparkLine" fill="none" stroke="#7a9bb9" stroke-width="1.5" points=""/>
  </svg>
</section>

<section id="config" hidden>
  <p class="lede">
    Most people only need the Status tab. These knobs fine-tune timing, brightness, and
    sensing thresholds. Adjust the sliders, then <b>Save &amp; apply</b> — saving asks for the
    admin password you set during WiFi setup.
  </p>
  <div id="cfgForm"></div>
  <div class="actions">
    <button class="btn" id="btnSave">Save &amp; apply</button>
    <button class="btn ghost" id="btnRevert">Revert</button>
    <button class="btn ghost" id="btnDefaults">Reset everything to defaults</button>
  </div>
</section>

<section id="debug" hidden>
  <h2>Raw signals</h2>
  <div class="row">
    <span>ADC mean <b id="dCur">—</b> mA</span>
    <span>ADC variance <b id="dVar">—</b></span>
    <span>Reed raw <b id="dReed">—</b></span>
    <span>Button raw <b id="dBtn">—</b></span>
    <span>State <b id="dState">—</b></span>
  </div>

  <h2>Commands</h2>
  <div class="actions">
    <button class="btn ghost" id="btnWalk">LED chase animation</button>
    <button class="btn ghost" id="btnLeds">Toggle LED visibility</button>
    <button class="btn ghost" id="btnPlotMute">Toggle plot stream</button>
    <button class="btn ghost" id="btnRefreshLog">Refresh log</button>
    <button class="btn ghost" id="btnReboot">Reboot device</button>
    <button class="btn danger" id="btnForget">Forget WiFi network</button>
  </div>

  <h2>Recent activity log</h2>
  <p class="lede">
    Important events from the device — state changes, water/disc detection, calibration runs.
    The bench-only plot stream (high-frequency current readings for Arduino Serial Plotter) is
    muted by default; use <b>Toggle plot stream</b> above to see it on USB serial. The log here
    is in RAM (~80 lines) and resets when you reboot.
  </p>
  <pre class="log" id="log">(loading)</pre>
</section>

<section id="about" hidden>
  <h2>Device</h2>
  <div class="card"><div class="kv" id="aboutKv"><b>Hostname</b><span>—</span></div></div>

  <h2>Updates over WiFi</h2>
  <p class="lede">
    In the Arduino IDE: <code>Tools → Port → mistmaker at &lt;ip&gt; (esp32)</code>. Use the
    admin password below. The diffuser stops the mist and powers down the boost rail before
    flash is erased, so flashing is always safe.
  </p>

  <h2>If you get locked out</h2>
  <p class="lede">
    If the device drops off the network (wrong WiFi, lost password, anything strange),
    <b>press the physical reset button three times in a row</b> within about five seconds
    each. The firmware wipes everything in non-volatile storage and reboots back into setup
    mode — same effect as <i>Reset everything to defaults</i> in Config, just without needing
    the password.
  </p>

  <h2>Admin password</h2>
  <p class="lede">
    Set or change the password used for the config writes, commands, and over-the-air flashing.
  </p>
  <div class="actions">
    <input type="password" id="pwdNew" placeholder="New password (min 4 characters)" style="flex:1;min-width:200px">
    <button class="btn" id="btnSetPwd">Set password</button>
  </div>
</section>

</main>
<div class="toast" id="toast"></div>

<script>
// --- field metadata used to render the Config form --------------------------
const FIELDS=[
 {grp:"LED — BREATH (idle)",f:[
  ["ledBreathPeak","Brightness peak (dim max)",0,255,1],
  ["ledBreathLow","Brightness floor (dim low)",0,255,1],
  ["ledBreathPeriodMs","Period (ms)",500,30000,100],
 ]},
 {grp:"LED — WAVE (docked)",f:[
  ["waveBaseLevel","Base level (dim low)",0,255,1],
  ["waveSwellPeak","Swell peak (base + peak ≤ 255)",0,255,1],
  ["wavePeriodMs","Period (ms)",500,60000,100],
  ["ledCrossfadeMs","Mode crossfade (ms)",0,10000,50],
  ["ledScaleStepPerTick","Hide/show fade step",1,255,1],
 ]},
 {grp:"Mist",f:[
  ["mistDutyMax","Duty max (high)",0,255,1],
  ["mistDutyMin","Duty min (low floor)",0,255,1],
  ["levelDefault","Boot level (0..255)",0,255,1],
  ["mistWaveTroughQ8","Wave-sync trough (Q8: 0..256)",0,256,1],
 ]},
 {grp:"Level smoother",f:[
  ["levelSmoothTickMs","Tick (ms)",1,1000,1],
  ["levelSmoothStepUp","Step up",1,255,1],
  ["levelSmoothStepDn","Step down",1,255,1],
  ["levelSmoothStepUpFast","Step up (fast restore)",1,255,1],
  ["levelRampTickMs","Long-press ramp tick (ms)",1,1000,1],
  ["levelRampStep","Long-press ramp step",1,255,1],
 ]},
 {grp:"Button & Reed",f:[
  ["buttonDebounceMs","Button debounce (ms)",1,1000,1],
  ["buttonLongPressMs","Long-press threshold (ms)",50,5000,10],
  ["buttonLongTickMs","Long-press tick (ms)",1,1000,1],
  ["reedInsertDwellMs","Reed insert dwell (ms)",0,5000,50],
  ["reedRemoveDwellMs","Reed remove dwell (ms)",0,5000,10],
 ]},
 {grp:"Status LED (D7)",f:[
  ["statusLedDimDuty","Dim duty (0..255)",0,255,1],
 ]},
 {grp:"Water & disc sensing",f:[
  ["senseProbeDuty","Disc-presence probe PWM (low)",0,255,1],
  ["senseDiscPresentMa10x","Disc-present threshold (mA × 10)",0,5000,1],
  ["senseWaterProbeDuty","Water-level probe PWM",0,255,1],
  ["senseWaterLowMa10x","Low-water threshold (mA × 10)",0,5000,1],
  ["senseDiscDisconnMidMa10x","Disc came loose threshold (mA × 10)",0,5000,1],
  ["senseWaterHystMa10x","Water recovery hysteresis (mA × 10)",0,500,1],
  ["senseWaterCheckIntervalS","Water check interval (seconds)",5,3600,1],
  ["senseWaterShutdownS","Low-water countdown to shutoff (seconds)",30,7200,10],
  ["senseAutoProbeIntervalS","Auto-probe interval when using current sense (s)",1,3600,1],
 ]},
];

// --- helpers ----------------------------------------------------------------
const $=id=>document.getElementById(id);
function toast(msg,kind){
  const t=$("toast");t.textContent=msg;t.className="toast show "+(kind||"");
  setTimeout(()=>t.classList.remove("show"),2200);
}
function fmtUptime(ms){
  const s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor(s/60)%60,
        ss=s%60;
  return (h?h+"h ":"")+(m||h?m+"m ":"")+ss+"s";
}
function dotState(state){
  $("connDot").className="dot "+state;
  $("connTxt").textContent={ok:"live",warn:"reconnecting",err:"offline"}[state]||"-";
}

// --- tab routing ------------------------------------------------------------
const SECTIONS=["status","config","debug","about"];
function showTab(name){
  for(const s of SECTIONS) $(s).hidden = (s!==name);
  for(const b of document.querySelectorAll("nav.tabs button"))
    b.classList.toggle("active", b.dataset.tab===name);
  if(name==="config") loadConfig();
  if(name==="debug")  refreshLog();
  if(name==="about")  loadInfo();
  location.hash = name;
}
document.querySelectorAll("nav.tabs button").forEach(b=>{
  b.addEventListener("click",()=>showTab(b.dataset.tab));
});
showTab(location.hash.replace("#","")||"status");

// --- SSE: live status -------------------------------------------------------
const sparkBuf=[];
let es;
function startSse(){
  if(es) es.close();
  try{ es=new EventSource("/api/events"); }catch(e){ dotState("err"); return; }
  es.onopen   =()=>dotState("ok");
  es.onerror  =()=>dotState("err");
  es.onmessage=ev=>{
    let d; try{ d=JSON.parse(ev.data); }catch(e){ return; }
    applyStatus(d);
  };
}
// Track if the user is actively dragging a slider so SSE doesn't fight them.
let dragging = null;     // "mist" | "wave" | null
let lastNonZeroLevel = 255;
function pctFromLevel(l){ return Math.round(l*100/255); }
function levelFromPct(p){ return Math.max(0,Math.min(255,Math.round(p*255/100))); }

function applyStatus(d){
  // Live readings
  $("vState").textContent= d.state==="RUNNING" ? "Running" :
                            d.state==="IDLE" ? "Idle, waiting" :
                            d.state==="XFADE_OUT" ? "Fading out" : d.state;
  $("vCurMa").textContent=d.meanMa.toFixed(1);
  $("vUp").textContent="up "+fmtUptime(d.uptimeMs);
  $("vHeap").textContent=d.freeHeap.toLocaleString();
  $("vRssi").textContent=d.rssi;
  $("setupBanner").hidden=!d.setupMode;

  // Mist tile
  const mistOn = d.userLevel>0;
  $("swMist").classList.toggle("on",mistOn);
  const mistPct = pctFromLevel(d.userLevel);
  $("mistPct").textContent = mistPct;
  if(dragging!=="mist") $("mistSlider").value = mistPct;
  $("mistSub").textContent = d.mist ? "Flowing" :
      (d.reedPresent ? "Ready, settling…" : "Waiting for a container");
  if(d.userLevel>0) lastNonZeroLevel = d.userLevel;

  // LED Wave tile — uses userLedLevel which equals userLevel when linked,
  // and rides its own slider when unlinked. Fall back to userLevel for
  // older firmwares that don't yet emit the field.
  const ledLevel = (d.userLedLevel != null) ? d.userLedLevel : d.userLevel;
  $("swWave").classList.toggle("on",!d.ledsHidden);
  const wavePct = pctFromLevel(ledLevel);
  $("wavePct").textContent = wavePct;
  if(dragging!=="wave") $("waveSlider").value = wavePct;
  $("waveSub").textContent = d.ledsHidden ? "Hidden — mist still flows" :
      (d.state==="RUNNING" ? "Swelling with the mist" : "Soft breath");

  // Link toggle reflects the live cfg flag (mistLedLinked may be 1 or 0).
  if(d.mistLedLinked != null){
    const linked = !!d.mistLedLinked;
    const lb = $("bLink");
    lb.classList.toggle("linked", linked);
    lb.querySelector(".link-label").textContent = linked ? "Linked" : "Unlinked";
    lb.title = linked
      ? "Mist and LED levels move together — click to unlink"
      : "Mist and LED are independent — click to link";
  }

  // Container detection source segmented control (lastCfg may have arrived already)
  const useSense = !!(lastCfg && lastCfg.senseUseAsReed);
  for(const b of document.querySelectorAll("#srcSeg button")){
    b.classList.toggle("active", (b.dataset.src==="sense")===useSense);
  }
  $("srcHint").textContent = useSense
    ? "Using the piezo's current draw to detect the container — reed switch ignored."
    : "Using the reed (magnet) switch on the base — the default.";

  // Container status card
  const docked = useSense
    ? (d.piezoState==="WATER_OK" || d.piezoState==="WATER_LOW" || d.piezoState==="WATER_DEPLETED")
    : !!d.reedPresent;
  $("vContainer").textContent = docked ? "Docked" : "Empty";
  $("vContainerSub").textContent = useSense
    ? (docked ? "Detected by current sense" : "Place a container to start")
    : (d.reedRaw ? "Magnet detected" : "No magnet — place a container to start");
  $("cardContainer").className = "status-card " + (docked ? "ok" : "");

  // Water & disc status card
  const wmap = {
    "UNKNOWN":           ["—","Waiting for first probe",""],
    "WATER_OK":          ["Good","Plenty of water","ok"],
    "WATER_LOW":         ["Low water","Refill soon","warn"],
    "WATER_DEPLETED":    ["Empty","Stopped — lift dispenser to reset","err"],
    "DISC_MISSING":      ["No disc","Dispenser not detected","warn"],
    "DISC_DISCONNECTED": ["Disc came loose","Stopped — lift dispenser to reset","err"],
    "DISC_DRY":          ["Dry disc","Add water","warn"]
  };
  const w = wmap[d.piezoState] || ["—","",""];
  let vText = w[0], subText = w[1];
  if(d.piezoState==="WATER_LOW" && d.waterCountdownS>0){
    const mm = Math.floor(d.waterCountdownS/60), ss = d.waterCountdownS%60;
    subText = "Refill within "+mm+":"+(ss<10?"0":"")+ss;
  }
  $("vWater").textContent = vText;
  $("vWaterMa").textContent = "Last probe: " + (d.piezoProbeMa||0).toFixed(1) + " mA · " + subText;
  $("cardWater").className = "status-card " + (w[2] || "");

  // Status-light card (segmented)
  const sOv = d.statLedOverride;
  $("vStat").textContent = sOv===1 ? "Forced on" : sOv===0 ? "Forced off" : "Following dock";
  const sMode = sOv===1 ? "on" : sOv===0 ? "off" : "auto";
  for(const b of document.querySelectorAll("#statSeg button")){
    b.classList.toggle("active", b.dataset.mode===sMode);
  }

  // Connection state (header dot)
  // (kept simple — SSE onopen/onerror toggle it)

  // Debug tab raw row
  if($("dCur")) {
    $("dCur").textContent=d.meanMa.toFixed(1);
    $("dVar").textContent=d.varMa2.toFixed(1);
    $("dReed").textContent=d.reedRaw;
    $("dBtn").textContent=d.btnRaw;
    $("dState").textContent=d.state+" ("+d.stateInt+")";
  }

  // Sparkline (last 120 samples ~30 s @ 4 Hz)
  sparkBuf.push(d.meanMa);
  if(sparkBuf.length>120) sparkBuf.shift();
  const max=Math.max(50,...sparkBuf);
  const pts=sparkBuf.map((v,i)=>{
    const x=(i*300/Math.max(1,sparkBuf.length-1)).toFixed(1);
    const y=(60-Math.min(60,(v/max)*55)).toFixed(1);
    return x+","+y;
  }).join(" ");
  $("sparkLine").setAttribute("points",pts);
}

// --- Status tile + override wiring -----------------------------------------
async function postJson(path, body){
  if(!ensureAdmin()) throw new Error("no auth");
  const r = await fetch(path,{method:"POST",
    headers:{"Content-Type":"application/json","Authorization":"Basic "+adminBasic()},
    body: body ? JSON.stringify(body) : undefined});
  if(r.status===401){ clearAdmin(); throw new Error("auth"); }
  if(!r.ok) throw new Error("HTTP "+r.status);
  return r.json().catch(()=>({}));
}

function bindLevelSlider(id, kind, endpoint){
  const el=$(id);
  // Mark dragging on pointerdown (covers mouse + touch); clear on pointerup
  // BEFORE the async POST runs, so a slow fetch doesn't widen the suppression
  // window. Keyboard arrow-key changes still fire "change" without ever
  // setting dragging — they just send the value, which is fine.
  el.addEventListener("pointerdown",()=>{ dragging=kind; });
  el.addEventListener("pointerup",  ()=>{ dragging=null; });
  el.addEventListener("change",async()=>{
    try{ await postJson(endpoint,{value:levelFromPct(+el.value)}); }
    catch(e){ toast("Level: "+e.message,"err"); }
  });
}
// Mist slider → mist level. Wave slider → LED level (firmware mirrors back
// to mist in linked mode, so dragging either slider keeps them aligned).
bindLevelSlider("mistSlider","mist","/api/cmd/level");
bindLevelSlider("waveSlider","wave","/api/cmd/led-level");

// Link/unlink toggle — flips cfg.mistLedLinked. Sends 0/1 to match the
// rest of the API's integer-boolean convention (the server's jsonGetLong
// helper doesn't parse true/false). Firmware snaps the LED level to mist
// on re-link so the two start aligned.
$("bLink").addEventListener("click",async()=>{
  if(!ensureAdmin()) return;
  const currentlyLinked = $("bLink").classList.contains("linked");
  try{
    await postJson("/api/cmd/link",{linked: currentlyLinked ? 0 : 1});
    toast(currentlyLinked ? "Mist and LED unlinked" : "Mist and LED linked","ok");
  }catch(e){ toast("Could not toggle link: "+e.message,"err"); }
});

$("swMist").addEventListener("click",async()=>{
  const wantOn = !$("swMist").classList.contains("on");
  try{
    await postJson("/api/cmd/level",{value: wantOn ? lastNonZeroLevel : 0});
  } catch(e){ toast("Mist: "+e.message,"err"); }
});
$("swWave").addEventListener("click",async()=>{
  try{ await postJson("/api/cmd/leds"); }
  catch(e){ toast("LEDs: "+e.message,"err"); }
});
$("bForceRun").addEventListener("click",()=>{
  // Force-RUNNING re-arms mist regardless of dock state. Confirm so a stray
  // tap doesn't fire the piezo on an empty bottle.
  if(!confirm("Start the mist manually? This runs even if no container is docked — use only for testing.")) return;
  postJson("/api/cmd/state",{state:"running"})
    .then(()=>toast("Mist started","ok")).catch(e=>toast(e.message,"err"));
});
$("bForceIdle").addEventListener("click",()=>postJson("/api/cmd/state",{state:"idle"})
  .then(()=>toast("Mist stopped","ok")).catch(e=>toast(e.message,"err")));

// Status-light segmented control
document.querySelectorAll("#statSeg button").forEach(b=>{
  b.addEventListener("click",()=>postJson("/api/cmd/statled",{mode:b.dataset.mode})
    .catch(e=>toast(e.message,"err")));
});

// Container-detection source segmented switch (writes cfg.senseUseAsReed)
document.querySelectorAll("#srcSeg button").forEach(b=>{
  b.addEventListener("click",async()=>{
    const wantSense = b.dataset.src==="sense";
    const cur = !!(lastCfg && lastCfg.senseUseAsReed);
    if(wantSense===cur) return;
    if(wantSense && !confirm("Switch to current-sense detection? The reed switch will be ignored. You can switch back any time.")) return;
    if(!ensureAdmin()) return;
    try{
      await postJson("/api/config",{field:"senseUseAsReed",value: wantSense ? 1 : 0});
      lastCfg.senseUseAsReed = wantSense ? 1 : 0;
      toast(wantSense ? "Now using current sense" : "Now using magnet (reed)","ok");
    }catch(e){ toast("Could not switch: "+e.message,"err"); }
  });
});

// Calibrate water — capture mA at water-probe duty, offer to apply ×0.85 as
// the new low-water threshold. Best run while mist is actively flowing with
// known-good water level.
$("bCalWater").addEventListener("click",async()=>{
  if(!ensureAdmin()) return;
  try{
    const r = await postJson("/api/cmd/calibrate-water");
    const recorded = r.recordedMa.toFixed(1);
    const recommended = r.recommendedLowMa.toFixed(1);
    if(confirm("Recorded "+recorded+" mA. Apply recommended low-water threshold of "+recommended+" mA (~85% of recorded)?")){
      const v10 = Math.round(r.recommendedLowMa * 10);
      await postJson("/api/config",{field:"senseWaterLowMa10x",value:v10});
      toast("Threshold set to "+recommended+" mA","ok");
      loadConfig();
    } else {
      toast("Recorded "+recorded+" mA (not applied)","ok");
    }
  }catch(e){ toast("Calibrate failed: "+e.message,"err"); }
});

// --- Mist pulse depth (UI-friendly wrapper around cfg.mistWaveTroughQ8) ----
// pulseDepth% 0..100 maps to troughQ8 = round((1-pulse/100)*256). 0% pulse
// means trough=256 (constant), 100% pulse means trough=0 (full swing).
function pulseFromTrough(troughQ8){ return Math.round(100*(256-troughQ8)/256); }
function troughFromPulse(pct){ return Math.max(0,Math.min(256,Math.round((1-pct/100)*256))); }
function syncPulseFromCfg(){
  if(lastCfg && lastCfg.mistWaveTroughQ8!=null){
    const p=pulseFromTrough(lastCfg.mistWaveTroughQ8);
    $("pulseSlider").value=p; $("pulseNum").value=p;
  }
}
$("pulseSlider").addEventListener("input",e=>{ $("pulseNum").value=e.target.value; });
$("pulseNum").addEventListener("input",e=>{
  let v=parseInt(e.target.value,10); if(isNaN(v)) return;
  v=Math.max(0,Math.min(100,v));
  $("pulseSlider").value=v;
});
$("bPulseSave").addEventListener("click",async()=>{
  if(!ensureAdmin()) return;
  const pct=parseInt($("pulseNum").value,10)||0;
  const q8=troughFromPulse(pct);
  try{
    await postJson("/api/config",{field:"mistWaveTroughQ8",value:q8});
    lastCfg.mistWaveTroughQ8=q8;
    toast("Pulse depth saved","ok");
  }catch(e){ toast("Save failed: "+e.message,"err"); }
});

// --- Config tab -------------------------------------------------------------
function renderConfigForm(values){
  const root=$("cfgForm");
  root.innerHTML="";
  for(const g of FIELDS){
    const fs=document.createElement("div"); fs.className="cfg-group";
    const lg=document.createElement("h3"); lg.textContent=g.grp; fs.appendChild(lg);
    for(const [name,label,lo,hi,step] of g.f){
      const div=document.createElement("div"); div.className="field";
      const cur = values[name] ?? lo;
      div.innerHTML=
        `<label>${label}<div class="hint">${name} (${lo}–${hi})</div></label>`+
        `<input type="range" min="${lo}" max="${hi}" step="${step}" data-name="${name}" value="${cur}">`+
        `<input type="number" min="${lo}" max="${hi}" step="${step}" data-name="${name}" value="${cur}">`;
      const [r,n]=div.querySelectorAll("input");
      r.addEventListener("input",()=>n.value=r.value);
      n.addEventListener("input",()=>{
        let v=parseInt(n.value,10); if(isNaN(v)) return;
        v=Math.max(lo,Math.min(hi,v));
        r.value=v;
      });
      fs.appendChild(div);
    }
    root.appendChild(fs);
  }
}
let lastCfg={};
async function loadConfig(){
  try{
    const r=await fetch("/api/config",{cache:"no-store"});
    lastCfg=await r.json();
    renderConfigForm(lastCfg);
    syncPulseFromCfg();
  }catch(e){ toast("Failed to load config: "+e,"err"); }
}
async function saveOne(name,value){
  const r=await fetch("/api/config",{method:"POST",
    headers:{"Content-Type":"application/json",
             "Authorization":"Basic "+adminBasic()},
    body:JSON.stringify({field:name,value:value})});
  if(r.status===401){ clearAdmin(); throw new Error("auth"); }
  if(!r.ok) throw new Error("HTTP "+r.status+" "+(await r.text()));
  return r.json();
}
$("btnSave").addEventListener("click",async()=>{
  if(!ensureAdmin()) return;
  const inputs=document.querySelectorAll("#cfgForm input[type=number]");
  let changed=0;
  for(const n of inputs){
    const k=n.dataset.name, v=parseInt(n.value,10);
    if(isNaN(v)) continue;
    if(lastCfg[k]!==v){
      try{ lastCfg=await saveOne(k,v); ++changed; }
      catch(e){ toast("Save failed at "+k+": "+e.message,"err"); return; }
    }
  }
  toast(changed?("Saved "+changed+" change(s)"):"No changes","ok");
});
$("btnRevert").addEventListener("click",loadConfig);
$("btnDefaults").addEventListener("click",async()=>{
  if(!confirm("Factory reset wipes EVERYTHING — all config, WiFi credentials, AND your admin password. The device reboots into setup mode and you'll redo the captive portal. Continue?")) return;
  if(!ensureAdmin()) return;
  try{
    await postJson("/api/cmd/factory-reset");
    toast("Factory reset triggered — device rebooting","ok");
  }catch(e){ toast("Failed: "+e.message,"err"); }
});

// --- Debug tab --------------------------------------------------------------
async function refreshLog(){
  try{
    const r=await fetch("/api/log",{cache:"no-store"});
    $("log").textContent=await r.text();
  }catch(e){ $("log").textContent="(failed: "+e+")"; }
}
$("btnRefreshLog").addEventListener("click",refreshLog);

async function postCmd(path,confirmMsg){
  if(confirmMsg && !confirm(confirmMsg)) return;
  if(!ensureAdmin()) return;
  try{
    const r=await fetch(path,{method:"POST",
      headers:{"Authorization":"Basic "+adminBasic()}});
    if(r.status===401){ clearAdmin(); toast("Wrong password","err"); return; }
    if(!r.ok){ toast("HTTP "+r.status,"err"); return; }
    toast("OK","ok");
  }catch(e){ toast("Failed: "+e,"err"); }
}
$("btnWalk").addEventListener("click",()=>postCmd("/api/cmd/walk"));
$("btnLeds").addEventListener("click",()=>postCmd("/api/cmd/leds"));
$("btnPlotMute").addEventListener("click",()=>postCmd("/api/cmd/plotmute"));
$("btnReboot").addEventListener("click",()=>postCmd("/api/cmd/reboot","Reboot the device?"));
$("btnForget").addEventListener("click",()=>postCmd("/api/cmd/forget","Forget the saved WiFi network and reboot into setup mode?"));

// --- About tab --------------------------------------------------------------
async function loadInfo(){
  try{
    const r=await fetch("/api/info",{cache:"no-store"});
    const d=await r.json();
    $("hostShow").textContent=d.hostname;
    $("ipShow").textContent=d.ip;
    $("verShow").textContent=d.firmware;
    const kv=$("aboutKv");
    kv.innerHTML=
      `<b>Hostname</b><span><a href="http://${d.hostname}.local/">${d.hostname}.local</a></span>`+
      `<b>IP address</b><span>${d.ip}</span>`+
      `<b>MAC</b><span>${d.mac}</span>`+
      `<b>Wi-Fi network</b><span>${d.ssid} · ${d.rssi} dBm</span>`+
      `<b>Firmware</b><span>${d.firmware}</span>`+
      `<b>Build date</b><span>${d.buildDate}</span>`+
      `<b>Free memory</b><span>${d.freeHeap.toLocaleString()} bytes</span>`+
      `<b>OTA flashing</b><span>Port ${d.otaPort} · password ${d.hasAdminPwd?"set":"<b style=\"color:var(--err)\">not set — anyone on the network can flash this device</b>"}</span>`;
  }catch(e){ /* ignore */ }
}
$("btnSetPwd").addEventListener("click",async()=>{
  const p=$("pwdNew").value;
  if(!p||p.length<4){ toast("Min 4 chars","err"); return; }
  if(!ensureAdmin()) return;
  try{
    const r=await fetch("/api/cmd/password",{method:"POST",
      headers:{"Content-Type":"application/json",
               "Authorization":"Basic "+adminBasic()},
      body:JSON.stringify({"new":p})});
    if(r.status===401){ clearAdmin(); toast("Wrong current password","err"); return; }
    if(!r.ok){ toast("Failed","err"); return; }
    // Update the cached credential to the new password so subsequent writes work.
    sessionStorage.setItem("adminPwd",p);
    $("pwdNew").value="";
    toast("Password updated","ok");
  }catch(e){ toast("Failed: "+e,"err"); }
});

// --- Admin password caching (session-scoped) --------------------------------
function adminBasic(){
  const p=sessionStorage.getItem("adminPwd")||"";
  return btoa("admin:"+p);
}
function ensureAdmin(){
  let p=sessionStorage.getItem("adminPwd");
  if(p) return true;
  p=prompt("Admin password (set during WiFi setup):");
  if(!p) return false;
  sessionStorage.setItem("adminPwd",p);
  return true;
}
function clearAdmin(){ sessionStorage.removeItem("adminPwd"); }

// --- Boot -------------------------------------------------------------------
loadInfo();
// Pull cfg once so the Status-tab pulse slider has a starting value, without
// rendering the full config form (that happens lazily when Config tab opens).
fetch("/api/config",{cache:"no-store"}).then(r=>r.json()).then(c=>{
  lastCfg=c; syncPulseFromCfg();
  // Reflect the saved sensing source in the segmented switch right away,
  // before the first SSE frame paints the rest of the status card.
  const useSense = !!c.senseUseAsReed;
  for(const b of document.querySelectorAll("#srcSeg button")){
    b.classList.toggle("active", (b.dataset.src==="sense")===useSense);
  }
}).catch(()=>{});
startSse();
</script>
</body>
</html>
)==";
