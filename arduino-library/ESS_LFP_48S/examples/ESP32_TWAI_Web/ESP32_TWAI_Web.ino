#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "ESS_LFP_48S.h"
#include "driver/twai.h"

// ===== User config =====
#ifndef WIFI_MODE_AP
#define WIFI_MODE_AP 1 // 1 = softAP, 0 = connect to STA
#endif

#if WIFI_MODE_AP
  #ifndef WIFI_AP_SSID
  #define WIFI_AP_SSID "ESS48S_BMS"
  #endif
  #ifndef WIFI_AP_PASS
  #define WIFI_AP_PASS "ess48spass" // min 8 chars
  #endif
#else
  #ifndef WIFI_STA_SSID
  #define WIFI_STA_SSID "your-ssid"
  #endif
  #ifndef WIFI_STA_PASS
  #define WIFI_STA_PASS "your-pass"
  #endif
#endif

#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN 4
#endif
#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN 5
#endif

ESS_LFP_48S bms;
WebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESS 48S BMS</title>
<style>
body{font-family:system-ui,Segoe UI,Arial,sans-serif;background:#0b1220;color:#e5e7eb;margin:0;padding:16px}
.card{background:#111827;border:1px solid #1f2937;border-radius:10px;padding:16px;margin-bottom:16px}
h1{margin:0 0 8px 0;font-size:20px}
.grid{display:grid;gap:8px}
.cells{grid-template-columns:repeat(12,1fr)}
.temps{grid-template-columns:repeat(12,1fr)}
.bad{color:#f87171}
.good{color:#34d399}
.small{font-size:12px;color:#9ca3af}
.tile{background:#0f172a;border:1px solid #1f2937;border-radius:8px;padding:8px;text-align:center}
</style>
</head>
<body>
  <div class="card">
    <h1>ESS 48S BMS Live</h1>
    <div id="summary" class="small">Loading...</div>
  </div>
  <div class="card">
    <h1>Cells (48)</h1>
    <div id="cells" class="grid cells"></div>
  </div>
  <div class="card">
    <h1>Temperatures (24)</h1>
    <div id="temps" class="grid temps"></div>
  </div>
<script>
async function fetchData(){
  const r = await fetch('/api');
  if(!r.ok) return;
  const j = await r.json();
  const s = document.getElementById('summary');
  s.innerHTML = `Pack ${j.packVoltage?.toFixed(1)} V | Δ ${j.cellDeltaV?.toFixed(3)} V | Max ${j.maxCellV?.toFixed(3)} V | Min ${j.minCellV?.toFixed(3)} V | AvgT ${j.avgTempC?.toFixed(2)} °C | MinT ${j.minTempC?.toFixed(2)} °C | Cells ${j.cellCount} | Temps ${j.tempCount} | Module ${j.moduleIndex} | Cap ${j.capacity}`;
  const cells = document.getElementById('cells');
  cells.innerHTML = '';
  (j.cells||[]).forEach((v,i)=>{
    const div = document.createElement('div');
    div.className = 'tile';
    const cls = (v!=null && (v<3.0 || v>3.7)) ? 'bad' : 'good';
    div.innerHTML = `<div class="small">C${i+1}</div><div class="${cls}">${v==null?'--':v.toFixed(3)} V</div>`;
    cells.appendChild(div);
  });
  const temps = document.getElementById('temps');
  temps.innerHTML = '';
  (j.temps||[]).forEach((t,i)=>{
    const div = document.createElement('div');
    div.className = 'tile';
    const cls = (t!=null && (t<-10 || t>55)) ? 'bad' : 'good';
    div.innerHTML = `<div class="small">T${i+1}</div><div class="${cls}">${t==null?'--':t.toFixed(2)} °C</div>`;
    temps.appendChild(div);
  });
}
setInterval(fetchData, 1000);
fetchData();
</script>
</body>
</html>
)HTML";

void setupTWAI(){
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_PIN, TWAI_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
  ESP_ERROR_CHECK(twai_start());
}

void setupWiFi(){
#if WIFI_MODE_AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  Serial.printf("AP SSID: %s  IP: %s\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
  Serial.printf("Connecting to %s", WIFI_STA_SSID);
  int tries=0;
  while (WiFi.status()!=WL_CONNECTED && tries<60){ delay(500); Serial.print('.'); tries++; }
  Serial.println();
  if (WiFi.status()==WL_CONNECTED) {
    Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi failed, falling back to AP");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("AP SSID: %s  IP: %s\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
  }
#endif
}

void handleRoot(){
  server.setContentLength(strlen_P(INDEX_HTML));
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApi(){
  String json;
  bms.toJson(json);
  server.send(200, "application/json", json);
}

void setup(){
  Serial.begin(115200);
  while(!Serial){}
  Serial.println("ESS_LFP_48S WebServer");
  setupTWAI();
  setupWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api", HTTP_GET, handleApi);
  server.begin();
}

void loop(){
  twai_message_t msg;
  while (twai_receive(&msg, pdMS_TO_TICKS(1)) == ESP_OK) {
    if (!msg.rtr && msg.extd) {
      bms.updateFromFrame(msg.identifier, msg.data_length_code, msg.data);
    }
  }
  server.handleClient();
}
