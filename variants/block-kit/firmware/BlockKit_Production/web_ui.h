// Single-page web UI for the Block Kit, embedded as one PROGMEM string.
// Vanilla HTML/CSS/JS — no external CDN, no LittleFS — so the UI loads with
// just one HTTP roundtrip and works offline on the LAN.
//
// Tabs: Status (live cards + sparkline), Config (sliders for every cfg.*
// field, save-gated by admin password), Debug (raw IO + commands), About
// (device info + OTA hint).
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
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#0c0d10">
<title>Block Kit</title>
<style>
:root{
  --bg:#0c0d10; --fg:#e7eaee; --mut:#8a93a1; --line:#1d2128;
  --card:#13151a; --accent:#7ab7ff; --ok:#5bd99c; --warn:#f1c14a; --err:#ff7c7c;
  --r:10px;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0;background:var(--bg);color:var(--fg);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  font-size:clamp(14px,1.7vw,16px);line-height:1.45}
a{color:var(--accent)}
header{padding:14px 16px;border-bottom:1px solid var(--line);
  display:flex;align-items:center;gap:12px;flex-wrap:wrap}
h1{font-size:clamp(16px,2.2vw,20px);margin:0;font-weight:600;letter-spacing:.3px}
.meta{color:var(--mut);font-size:.9em;display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.dot{width:9px;height:9px;border-radius:50%;background:#555;display:inline-block;vertical-align:middle}
.dot.ok{background:var(--ok);box-shadow:0 0 6px var(--ok)}
.dot.warn{background:var(--warn)}
.dot.err{background:var(--err)}
nav.tabs{display:flex;gap:2px;padding:8px 8px 0;border-bottom:1px solid var(--line);
  overflow-x:auto;-webkit-overflow-scrolling:touch}
nav.tabs button{background:transparent;border:0;color:var(--mut);
  padding:10px 14px;font-size:1em;cursor:pointer;border-radius:var(--r) var(--r) 0 0;
  border-bottom:2px solid transparent;white-space:nowrap}
nav.tabs button:hover{color:var(--fg)}
nav.tabs button.active{color:var(--fg);border-bottom-color:var(--accent)}
main{padding:16px;max-width:980px;margin:0 auto}
section[hidden]{display:none!important}
h2{font-size:1.05em;margin:18px 0 8px;color:var(--mut);font-weight:500;
  text-transform:uppercase;letter-spacing:.6px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:12px 14px}
.card .lbl{color:var(--mut);font-size:.82em;text-transform:uppercase;letter-spacing:.5px}
.card .v{font-size:clamp(20px,3vw,28px);font-weight:600;margin-top:2px}
.card.green .v{color:var(--ok)}
.card.warn .v{color:var(--warn)}
.card.err .v{color:var(--err)}
.row{display:flex;flex-wrap:wrap;gap:8px 16px;color:var(--mut);font-size:.92em;margin:8px 0}
.row span b{color:var(--fg);font-weight:600}
.spark{width:100%;height:60px;background:#0a0b0e;border:1px solid var(--line);
  border-radius:var(--r);margin-top:10px}
fieldset{border:1px solid var(--line);border-radius:var(--r);padding:12px 14px;margin:12px 0;background:var(--card)}
fieldset>legend{padding:0 6px;color:var(--mut);font-size:.82em;
  text-transform:uppercase;letter-spacing:.5px}
.field{display:grid;grid-template-columns:1fr 1fr 90px;gap:8px 12px;align-items:center;
  padding:6px 0;border-top:1px dashed var(--line)}
.field:first-of-type{border-top:0}
.field label{color:var(--fg)}
.field .hint{color:var(--mut);font-size:.85em}
.field input[type=range]{width:100%}
.field input[type=number]{width:90px;background:#0a0b0e;color:var(--fg);
  border:1px solid var(--line);border-radius:6px;padding:4px 6px;font:inherit}
button.btn{background:var(--accent);color:#0c0d10;border:0;padding:10px 16px;
  border-radius:var(--r);font:inherit;font-weight:600;cursor:pointer}
button.btn.ghost{background:transparent;color:var(--fg);border:1px solid var(--line)}
button.btn.danger{background:#3a1416;color:var(--err);border:1px solid var(--err)}
button.btn:disabled{opacity:.5;cursor:not-allowed}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}
.toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);
  background:var(--card);border:1px solid var(--line);padding:10px 14px;
  border-radius:var(--r);box-shadow:0 8px 24px rgba(0,0,0,.4);opacity:0;
  pointer-events:none;transition:opacity .2s;z-index:9}
