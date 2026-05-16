#include "DHT.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include "LittleFS.h"
#include "WebServer.h"

#define DHT_PIN 27
#define DHTTYPE DHT11
#define LDR_PIN 36
#define SOIL_PIN 39
#define LED_PIN 25

// Experiment scenario (change per run, then compare with: python analyze.py)
// 1 = Wi-Fi always on + cloud decision
// 2 = Wi-Fi always on + edge (tiny) decision
// 3 = Wi-Fi duty-cycle + cloud decision
// 4 = Wi-Fi duty-cycle + edge (tiny) decision
#define SCENARIO 4

WebServer server(80);

const unsigned long READ_INTERVAL           = 5000;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;
const unsigned long WIFI_WARN_INTERVAL      = 60000;
const unsigned long WIFI_WARN_DURATION      = 5000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

const char* CSV_FILE_PATH = "/sensor_data.csv";
const char* CSV_TMP_PATH  = "/sensor_data.tmp";
const int   ANALOG_SAMPLES = 8;

const uint8_t FS_TRIM_THRESHOLD_PERCENT = 90;
const uint8_t FS_TRIM_REMOVE_PERCENT    = 20;

const char* ssid = "A52-telefon";
const char* password = "eren12345";

String serverUrl = "http://192.168.43.81:8741/fetch";

DHT dht(DHT_PIN, DHTTYPE);

unsigned long lastWifiReconnectTime = 0;
unsigned long lastWifiWarnTime      = 0;
unsigned long wifiSessionStartMs    = 0;
unsigned long wifiOnAccumMs         = 0;
bool wifiWasConnected               = false;

float lastTemp    = 0;
float lastHum     = 0;
float lastLight   = 0;
float lastSoil    = 0;
unsigned long lastReadMs = 0;
unsigned int soilSaturatedCount = 0;

String lastDecision       = "—";
String lastDecisionSource = "—";
float lastEdgeInferenceMs = 0;
float lastHttpRttMs       = 0;

// ── Scenario helpers ──────────────────────────────────────────────────

bool uses_cloud_decision() {
  return SCENARIO == 1 || SCENARIO == 3;
}

bool uses_edge_decision() {
  return SCENARIO == 2 || SCENARIO == 4;
}

bool wifi_duty_cycle() {
  return SCENARIO == 3 || SCENARIO == 4;
}

// Mirror listener/plant_model.py thresholds
String classify_plant_edge(float temp, float hum, float light, float soil) {
  if (isnan(temp) || isnan(soil)) return "unknown";
  if (soil >= 3500) return "needs_water";
  if (soil <= 1500) return "overwatered";
  if (temp > 35 || temp < 10) return "temperature_stress";
  if (!isnan(hum) && hum < 30) return "low_humidity";
  if (!isnan(light) && light < 500) return "low_light";
  return "healthy";
}

float run_edge_inference(float temp, float hum, float light, float soil, String& outLabel) {
  unsigned long t0 = micros();
  outLabel = classify_plant_edge(temp, hum, light, soil);
  return (micros() - t0) / 1000.0f;
}

// ── LED ─────────────────────────────────────────────────────────────

void led_blink(int n, int onMs, int offMs) {
  for (int i = 0; i < n; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(LED_PIN, LOW);
    if (i < n - 1) delay(offMs);
  }
}

void led_on_for(unsigned long durMs) {
  digitalWrite(LED_PIN, HIGH);
  delay(durMs);
  digitalWrite(LED_PIN, LOW);
}

int read_analog_avg(uint8_t pin, int samples = ANALOG_SAMPLES) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(2);
  }
  return (int)(total / samples);
}

// ── Wi-Fi session accounting ──────────────────────────────────────────

void wifi_track_session_start() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiSessionStartMs = millis();
  }
}

