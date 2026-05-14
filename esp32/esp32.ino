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


WebServer server(80);

const unsigned long READ_INTERVAL = 1000; // 1 seconds
const char* CSV_FILE_PATH = "/sensor_data.csv";

const char* ssid = "A52-telefon"; //"local-wifi"; //"A52-telefon";
const char* password = "eren12345";

String serverUrl = "http://0.0.0.0:5000/fetch";

DHT dht(DHT_PIN, DHTTYPE);


void handle_download() {
  File file = LittleFS.open(CSV_FILE_PATH, FILE_READ);

  if (!file) {
    server.send(404, "text/plain", "CSV dosyasi bulunamadi");
    return;
  }

  server.streamFile(file, "text/csv");
  file.close();
}

void start_web_server() {
  server.on("/", []() {
    server.send(200, "text/plain", "ESP32 CSV Server. CSV indirmek icin /download adresine git.");
  });

  server.on("/download", handle_download);

  server.begin();
  Serial.println("Web server baslatildi.");
}


void init_csv() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS baslatilamadi!");
    return;
  }

  Serial.println("LittleFS baslatildi.");

  if (!LittleFS.exists(CSV_FILE_PATH)) {
    File file = LittleFS.open(CSV_FILE_PATH, FILE_WRITE);

    if (!file) {
      Serial.println("CSV dosyasi olusturulamadi!");
      return;
    }

    file.println("time_ms,temp,humidity,light,soil");
    file.close();

    Serial.println("CSV dosyasi olusturuldu ve baslik eklendi.");
  } else {
    Serial.println("CSV dosyasi zaten mevcut.");
  }
};

void write_csv(float temp, float hum, float fotores, float soil) {
  File file = LittleFS.open(CSV_FILE_PATH, FILE_APPEND);

  if (!file) {
    Serial.println("CSV dosyasi acilamadi!");
    return;
  }

  file.print(millis());
  file.print(",");
  file.print(temp);
  file.print(",");
  file.print(hum);
  file.print(",");
  file.print(fotores);
  file.print(",");
  file.println(soil);

  file.close();

  Serial.println("Veri CSV dosyasina eklendi.");
};


void wifi_on() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

    Serial.println();
    Serial.println("WiFi connected");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
};

void wifi_off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disctonnected");
  digitalWrite(LED_PIN, LOW);
};

void send_data(float temp, float hum, float fotores, float soil) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Sunucu adresini belirtin
    http.begin(serverUrl); 
    http.addHeader("Content-Type", "application/json");

    // 1. JSON Verisini Oluşturma (ArduinoJson kullanarak)
    StaticJsonDocument<200> doc;
    doc["temp"] = temp;
    doc["hum"] = hum;
    doc["light"] = fotores;
    doc["soil"] = soil;

    String jsonResponse;
    serializeJson(doc, jsonResponse);

    // 2. Veriyi Gönder
    int code = http.POST(jsonResponse);
    Serial.println("DATA GONDERILDI");
    Serial.println(code);

    // 3. Yanıtı Al ve İşle
    if (code > 0) {
      String response = http.getString();
      Serial.print("RESPONSE: ");
      Serial.println(response);
    }

    http.end();
  } else {
    Serial.println("Wi-Fi Bağlantısı Kesik!");
  }
};

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  init_csv();
  wifi_on();
  start_web_server(); 


};

// scenario1 = wifi always on + cloudML
// scenario2 = wifi always on + tinyML
// scenario3 = wifi on/off + cloudML
// scenario4 = wifi on/off + tinyML


void loop() {
  server.handleClient();

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  float fotores = analogRead(LDR_PIN);
  float soil = analogRead(SOIL_PIN);

  
  Serial.print("Temp: ");
  Serial.println(temp);

  Serial.print("Humidity: ");
  Serial.println(hum);

  Serial.print("Foto: ");
  Serial.println(fotores);

  Serial.print("Soil: ");
  Serial.println(soil);

  write_csv(temp, hum, fotores, soil);


  Serial.println("********************");


  //wifi_on();

  //Serial.println("Data is transferred...");
  //send_data(temp, hum, fotores, soil);
  //Serial.println("Data Transfer Success!");
  
  //wifi_off();
  

  delay(READ_INTERVAL);

};