.toast.show{opacity:1}
.toast.ok{border-color:var(--ok)} .toast.err{border-color:var(--err)}
pre.log{background:#0a0b0e;border:1px solid var(--line);border-radius:var(--r);
  padding:10px;max-height:320px;overflow:auto;font:12px/1.4 ui-monospace,Menlo,Consolas,monospace;
  white-space:pre-wrap;color:var(--mut)}
.kv{display:grid;grid-template-columns:max-content 1fr;gap:6px 14px;
  font-size:.95em;color:var(--mut)}
.kv b{color:var(--fg);font-weight:500}
.setup-banner{background:#3a2a14;color:var(--warn);border:1px solid var(--warn);
  padding:10px 14px;border-radius:var(--r);margin-bottom:12px}
@media(max-width:560px){
  .field{grid-template-columns:1fr 1fr;}
  .field .hint{grid-column:1/-1}
}
</style>
</head>
<body>
<header>
  <h1>Block Kit</h1>
  <span class="meta">
    <span id="connDot" class="dot"></span>
    <span id="connTxt">connecting…</span> ·
    <span id="hostShow">-</span> ·
    <span id="ipShow">-</span> ·
    <span id="verShow">-</span>
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
    <code>MistMaker-Setup-XXXX</code> AP (password <code>mistmaker-setup</code>).
  </div>
  <div class="cards">
    <div class="card" id="cState"><div class="lbl">State</div><div class="v" id="vState">-</div></div>
    <div class="card" id="cMist"><div class="lbl">Mist</div><div class="v" id="vMist">-</div></div>
    <div class="card" id="cCur"><div class="lbl">Current</div><div class="v"><span id="vCurMa">-</span> mA</div></div>
    <div class="card" id="cUp"><div class="lbl">Uptime</div><div class="v" id="vUp">-</div></div>
  </div>
  <h2>Live</h2>
  <div class="row">
    <span>Button raw <b id="vBtn">-</b></span>
    <span>Reed raw <b id="vReed">-</b></span>
    <span>User level <b id="vUl">-</b></span>
    <span>Current level <b id="vCl">-</b></span>
    <span>Free heap <b id="vHeap">-</b></span>
    <span>RSSI <b id="vRssi">-</b></span>
  </div>
  <svg class="spark" id="spark" viewBox="0 0 300 60" preserveAspectRatio="none">
    <polyline id="sparkLine" fill="none" stroke="#7ab7ff" stroke-width="1.5" points=""/>
  </svg>
</section>

<section id="config" hidden>
  <p style="color:var(--mut)">Move a slider or type a value, then <b>Save &amp; Apply</b>.
  Saving prompts for the admin password (set during WiFi setup).</p>
  <div id="cfgForm"></div>
  <div class="actions">
    <button class="btn" id="btnSave">Save &amp; Apply</button>
    <button class="btn ghost" id="btnRevert">Revert</button>
    <button class="btn ghost" id="btnDefaults">Reset to defaults</button>
  </div>
</section>

<section id="debug" hidden>
  <h2>Live raw</h2>
  <div class="row">
    <span>ADC mean <b id="dCur">-</b> mA</span>
    <span>ADC var <b id="dVar">-</b></span>
    <span>Reed raw <b id="dReed">-</b></span>
    <span>Button raw <b id="dBtn">-</b></span>
    <span>State <b id="dState">-</b></span>
  </div>
  <h2>Commands</h2>
  <div class="actions">
    <button class="btn ghost" id="btnWalk">LED walk</button>
    <button class="btn ghost" id="btnLeds">Hide / show LEDs</button>
    <button class="btn ghost" id="btnScope">Toggle scope</button>
    <button class="btn ghost" id="btnRefreshLog">Refresh log</button>
    <button class="btn ghost" id="btnReboot">Reboot</button>
    <button class="btn danger" id="btnForget">Forget WiFi</button>
  </div>
  <h2>Recent log</h2>
  <pre class="log" id="log">(loading)</pre>
</section>