void wifi_track_session_end() {
  if (wifiSessionStartMs > 0 && WiFi.status() == WL_CONNECTED) {
    wifiOnAccumMs += millis() - wifiSessionStartMs;
  }
  wifiSessionStartMs = 0;
}

unsigned long wifi_on_ms_snapshot() {
  unsigned long total = wifiOnAccumMs;
  if (wifiSessionStartMs > 0 && WiFi.status() == WL_CONNECTED) {
    total += millis() - wifiSessionStartMs;
  }
  return total;
}

bool wifi_connect_blocking() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiSessionStartMs == 0) wifiSessionStartMs = millis();
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    wifiSessionStartMs = millis();
    return true;
  }

  Serial.println("WiFi connect timeout");
  return false;
}

void wifi_on() {
  wifi_connect_blocking();
}

void wifi_off() {
  wifi_track_session_end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disconnected (duty-cycle)");
  digitalWrite(LED_PIN, LOW);
}

void wifi_ensure_for_upload() {
  if (!wifi_duty_cycle()) return;
  wifi_connect_blocking();
}

void wifi_release_after_upload() {
  if (!wifi_duty_cycle()) return;
  wifi_track_session_end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("WiFi released after upload (duty-cycle)");
}

// ── CSV & web (unchanged core) ───────────────────────────────────────

void handle_download() {
  File file = LittleFS.open(CSV_FILE_PATH, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "CSV file not found");
    return;
  }
  server.streamFile(file, "text/csv");
  file.close();
}

void handle_root() {
  String html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  html += "<title>ESP32 Sensor</title><style>";
  html += "body{font-family:system-ui,sans-serif;margin:1.5rem;background:#0f1419;color:#e7ecf3;}";
  html += "h1{font-size:1.25rem;} .card{background:#1a2332;padding:1rem;border-radius:8px;margin:1rem 0;}";
  html += "a{color:#3d9eff;} .muted{color:#8b9cb3;}</style></head><body>";
  html += "<h1>ESP32 Smart Plant Node</h1>";
  html += "<p class=\"muted\">Scenario: " + String(SCENARIO) + " &mdash; Wi-Fi: ";
  html += (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "Disconnected";
  html += " &mdash; IP: ";
  html += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
  html += "</p><div class=\"card\"><p><strong>Last decision</strong>: ";
  html += lastDecision + " (" + lastDecisionSource + ")</p>";
  html += "<p>Edge infer: " + String(lastEdgeInferenceMs, 2) + " ms &mdash; HTTP RTT: ";
  html += String(lastHttpRttMs, 2) + " ms</p>";
  html += "<p><strong>Last reading</strong> (" + String(lastReadMs) + " ms)</p><ul>";
  html += "<li>Temperature: " + String(lastTemp) + " C</li>";
  html += "<li>Humidity: " + String(lastHum) + " %</li>";
  html += "<li>Light (LDR): " + String(lastLight) + "</li>";
  html += "<li>Soil: " + String(lastSoil) + "</li>";
  html += "</ul></div>";
  html += "<p><a href=\"/download\">Download CSV</a></p>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void start_web_server() {
  server.on("/", handle_root);
  server.on("/download", handle_download);
  server.begin();
  Serial.println("Web server started.");
}

void init_csv() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS initialization failed!");
    return;
  }
  if (!LittleFS.exists(CSV_FILE_PATH)) {
    File file = LittleFS.open(CSV_FILE_PATH, FILE_WRITE);
    if (!file) return;
    file.println("time_ms,scenario,temp,humidity,light,soil,decision,decision_source,edge_ms,rtt_ms,wifi_on_ms");
    file.close();
  }
}

uint8_t fs_usage_percent() {
  size_t total = LittleFS.totalBytes();
  if (total == 0) return 0;
  return (uint8_t)((LittleFS.usedBytes() * 100) / total);
}

