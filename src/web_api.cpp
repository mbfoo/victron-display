/*
 * web_api.cpp — REST routes + embedded HTML served from flash (PROGMEM)
 *
 * GET  /             dashboard (live PV data, auto-refreshes every 5 s)
 * GET  /config       configuration page (WiFi, Victron, MQTT, Display)
 * GET  /api/data     JSON live + config data used by both pages
 * POST /api/wifi     save WiFi profiles + AP settings
 * POST /api/victron  save Victron device config (MAC + AES key)
 * POST /api/mqtt     save MQTT settings (incl. TLS, username, password, CA cert)
 * POST /api/display  save display settings
 * POST /api/reboot   reboot ESP
 */

#include "web_api.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "victron_ble.h"
#include "mqtt_client.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// ─────────────────────────────────────────────────────────────────────────
// Dashboard HTML
// ─────────────────────────────────────────────────────────────────────────
static const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Victron Monitor</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh}
  header{background:#16213e;padding:1rem;display:flex;justify-content:space-between;align-items:center}
  header h1{font-size:1.2rem;color:#e94560}
  nav a{color:#aaa;text-decoration:none;margin-left:1rem;font-size:.9rem}
  nav a:hover{color:#e94560}
  .container{max-width:900px;margin:0 auto;padding:1rem}
  .total-card{background:linear-gradient(135deg,#e94560,#0f3460);border-radius:12px;
    padding:1.5rem;text-align:center;margin-bottom:1.5rem}
  .total-card .value{font-size:3rem;font-weight:700}
  .total-card .label{font-size:.9rem;opacity:.8;margin-top:.25rem}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:1rem}
  .card{background:#16213e;border-radius:10px;padding:1rem;border:1px solid #0f3460}
  .card-title{font-size:.8rem;color:#aaa;text-transform:uppercase;letter-spacing:.05em}
  .card-value{font-size:1.6rem;font-weight:600;margin:.25rem 0}
  .card-sub{font-size:.8rem;color:#aaa}
  .badge{display:inline-block;padding:.15rem .5rem;border-radius:4px;font-size:.75rem;font-weight:600}
  .badge-ok{background:#0d7a3e;color:#fff}
  .badge-warn{background:#b45309;color:#fff}
  .badge-err{background:#b91c1c;color:#fff}
  .badge-off{background:#374151;color:#aaa}
  .badge-offline{background:#1f2937;color:#9ca3af;border:1px solid #374151}
  .device-list{margin-top:1.5rem}
  .device-card{background:#16213e;border-radius:10px;padding:1rem;margin-bottom:1rem;
    border-left:4px solid #e94560;transition:opacity .3s}
  .device-card.offline{border-left-color:#374151;opacity:.55}
  .device-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:.75rem}
  .device-name{font-weight:600}
  .device-mac{font-size:.75rem;color:#aaa}
  .metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:.5rem}
  .metric{text-align:center;background:#0f3460;border-radius:6px;padding:.5rem}
  .metric.dim{background:#111827}
  .metric-val{font-size:1.1rem;font-weight:600}
  .metric-val.muted{color:#4b5563}
  .metric-lbl{font-size:.7rem;color:#aaa;margin-top:.15rem}
  .offline-notice{background:#111827;border-radius:6px;padding:.6rem .9rem;
    margin-top:.5rem;font-size:.82rem;color:#6b7280;display:flex;
    align-items:center;gap:.5rem}
  .offline-dot{width:8px;height:8px;border-radius:50%;background:#374151;
    flex-shrink:0;display:inline-block}
  .state-bar{background:#0f3460;border-radius:6px;padding:.5rem .75rem;margin-top:.5rem;
    display:flex;justify-content:space-between;font-size:.8rem;flex-wrap:wrap;gap:.25rem}
  .state-bar.dim{background:#111827;color:#4b5563}
  footer{text-align:center;padding:1.5rem;color:#555;font-size:.8rem}
  .refresh-bar{display:flex;justify-content:flex-end;margin-bottom:1rem;align-items:center;
    gap:.5rem;font-size:.8rem;color:#aaa}
  .btn{background:#e94560;color:#fff;border:none;border-radius:6px;padding:.4rem .9rem;
    cursor:pointer;font-size:.8rem;text-decoration:none;display:inline-block}
  .btn:hover{background:#c73652}
</style>
</head>
<body>
<header>
  <h1>&#9728; Victron Monitor</h1>
  <nav><a href="/">Dashboard</a><a href="/config">Config</a></nav>
</header>
<div class="container">
  <div class="refresh-bar">
    <span id="last-update">—</span>
    <button class="btn" onclick="fetchData()">Refresh</button>
  </div>
  <div class="total-card">
    <div class="value" id="total-pv">— W</div>
    <div class="label">Total PV Power</div>
  </div>
  <div class="grid">
    <div class="card">
      <div class="card-title">Devices Online</div>
      <div class="card-value" id="devices-online">—</div>
      <div class="card-sub" id="devices-total">of — configured</div>
    </div>
    <div class="card">
      <div class="card-title">WiFi</div>
      <div class="card-value" id="wifi-ssid">—</div>
      <div class="card-sub" id="wifi-ip">—</div>
    </div>
    <div class="card">
      <div class="card-title">MQTT</div>
      <div class="card-value" id="mqtt-state">—</div>
      <div class="card-sub" id="mqtt-pub">publishes: —</div>
    </div>
  </div>
  <div class="device-list" id="device-list"></div>
</div>
<footer>Victron MPPT Monitor &bull; ESP32-C6</footer>
<script>
const stateMap={0:"Off",1:"Low Power",2:"Fault",3:"Bulk",4:"Absorption",
  5:"Float",6:"Storage",7:"Equalize",9:"Inverting",11:"Power Supply",
  245:"Starting",247:"Repeat Abs",248:"Auto Eq",249:"Bat Safe",252:"Ext Ctrl"};
function stateName(s){return stateMap[s]||("State "+s);}
function stateClass(s){
  if([3,4,5,6,248].includes(s)) return "badge-ok";
  if([0,1].includes(s)) return "badge-off";
  return "badge-err";
}
const mqttStates=["Disabled","Waiting WiFi","Connecting","Connected","Disconnected"];

function fetchData(){
  fetch('/api/data').then(r=>r.json()).then(d=>{
    document.getElementById('total-pv').textContent=d.total_pv_w.toFixed(0)+' W';
    const online=d.devices.filter(x=>x.valid).length;
    document.getElementById('devices-online').textContent=online;
    document.getElementById('devices-total').textContent='of '+d.devices.length+' configured';
    document.getElementById('wifi-ssid').textContent=d.wifi.ssid||'—';
    document.getElementById('wifi-ip').textContent=d.wifi.ip+' | '+d.wifi.rssi+' dBm';
    document.getElementById('mqtt-state').textContent=mqttStates[d.mqtt.state]||'—';
    document.getElementById('mqtt-pub').textContent='publishes: '+d.mqtt.publish_count;
    document.getElementById('last-update').textContent='Updated: '+new Date().toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false});

    const list=document.getElementById('device-list');
    list.innerHTML='';
    d.devices.forEach((dev,i)=>{
      const on=dev.valid;
      const pvVal   = on ? dev.pv_power_w.toFixed(0)+' W'        : '—';
      const batVal  = on ? dev.battery_voltage_v.toFixed(2)+' V'  : '—';
      const curVal  = on ? dev.battery_current_a.toFixed(1)+' A'  : '—';
      const yldVal  = on ? dev.yield_today_kwh.toFixed(2)+' kWh'  : '—';
      const rssiVal = on ? dev.rssi+' dBm'                        : '—';

      const headerBadge = on
        ? `<span class="badge ${stateClass(dev.charger_state)}">${stateName(dev.charger_state)}</span>`
        : `<span class="badge badge-offline">&#9679; Offline</span>`;

      const bottomSection = on
        ? `<div class="state-bar">
             <span>Yield today: ${yldVal}</span>
             <span>RSSI: ${rssiVal}</span>
             ${dev.error_code?'<span style="color:#f87171">Error: '+dev.error_code+'</span>':''}
           </div>`
        : `<div class="offline-notice">
             <span class="offline-dot"></span>
             No BLE advertisement received for &gt;30 s &mdash; device out of range or powered off
           </div>`;

      list.innerHTML+=`
      <div class="device-card ${on?'':'offline'}">
        <div class="device-header">
          <div>
            <div class="device-name">${dev.name||'Device '+i}</div>
            <div class="device-mac">${dev.mac}</div>
          </div>
          ${headerBadge}
        </div>
        <div class="metrics">
          <div class="metric${on?'':' dim'}">
            <div class="metric-val${on?'':' muted'}">${pvVal}</div>
            <div class="metric-lbl">PV Power</div>
          </div>
          <div class="metric${on?'':' dim'}">
            <div class="metric-val${on?'':' muted'}">${batVal}</div>
            <div class="metric-lbl">Battery</div>
          </div>
          <div class="metric${on?'':' dim'}">
            <div class="metric-val${on?'':' muted'}">${curVal}</div>
            <div class="metric-lbl">Current</div>
          </div>
        </div>
        ${bottomSection}
      </div>`;
    });
  }).catch(e=>console.error(e));
}
fetchData();
setInterval(fetchData,5000);
</script>
</body></html>
)rawhtml";

// ─────────────────────────────────────────────────────────────────────────
// Config HTML
// ─────────────────────────────────────────────────────────────────────────
static const char CONFIG_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Victron Monitor – Config</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee}
  header{background:#16213e;padding:1rem;display:flex;justify-content:space-between;align-items:center}
  header h1{font-size:1.2rem;color:#e94560}
  nav a{color:#aaa;text-decoration:none;margin-left:1rem;font-size:.9rem}
  nav a:hover{color:#e94560}
  .container{max-width:800px;margin:0 auto;padding:1rem}
  .section{background:#16213e;border-radius:10px;padding:1.25rem;margin-bottom:1.5rem;
    border:1px solid #0f3460}
  .section h2{font-size:1rem;color:#e94560;margin-bottom:1rem;border-bottom:1px solid #0f3460;
    padding-bottom:.5rem}
  .form-row{margin-bottom:.75rem}
  label{display:block;font-size:.8rem;color:#aaa;margin-bottom:.25rem}
  input,select,textarea{width:100%;background:#0f3460;border:1px solid #1e3a5f;border-radius:6px;
    padding:.5rem .75rem;color:#eee;font-size:.9rem}
  textarea{font-family:monospace;font-size:.8rem;resize:vertical}
  input:focus,textarea:focus{outline:none;border-color:#e94560}
  .btn{background:#e94560;color:#fff;border:none;border-radius:6px;padding:.5rem 1.2rem;
    cursor:pointer;font-size:.9rem;margin-top:.5rem}
  .btn:hover{background:#c73652}
  .btn-secondary{background:#374151}
  .btn-secondary:hover{background:#4b5563}
  .block{background:#0f3460;border-radius:8px;padding:1rem;margin-bottom:.75rem}
  .block-header{display:flex;justify-content:space-between;align-items:center;
    margin-bottom:.75rem;font-weight:600}
  .grid2{display:grid;grid-template-columns:1fr 1fr;gap:.75rem}
  .cb-row{display:flex;align-items:center;gap:.5rem;margin-bottom:.5rem}
  .cb-row input{width:auto}
  .toast{position:fixed;bottom:1rem;right:1rem;background:#0d7a3e;color:#fff;
    padding:.75rem 1.25rem;border-radius:8px;display:none;font-size:.9rem;z-index:999}
  .hint{font-size:.75rem;color:#6b7280;margin-top:.25rem}
  @media(max-width:480px){.grid2{grid-template-columns:1fr}}
</style>
</head>
<body>
<header>
  <h1>&#9728; Victron Monitor</h1>
  <nav><a href="/">Dashboard</a><a href="/config">Config</a></nav>
</header>
<div class="container">

<div class="section">
  <h2>WiFi Profiles (up to 10)</h2>
  <div id="wifi-profiles"></div>
  <button class="btn btn-secondary" onclick="addWifi()">+ Add Profile</button>
  <div style="margin-top:1rem">
    <div class="cb-row"><input type="checkbox" id="ap-enabled">
      <label for="ap-enabled" style="margin:0">Enable AP mode fallback</label></div>
    <div class="grid2">
      <div class="form-row"><label>AP SSID</label><input id="ap-ssid"></div>
      <div class="form-row"><label>AP Password</label><input id="ap-pass" type="password"></div>
    </div>
  </div>
  <button class="btn" onclick="saveWifi()">Save WiFi</button>
</div>

<div class="section">
  <h2>Victron MPPT Devices (up to 5)</h2>
  <p style="font-size:.8rem;color:#aaa;margin-bottom:.75rem">
    Get the BLE MAC and AES key from VictronConnect app<br>
    (Victron Connect &rarr; device &rarr; Settings &rarr; Product info &rarr; Advertising key)
  </p>
  <div id="victron-devices"></div>
  <button class="btn btn-secondary" onclick="addVictron()">+ Add Device</button>
  <br><button class="btn" onclick="saveVictron()" style="margin-top:.75rem">Save Devices</button>
</div>

<div class="section">
  <h2>MQTT</h2>
  <div class="cb-row" style="margin-bottom:.75rem">
    <input type="checkbox" id="mqtt-enabled">
    <label for="mqtt-enabled" style="margin:0">Enable MQTT</label>
  </div>
  <div class="grid2">
    <div class="form-row"><label>Broker host / IP</label><input id="mqtt-server" placeholder="192.168.1.10"></div>
    <div class="form-row"><label>Port</label><input id="mqtt-port" type="number" value="1883"></div>
    <div class="form-row"><label>Topic base</label><input id="mqtt-topic" placeholder="victron"></div>
    <div class="form-row"><label>Publish interval (s)</label><input id="mqtt-interval" type="number" value="30"></div>
    <div class="form-row"><label>Username (optional)</label><input id="mqtt-user" autocomplete="off"></div>
    <div class="form-row"><label>Password (optional)</label>
      <input id="mqtt-pass" type="password" placeholder="leave blank to keep current" autocomplete="new-password"></div>
  </div>
  <div class="cb-row" style="margin-top:.5rem">
    <input type="checkbox" id="mqtt-tls" onchange="document.getElementById('tls-extra').style.display=this.checked?'':'none'">
    <label for="mqtt-tls" style="margin:0">Enable TLS / MQTTS (port 8883)</label>
  </div>
  <div id="tls-extra" style="display:none;margin-top:.75rem;background:#0f3460;border-radius:8px;padding:1rem">
    <div class="form-row">
      <label>CA Certificate (PEM) — leave empty to skip server verification</label>
      <textarea id="mqtt-ca" rows="7"
        placeholder="-----BEGIN CERTIFICATE-----&#10;MIIDxTCCAq2gAwIBAgIQ...&#10;-----END CERTIFICATE-----"></textarea>
      <div class="hint" id="ca-status"></div>
    </div>
  </div>
  <button class="btn" onclick="saveMqtt()">Save MQTT</button>
</div>

<div class="section">
  <h2>Display (Waveshare ESP32-C6 Touch 1.47")</h2>
  <div class="grid2">
    <div class="form-row"><label>Brightness (%)</label>
      <input id="brightness" type="number" min="10" max="100"></div>
    <div class="form-row"><label>Screen off timeout (s)</label>
      <input id="disp-timeout" type="number"></div>
  </div>
  <button class="btn" onclick="saveDisplay()">Save Display</button>
</div>

<div class="section">
  <h2>System</h2>
  <button class="btn" style="background:#b91c1c" onclick="reboot()">&#8635; Reboot Device</button>
</div>

</div>
<div class="toast" id="toast"></div>
<script>
let wifiProfiles=[], victronDevices=[];

function showToast(msg,ok=true){
  const t=document.getElementById('toast');
  t.textContent=msg; t.style.background=ok?'#0d7a3e':'#b91c1c';
  t.style.display='block'; setTimeout(()=>t.style.display='none',2500);
}

function escH(s){return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;');}

function renderWifi(){
  const el=document.getElementById('wifi-profiles');
  el.innerHTML=wifiProfiles.map((p,i)=>`
    <div class="block">
      <div class="block-header"><span>Profile ${i+1}</span>
        <button class="btn btn-secondary" style="padding:.2rem .6rem;font-size:.75rem;margin:0"
          onclick="wifiProfiles.splice(${i},1);renderWifi()">Remove</button></div>
      <div class="grid2">
        <div class="form-row"><label>SSID</label>
          <input oninput="wifiProfiles[${i}].ssid=this.value" value="${escH(p.ssid)}"></div>
        <div class="form-row"><label>Password</label>
          <input type="password" oninput="wifiProfiles[${i}].pass=this.value" value="${escH(p.pass)}"></div>
      </div></div>`).join('');
}

function renderVictron(){
  const el=document.getElementById('victron-devices');
  el.innerHTML=victronDevices.map((d,i)=>`
    <div class="block">
      <div class="block-header"><span>Device ${i+1}</span>
        <button class="btn btn-secondary" style="padding:.2rem .6rem;font-size:.75rem;margin:0"
          onclick="victronDevices.splice(${i},1);renderVictron()">Remove</button></div>
      <div class="grid2">
        <div class="form-row"><label>Friendly name</label>
          <input oninput="victronDevices[${i}].name=this.value" value="${escH(d.name)}"></div>
        <div class="form-row"><label>BLE MAC</label>
          <input placeholder="AA:BB:CC:DD:EE:FF" oninput="victronDevices[${i}].mac=this.value"
            value="${escH(d.mac)}"></div>
      </div>
      <div class="form-row"><label>AES Advertising Key (32 hex chars)</label>
        <input placeholder="00112233445566778899aabbccddeeff"
          oninput="victronDevices[${i}].key=this.value" value="${escH(d.key)}"></div>
      <div class="cb-row" style="margin-top:.5rem">
        <input type="checkbox" id="ve${i}" ${d.enabled?'checked':''}
          onchange="victronDevices[${i}].enabled=this.checked">
        <label for="ve${i}" style="margin:0">Enabled</label></div>
    </div>`).join('');
}

function addWifi(){
  if(wifiProfiles.length<10){wifiProfiles.push({ssid:'',pass:''});renderWifi();}
  else showToast('Max 10 profiles',false);
}
function addVictron(){
  if(victronDevices.length<5){victronDevices.push({name:'',mac:'',key:'',enabled:true});renderVictron();}
  else showToast('Max 5 devices',false);
}

function saveWifi(){
  const profiles=wifiProfiles.filter(p=>p.ssid.trim());
  fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({profiles,
      ap_enabled:document.getElementById('ap-enabled').checked,
      ap_ssid:document.getElementById('ap-ssid').value,
      ap_pass:document.getElementById('ap-pass').value
    })}).then(r=>r.json()).then(d=>showToast(d.ok?'WiFi saved!':'Error: '+d.err,d.ok));
}

function saveVictron(){
  fetch('/api/victron',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({devices:victronDevices})})
    .then(r=>r.json()).then(d=>showToast(d.ok?'Devices saved!':'Error: '+d.err,d.ok));
}

function saveMqtt(){
  const tlsOn=document.getElementById('mqtt-tls').checked;
  fetch('/api/mqtt',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({
      server:  document.getElementById('mqtt-server').value,
      port:   +document.getElementById('mqtt-port').value,
      topic:   document.getElementById('mqtt-topic').value,
      interval:+document.getElementById('mqtt-interval').value,
      enabled: document.getElementById('mqtt-enabled').checked,
      tls_enabled: tlsOn,
      username: document.getElementById('mqtt-user').value,
      password: document.getElementById('mqtt-pass').value,
      ca_cert:  tlsOn ? document.getElementById('mqtt-ca').value : ''
    })}).then(r=>r.json()).then(d=>showToast(d.ok?'MQTT saved!':'Error: '+d.err,d.ok));
}

function saveDisplay(){
  fetch('/api/display',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({
      brightness:+document.getElementById('brightness').value,
      timeout:+document.getElementById('disp-timeout').value
    })}).then(r=>r.json()).then(d=>showToast(d.ok?'Saved!':'Error: '+d.err,d.ok));
}

function reboot(){
  if(confirm('Reboot device now?'))
    fetch('/api/reboot',{method:'POST'}).then(()=>showToast('Rebooting…'));
}

fetch('/api/data').then(r=>r.json()).then(d=>{
  wifiProfiles=(d.wifi_profiles||[]).map(p=>({ssid:p.ssid,pass:''}));
  renderWifi();
  document.getElementById('ap-enabled').checked=!!d.ap_enabled;
  document.getElementById('ap-ssid').value=d.ap_ssid||'';

  victronDevices=(d.victron_cfg||[]).map(v=>({name:v.name,mac:v.mac,key:'',enabled:v.enabled}));
  renderVictron();

  const mc=d.mqtt_cfg||{};
  document.getElementById('mqtt-server').value=mc.server||'';
  document.getElementById('mqtt-port').value=mc.port||1883;
  document.getElementById('mqtt-topic').value=mc.topic||'victron';
  document.getElementById('mqtt-interval').value=mc.interval||30;
  document.getElementById('mqtt-enabled').checked=!!mc.enabled;
  document.getElementById('mqtt-tls').checked=!!mc.tls_enabled;
  document.getElementById('mqtt-user').value=mc.username||'';
  if(mc.tls_enabled) document.getElementById('tls-extra').style.display='';
  const caStatus=document.getElementById('ca-status');
  caStatus.textContent=mc.has_ca_cert
    ? '\u2714 CA cert stored — paste a new PEM to replace, or save empty to clear'
    : 'No CA cert stored — leave empty to skip server verification (insecure)';

  const dc=d.display||{};
  document.getElementById('brightness').value=dc.brightness||80;
  document.getElementById('disp-timeout').value=dc.timeout||600;
});
</script>
</body></html>
)rawhtml";

// ─────────────────────────────────────────────────────────────────────────
// Route implementation
// ─────────────────────────────────────────────────────────────────────────
static WebServer* s_server = nullptr;

static void sendJson(const String& json, int code = 200) {
    s_server->send(code, "application/json", json);
}

static void handleApiData() {
    DynamicJsonDocument doc(4096);
    uint8_t n = victronBleGetDeviceCount();
    const VictronMpptData* devs = victronBleGetDevices();

    doc["total_pv_w"] = victronBleGetTotalPvPower();
    JsonArray arr = doc.createNestedArray("devices");
    for (uint8_t i = 0; i < n; i++) {
        JsonObject o = arr.createNestedObject();
        o["name"]              = devs[i].name;
        o["mac"]               = devs[i].mac;
        o["valid"]             = devs[i].valid;
        o["pv_power_w"]        = devs[i].pvPower_W;
        o["battery_voltage_v"] = devs[i].batteryVoltage_V;
        o["battery_current_a"] = devs[i].batteryCurrent_A;
        o["yield_today_kwh"]   = devs[i].yieldToday_kWh;
        o["charger_state"]     = (int)devs[i].chargerState;
        o["error_code"]        = (int)devs[i].errorCode;
        o["rssi"]              = devs[i].rssi;
    }

    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["ssid"] = wifiGetSsid();
    wifi["ip"]   = wifiGetIp();
    wifi["rssi"] = wifiGetRssi();

    JsonObject mqtt = doc.createNestedObject("mqtt");
    mqtt["state"]         = (int)mqttGetState();
    mqtt["publish_count"] = mqttGetPublishCount();

    JsonArray wp = doc.createNestedArray("wifi_profiles");
    for (uint8_t i = 0; i < configGetWifiProfileCount(); i++) {
        JsonObject p = wp.createNestedObject();
        p["ssid"] = configGetWifiProfile(i).ssid;
    }
    doc["ap_enabled"] = configGetApEnabled();
    doc["ap_ssid"]    = configGetApSsid();

    JsonArray vc = doc.createNestedArray("victron_cfg");
    for (uint8_t i = 0; i < configGetVictronCount(); i++) {
        const VictronDeviceCfg& c = configGetVictronDevice(i);
        JsonObject o = vc.createNestedObject();
        o["name"]    = c.name;
        o["mac"]     = c.mac;
        o["enabled"] = c.enabled;
    }

    JsonObject mc = doc.createNestedObject("mqtt_cfg");
    mc["server"]      = configGetMqttServer();
    mc["port"]        = configGetMqttPort();
    mc["topic"]       = configGetMqttTopic();
    mc["interval"]    = configGetMqttInterval();
    mc["enabled"]     = configGetMqttEnabled();
    mc["tls_enabled"] = configGetMqttTlsEnabled();
    mc["username"]    = configGetMqttUsername();
    // Never send the password back — send a boolean instead
    mc["has_password"] = strlen(configGetMqttPassword()) > 0;
    // Indicate if a CA cert is stored
    char certPeek[8];
    configGetMqttCaCert(certPeek, sizeof(certPeek));
    mc["has_ca_cert"] = strlen(certPeek) > 0;

    JsonObject dc = doc.createNestedObject("display");
    dc["brightness"] = configGetBacklight();
    dc["timeout"]    = configGetDisplayTimeout();

    String out; serializeJson(doc, out);
    sendJson(out);
}

static void handleSaveWifi() {
    if (!s_server->hasArg("plain")) { sendJson("{\"ok\":false,\"err\":\"no body\"}"); return; }
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, s_server->arg("plain"))) {
        sendJson("{\"ok\":false,\"err\":\"json\"}"); return;
    }
    uint8_t cnt = 0;
    for (JsonObject p : doc["profiles"].as<JsonArray>()) {
        if (cnt >= MAX_WIFI_PROFILES) break;
        configSetWifiProfile(cnt++, p["ssid"] | "", p["pass"] | "");
    }
    configSetWifiProfileCount(cnt);
    configSetApEnabled(doc["ap_enabled"] | false);
    configSetApSsid(doc["ap_ssid"] | "");
    configSetApPassword(doc["ap_pass"] | "");
    configSave();
    wifiApplyConfig();
    sendJson("{\"ok\":true}");
}

static void handleSaveVictron() {
    if (!s_server->hasArg("plain")) { sendJson("{\"ok\":false,\"err\":\"no body\"}"); return; }
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, s_server->arg("plain"))) {
        sendJson("{\"ok\":false,\"err\":\"json\"}"); return;
    }
    uint8_t cnt = 0;
    for (JsonObject d : doc["devices"].as<JsonArray>()) {
        if (cnt >= MAX_VICTRON_DEVICES) break;
        configSetVictronDevice(cnt++, d["name"]|"", d["mac"]|"", d["key"]|"", d["enabled"]|true);
    }
    configSetVictronCount(cnt);
    configSave();
    victronBleApplyConfig();
    sendJson("{\"ok\":true}");
}

static void handleSaveMqtt() {
    if (!s_server->hasArg("plain")) { sendJson("{\"ok\":false,\"err\":\"no body\"}"); return; }
    DynamicJsonDocument doc(4096);  // larger to accommodate CA cert PEM
    if (deserializeJson(doc, s_server->arg("plain"))) {
        sendJson("{\"ok\":false,\"err\":\"json\"}"); return;
    }
    configSetMqttServer(doc["server"]    | "");
    configSetMqttPort(doc["port"]        | 1883);
    configSetMqttTopic(doc["topic"]      | "victron");
    configSetMqttInterval(doc["interval"]| 30);
    configSetMqttEnabled(doc["enabled"]  | false);
    configSetMqttTlsEnabled(doc["tls_enabled"] | false);
    if (doc.containsKey("username")) configSetMqttUsername(doc["username"] | "");
    // Only update password if a non-empty value was supplied
    const char* newPass = doc["password"] | "";
    if (strlen(newPass) > 0) configSetMqttPassword(newPass);
    // Update CA cert (empty string clears it)
    if (doc.containsKey("ca_cert")) configSetMqttCaCert(doc["ca_cert"] | "");
    configSave();
    mqttApplyConfig();
    sendJson("{\"ok\":true}");
}

static void handleSaveDisplay() {
    if (!s_server->hasArg("plain")) { sendJson("{\"ok\":false,\"err\":\"no body\"}"); return; }
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, s_server->arg("plain"))) {
        sendJson("{\"ok\":false,\"err\":\"json\"}"); return;
    }
    configSetBacklight(doc["brightness"]   | 80);
    configSetDisplayTimeout(doc["timeout"] | 600);
    configSave();
    sendJson("{\"ok\":true}");
}

static void handleReboot() {
    sendJson("{\"ok\":true}"); delay(300); ESP.restart();
}

void webApiRegisterRoutes(WebServer& server) {
    s_server = &server;
    server.on("/", HTTP_GET, [&server]{
        server.send_P(200, "text/html", DASHBOARD_HTML);
    });
    server.on("/config", HTTP_GET, [&server]{
        server.send_P(200, "text/html", CONFIG_HTML);
    });
    server.on("/api/data",    HTTP_GET,  handleApiData);
    server.on("/api/wifi",    HTTP_POST, handleSaveWifi);
    server.on("/api/victron", HTTP_POST, handleSaveVictron);
    server.on("/api/mqtt",    HTTP_POST, handleSaveMqtt);
    server.on("/api/display", HTTP_POST, handleSaveDisplay);
    server.on("/api/reboot",  HTTP_POST, handleReboot);
    server.onNotFound([&server]{
        server.send(404, "text/plain", "Not found");
    });
}
