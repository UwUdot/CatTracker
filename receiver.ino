#include <SPI.h>
#include <cc1101.h>
#include <AESLib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

AESLib aesLib;
Preferences preferences;

byte aes_key[] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 
                   0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35 };
    
using namespace CC1101;
const int CS_PIN = 1; 
Radio radio(CS_PIN);

float latest_lat = 0.0;
float latest_lon = 0.0;
uint8_t latest_battery = 0;
int8_t latest_rssi = 0;
bool data_received = false;
unsigned long last_packet_millis = 0;

String home_ssid = "";
String home_pass = "";
bool portal_submitted = false;

WebServer server(80);

void handleSetupRoot() {
  String html = R"rawliteral(<!DOCTYPE html><html><head><title>WiFi Setup</title>
<style>body{font-family:sans-serif;text-align:center;background:#121212;color:#fff;padding-top:50px;}
input{padding:10px;margin:10px;width:220px;border-radius:5px;border:none;}
button{padding:10px 20px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;}</style></head>
<body><h2>CC1101 Secure Wi-Fi Setup</h2>
<form action='/' method='POST'>
<input type='text' name='ssid' placeholder='Home Wi-Fi SSID' maxlength='32' required><br>
<input type='password' name='pass' placeholder='Home Wi-Fi Password' maxlength='64'><br>
<button type='submit'>Connect</button>
</form></body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleSetupPost() {
  if (server.hasArg("ssid")) {
    home_ssid = server.arg("ssid");
    home_pass = server.hasArg("pass") ? server.arg("pass") : "";

    if (home_ssid.length() > 32) home_ssid = home_ssid.substring(0, 32);
    if (home_pass.length() > 64) home_pass = home_pass.substring(0, 64);

    preferences.begin("wifi-config", false);
    preferences.putString("ssid", home_ssid);
    preferences.putString("pass", home_pass);
    preferences.end();

    portal_submitted = true;

    server.sendHeader("Location", "/done", true);
    server.send(303);
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleDoneGet() {
  String html = R"rawliteral(<!DOCTYPE html><html><head><title>Setup Complete</title>
<style>
body{font-family:sans-serif;text-align:center;background:#121212;color:#4CAF50;padding-top:50px;}
p{color:#ccc;line-height:1.6;}
a.btn{display:inline-block;padding:12px 25px;background:#4CAF50;color:white;text-decoration:none;border-radius:5px;margin-top:20px;font-weight:bold;}
a.btn.disabled{background:#333;color:#777;cursor:not-allowed;pointer-events:none;}
</style>
</head>
<body>
<h2>Credentials Saved Successfully!</h2>
<p>The ESP32 is switching over to your home network now.</p>
<hr style='border:0;border-top:1px solid #333;margin:20px auto;width:80%'>
<p><b>Next Steps:</b><br>
1. Disconnect your device from <b>CC1101-Setup</b> and reconnect to your <b>Home Wi-Fi</b>.<br>
2. Please wait while the ESP32 restarts... (<span id='timer' style='color:#ff9800;font-weight:bold;'>20</span>s)</p>
<a href='http://cc1101.local' id='dashBtn' class='btn disabled'>Open Live Dashboard</a>

<script>
let timeLeft = 20;
const timerEl = document.getElementById('timer');
const btnEl = document.getElementById('dashBtn');

let countdown = setInterval(() => {
  timeLeft--;
  timerEl.innerText = timeLeft;
  if (timeLeft <= 0) {
    clearInterval(countdown);
    timerEl.parentElement.innerHTML = '2. Click the button below once reconnected:';
    btnEl.classList.remove('disabled');
  }
}, 1000);
</script>
</body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleDashboardRoot() {
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<title>CC1101 Live Telemetry</title>
<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>
<style>
body{font-family:sans-serif;text-align:center;background:#121212;color:#fff;margin:0;padding:20px;}
.card{background:#1e1e1e;padding:15px;border-radius:10px;max-width:500px;margin:auto;box-shadow:0 4px 10px rgba(0,0,0,0.5);}
h2{color:#4CAF50;margin-top:0;}
#map{width:100%;height:300px;border-radius:8px;margin-top:15px;z-index:1;}
.status-box{background:#2a2a2a;padding:10px;border-radius:6px;margin-bottom:15px;font-size:14px;}
</style>
</head>
<body>
<div class='card'>
<h2>Live Tracker Dashboard</h2>
<div class='status-box'><b>Status:</b> <span id='sys-status' style='color:#ff9800;'>Connecting...</span></div>
<p><b>Latitude:</b> <span id='lat'>Waiting...</span></p>
<p><b>Longitude:</b> <span id='lon'>Waiting...</span></p>
<p><b>Battery:</b> <span id='batt'>Waiting...</span>%</p>
<p><b>Signal:</b> <span id='sig'>Waiting...</span></p>
<div id='map'></div>
<p style='font-size:11px;color:#888;margin-top:15px;'>Last tracker signal: <span id='update-age'>Waiting for packet...</span></p>
</div>
<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>
<script>
let map, marker;
let lastUpdateSeconds = 0;
let hasReceivedOnce = false;
let consecutiveErrors = 0;

function initMap(lat, lon) {
  map = L.map('map').setView([lat, lon], 15);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom: 19, attribution: '&copy; OpenStreetMap'}).addTo(map);
  marker = L.marker([lat, lon]).addTo(map).bindPopup('Tracker Location').openPopup();
}

function getSignalQualityText(rssi) {
  if (rssi >= -75) return rssi + " dBm (Strong 🟢)";
  if (rssi >= -90) return rssi + " dBm (Good 🟡)";
  return rssi + " dBm (Weak / Out of Range 🟠)";
}

async function fetchData() {
  try {
    let res = await fetch('/data');
    if (!res.ok) throw new Error('Server error');
    let data = await res.json();
    consecutiveErrors = 0;
    
    if (hasReceivedOnce && lastUpdateSeconds > 80) {
      document.getElementById('sys-status').innerHTML = "<span style='color:#ff9800;'>⚠️ Tracker Offline (Out of range or dead battery)</span>";
    } else {
      document.getElementById('sys-status').innerHTML = "<span style='color:#4CAF50;'>🟢 Connected</span>";
    }

    if (data.received) {
      hasReceivedOnce = true;
      lastUpdateSeconds = data.age;
      document.getElementById('lat').innerText = data.lat.toFixed(6);
      document.getElementById('lon').innerText = data.lon.toFixed(6);
      document.getElementById('batt').innerText = data.battery;
      document.getElementById('sig').innerHTML = getSignalQualityText(data.rssi);
      
      if (!map) {
        initMap(data.lat, data.lon);
      } else {
        let newLatLng = [data.lat, data.lon];
        marker.setLatLng(newLatLng);
        map.setView(newLatLng);
      }
    }
  } catch(e) {
    consecutiveErrors++;
    if (consecutiveErrors >= 2) {
      document.getElementById('sys-status').innerHTML = "<span style='color:#f44336;'>❌ Home Hub Offline (Check power or Wi-Fi)</span>";
    }
  }
}

setInterval(() => {
  if (hasReceivedOnce) {
    document.getElementById('update-age').innerText = lastUpdateSeconds + ' seconds ago';
    lastUpdateSeconds++;
  }
}, 1000);

setInterval(fetchData, 3000);
</script>
</body>
</html>)rawliteral";

  server.send(200, "text/html", html);
}


void handleDashboardData() {
  unsigned long age = data_received ? (millis() - last_packet_millis) / 1000 : 0;
  String json = "{";
  json += "\"received\":" + String(data_received ? "true" : "false") + ",";
  json += "\"lat\":" + String(latest_lat, 6) + ",";
  json += "\"lon\":" + String(latest_lon, 6) + ",";
  json += "\"battery\":" + String(latest_battery) + ",";
  json += "\"rssi\":" + String(latest_rssi) + ",";
  json += "\"age\":" + String(age);
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  SPI.begin(3, 4, 2, 1);
  if (radio.begin() == STATUS_CHIP_NOT_FOUND) {
    Serial.println("CC1101 chip not found!");
    while (true);
  }
  radio.setModulation(MOD_ASK_OOK);
  radio.setFrequency(433.92);
  radio.setPacketLengthMode(PKT_LEN_MODE_VARIABLE);
  aesLib.set_paddingmode((paddingMode)0);

  preferences.begin("wifi-config", true); 
  home_ssid = preferences.getString("ssid", "");
  home_pass = preferences.getString("pass", "");
  preferences.end();

  if (home_ssid.length() == 0) {
    Serial.println("\nNo saved credentials found. Launching Setup AP...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP("CC1101-Setup", "cc1101secure");
    
    server.on("/", HTTP_GET, handleSetupRoot);
    server.on("/", HTTP_POST, handleSetupPost);
    server.on("/done", HTTP_GET, handleDoneGet);
    
    if (MDNS.begin("cc1101")) {
      Serial.println("mDNS active! Access portal at: http://cc1101.local (or http://192.168.4.1)");
    }
    server.begin();

    while (!portal_submitted) {
      server.handleClient();
      delay(2);
    }

    unsigned long startWait = millis();
    while (millis() - startWait < 600) {
      server.handleClient();
      delay(2);
    }
    
    preferences.begin("wifi-config", true);
    home_ssid = preferences.getString("ssid", "");
    home_pass = preferences.getString("pass", "");
    preferences.end();

    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(home_ssid.c_str(), home_pass.c_str());
  
  Serial.print("Connecting to router: ");
  Serial.println(home_ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nSuccessfully Connected to Home Wi-Fi!");
    Serial.print("Assigned IP: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, handleDashboardRoot);
    server.on("/data", HTTP_GET, handleDashboardData);
    server.begin();

    if (MDNS.begin("cc1101")) {
      Serial.println("mDNS live! Access dashboard at: http://cc1101.local");
    }
  } else {
    Serial.println("\nConnection failed! Clearing bad credentials and restarting setup portal...");
    
    preferences.begin("wifi-config", false);
    preferences.clear();
    preferences.end();

    ESP.restart();
  }

  Serial.println("Receiver Listening for RF Packets...");
}

void loop() {
  server.handleClient();

  unsigned char receivedPacket[64];
  unsigned int bytesRead = 0;

  Status status = radio.receive(receivedPacket, sizeof(receivedPacket), &bytesRead);

  if (status == STATUS_OK && bytesRead > 16) {
    byte extracted_iv[16];
    memcpy(extracted_iv, receivedPacket, 16);

    uint16_t cipherLen = bytesRead - 16;
    aesLib.decrypt(receivedPacket + 16, cipherLen, receivedPacket + 16, aes_key, 128, extracted_iv);
    
    int32_t lat_int, lon_int;
    memcpy(&lat_int, receivedPacket + 16, sizeof(lat_int));
    memcpy(&lon_int, receivedPacket + 16 + 4, sizeof(lon_int));
    latest_battery = receivedPacket[16 + 8];

    latest_lat = (float)lat_int / 1e6;
    latest_lon = (float)lon_int / 1e6;
    latest_rssi = radio.getRSSI();
    data_received = true;
    last_packet_millis = millis();

    Serial.print("Success! Coordinates -> ");
    Serial.print(latest_lat, 6);
    Serial.print(", ");
    Serial.print(latest_lon, 6);
    Serial.print(" | Battery -> ");
    Serial.print(latest_battery);
    Serial.println("%");
  }
}