bool trim_csv() {
  File in = LittleFS.open(CSV_FILE_PATH, FILE_READ);
  if (!in) return false;
  int totalLines = 0;
  while (in.available()) {
    if (in.read() == '\n') totalLines++;
  }
  in.close();
  if (totalLines <= 1) return false;

  int dataLines = totalLines - 1;
  int linesToRemove = (dataLines * FS_TRIM_REMOVE_PERCENT) / 100;
  if (linesToRemove < 1) linesToRemove = 1;

  in = LittleFS.open(CSV_FILE_PATH, FILE_READ);
  File out = LittleFS.open(CSV_TMP_PATH, FILE_WRITE);
  if (!in || !out) {
    if (in) in.close();
    if (out) out.close();
    LittleFS.remove(CSV_TMP_PATH);
    return false;
  }

  int lineNum = 0;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    if (lineNum == 0) out.println(line);
    else if (lineNum > linesToRemove) out.println(line);
    lineNum++;
  }
  in.close();
  out.close();
  LittleFS.remove(CSV_FILE_PATH);
  if (!LittleFS.rename(CSV_TMP_PATH, CSV_FILE_PATH)) {
    LittleFS.remove(CSV_TMP_PATH);
    return false;
  }
  return true;
}

void maybe_trim_csv() {
  if (fs_usage_percent() >= FS_TRIM_THRESHOLD_PERCENT) trim_csv();
}

void write_csv(float temp, float hum, float light, float soil, const String& decision, const String& src,
               float edgeMs, float rttMs) {
  maybe_trim_csv();
  File file = LittleFS.open(CSV_FILE_PATH, FILE_APPEND);
  if (!file) return;
  file.print(millis()); file.print(",");
  file.print(SCENARIO); file.print(",");
  file.print(temp); file.print(",");
  file.print(hum); file.print(",");
  file.print(light); file.print(",");
  file.print(soil); file.print(",");
  file.print(decision); file.print(",");
  file.print(src); file.print(",");
  file.print(edgeMs, 2); file.print(",");
  file.print(rttMs, 2); file.print(",");
  file.println(wifi_on_ms_snapshot());
  file.close();
}

// ── Upload with metrics ───────────────────────────────────────────────

bool send_metrics_followup(int readingId, float rttMs, int bytesSent, int bytesReceived, float endToEndMs) {
  if (readingId <= 0) return false;

  String metricsUrl = serverUrl;
  metricsUrl.replace("/fetch", "/fetch/metrics");

  StaticJsonDocument<256> doc;
  doc["reading_id"] = readingId;
  doc["http_rtt_ms"] = rttMs;
  doc["bytes_sent"] = bytesSent;
  doc["bytes_received"] = bytesReceived;
  doc["end_to_end_ms"] = endToEndMs;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(metricsUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  int code = http.POST(payload);
  http.end();
  return code > 0;
}

bool send_data(float temp, float hum, float light, float soil,
               const String& edgeDecision, float edgeInferenceMs,
               unsigned long cycleStartMs) {
  wifi_ensure_for_upload();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi unavailable, skip upload");
    return false;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);

  StaticJsonDocument<512> doc;
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["light"] = light;
  doc["soil"] = soil;
  doc["device_ms"] = millis();
  doc["scenario"] = SCENARIO;
  doc["edge_inference_ms"] = edgeInferenceMs;
  if (uses_edge_decision()) {
    doc["edge_decision"] = edgeDecision;
  }
  doc["wifi_on_ms"] = wifi_on_ms_snapshot();

  String payload;
  serializeJson(doc, payload);
  int bytesSent = payload.length();

  unsigned long httpStart = millis();
  int code = http.POST(payload);
  lastHttpRttMs = millis() - httpStart;

  int bytesReceived = 0;
  int readingId = 0;
  String cloudDecision = edgeDecision;
  String decisionSource = uses_edge_decision() ? "edge" : "cloud";

  if (code > 0) {
    String response = http.getString();
    bytesReceived = response.length();
    Serial.print("RESPONSE: ");
    Serial.println(response);

    StaticJsonDocument<384> resp;
    DeserializationError err = deserializeJson(resp, response);
    if (!err) {
      if (resp.containsKey("decision")) {
        cloudDecision = resp["decision"].as<String>();
      }
      if (resp.containsKey("decision_source")) {
        decisionSource = resp["decision_source"].as<String>();
      }
      if (resp.containsKey("id")) {
        readingId = resp["id"].as<int>();
      }
    }
  } else {
    Serial.printf("HTTP error: %s\n", http.errorToString(code).c_str());
  }

  http.end();

  float endToEndMs = millis() - cycleStartMs;
  lastDecision = cloudDecision;
  lastDecisionSource = decisionSource;
  lastEdgeInferenceMs = edgeInferenceMs;

  if (readingId > 0) {
    send_metrics_followup(readingId, lastHttpRttMs, bytesSent, bytesReceived, endToEndMs);
  }

  Serial.printf("Metrics scenario=%d edge_ms=%.2f rtt_ms=%.2f wifi_on_ms=%lu bytes=%d e2e_ms=%.0f\n",
                SCENARIO, edgeInferenceMs, lastHttpRttMs, wifi_on_ms_snapshot(), bytesSent, endToEndMs);

  wifi_release_after_upload();
  return code > 0;
}