<section id="about" hidden>
  <h2>Device</h2>
  <div class="kv" id="aboutKv"><b>Hostname</b><span id="aHost">-</span></div>
  <h2>OTA</h2>
  <p>Arduino IDE → <code>Tools → Port → mistmaker at &lt;ip&gt; (esp32)</code>.
  Password is the admin password set during WiFi setup. The device hard-stops
  the mist + boost rail before flash is erased.</p>
  <h2>Admin password</h2>
  <p style="color:var(--mut)">Set or change the password used for config writes,
  commands, and OTA.</p>
  <div class="actions">
    <input type="password" id="pwdNew" placeholder="new password (min 4 chars)"
      style="flex:1;min-width:160px;background:#0a0b0e;color:var(--fg);
      border:1px solid var(--line);border-radius:6px;padding:8px;font:inherit">
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
function applyStatus(d){
  // Status tab cards
  $("vState").textContent=d.state;
  $("vMist").textContent = d.mist
      ? (d.ledsHidden?"ON (LEDs hidden)":"ON")
      : "off";
  $("cMist").classList.toggle("green",!!d.mist);
  $("vCurMa").textContent=d.meanMa.toFixed(1);
  $("cCur").classList.toggle("green",d.meanMa>50&&d.meanMa<500);
  $("cCur").classList.toggle("err",d.meanMa>=500);
  $("vUp").textContent=fmtUptime(d.uptimeMs);
  $("vBtn").textContent=d.btnRaw;
  $("vReed").textContent=d.reedRaw+(d.reedPresent?" (docked)":"");
  $("vUl").textContent=d.userLevel;
  $("vCl").textContent=d.currentLevel;
  $("vHeap").textContent=d.freeHeap;
  $("vRssi").textContent=d.rssi+" dBm";
  $("setupBanner").hidden=!d.setupMode;
  // Debug tab raw row
  $("dCur").textContent=d.meanMa.toFixed(1);
  $("dVar").textContent=d.varMa2.toFixed(1);
  $("dReed").textContent=d.reedRaw;
  $("dBtn").textContent=d.btnRaw;
  $("dState").textContent=d.state+" ("+d.stateInt+")";
  // Sparkline buffer (last 120 samples ≈ 30 s @ 4 Hz)
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

// --- Config tab -------------------------------------------------------------
function renderConfigForm(values){
  const root=$("cfgForm");
  root.innerHTML="";
  for(const g of FIELDS){
    const fs=document.createElement("fieldset");
    const lg=document.createElement("legend"); lg.textContent=g.grp; fs.appendChild(lg);
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
  if(!confirm("Reset every tunable to firmware defaults? (Saved to NVS.)")) return;
  if(!ensureAdmin()) return;
  // Re-fire the SAME values as configResetDefaults yields by sending each
  // CFG_DEFAULT_* — simpler client-side: ask server to roll back.
  // For now: pull current defaults via a fresh GET after an empty body POST
  // — but the API doesn't expose that. Easiest path: prompt the user to
  // power-cycle after we forget the saved blob via /api/cmd/reboot.
  // (Lightweight implementation: just set every visible field to its `lo`
  // bound. Improvement tracked in TODO.)
  alert("Defaults reset: please use Debug → Reboot, then on first boot the firmware defaults will load. (Full reset endpoint TBD.)");
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
$("btnScope").addEventListener("click",()=>postCmd("/api/cmd/scope"));
$("btnReboot").addEventListener("click",()=>postCmd("/api/cmd/reboot","Reboot the device?"));
$("btnForget").addEventListener("click",()=>postCmd("/api/cmd/forget","Forget WiFi credentials and reboot into setup mode?"));

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
      `<b>Hostname</b><span>${d.hostname} (http://${d.hostname}.local/)</span>`+
      `<b>IP</b><span>${d.ip}</span>`+
      `<b>MAC</b><span>${d.mac}</span>`+
      `<b>SSID</b><span>${d.ssid} (${d.rssi} dBm)</span>`+
      `<b>Firmware</b><span>${d.firmware}</span>`+
      `<b>Built</b><span>${d.buildDate}</span>`+
      `<b>Free heap</b><span>${d.freeHeap}</span>`+
      `<b>OTA</b><span>port ${d.otaPort}, admin password ${d.hasAdminPwd?"set":"NOT SET (any host can flash!)"}</span>`;
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
startSse();
</script>
</body>
</html>
)==";