// ── Setup & loop ──────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.printf("Smart Plant Monitor — SCENARIO %d\n", SCENARIO);

  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  init_csv();

  if (!wifi_duty_cycle()) {
    wifi_on();
    wifiWasConnected = true;
    led_blink(5, 200, 150);
  } else {
    Serial.println("Duty-cycle Wi-Fi: radio off until upload window");
    WiFi.mode(WIFI_STA);
  }

  start_web_server();
}

void loop() {
  server.handleClient();

  if (!wifi_duty_cycle()) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiWasConnected) {
        wifiWasConnected = true;
        led_blink(5, 200, 150);
        wifi_track_session_start();
      }
    } else {
      wifiWasConnected = false;
      unsigned long now = millis();
      if (now - lastWifiReconnectTime >= WIFI_RECONNECT_INTERVAL) {
        lastWifiReconnectTime = now;
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
      }
      if (now - lastWifiWarnTime >= WIFI_WARN_INTERVAL) {
        lastWifiWarnTime = now;
        led_on_for(WIFI_WARN_DURATION);
      }
    }
  }

  unsigned long cycleStart = millis();

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  float light = read_analog_avg(LDR_PIN);
  float soil = read_analog_avg(SOIL_PIN);

  if (soil >= 4090) {
    soilSaturatedCount++;
    if (soilSaturatedCount % 20 == 0) {
      Serial.println("WARNING: Soil ADC saturated near 4095.");
    }
  } else {
    soilSaturatedCount = 0;
  }

  lastTemp = temp;
  lastHum = hum;
  lastLight = light;
  lastSoil = soil;
  lastReadMs = millis();

  String edgeDecision = "unknown";
  float edgeMs = 0;
  if (uses_edge_decision()) {
    edgeMs = run_edge_inference(temp, hum, light, soil, edgeDecision);
    lastDecision = edgeDecision;
    lastDecisionSource = "edge";
  } else {
    edgeDecision = "";
    edgeMs = 0;
  }

  Serial.printf("S%d Temp=%.1f Hum=%.1f Light=%.0f Soil=%.0f\n",
                SCENARIO, temp, hum, light, soil);

  bool uploaded = send_data(temp, hum, light, soil, edgeDecision, edgeMs, cycleStart);

  if (!uploaded && uses_cloud_decision()) {
    lastDecision = "offline";
    lastDecisionSource = "none";
  }

  write_csv(temp, hum, light, soil, lastDecision, lastDecisionSource, edgeMs, lastHttpRttMs);

  led_blink(1, 300, 0);
  delay(READ_INTERVAL);
}
