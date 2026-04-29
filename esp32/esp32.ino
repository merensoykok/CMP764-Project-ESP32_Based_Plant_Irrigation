#include "DHT.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"

#define DHT_PIN 27
#define DHTTYPE DHT11
#define LDR_PIN 36
#define SOIL_PIN 39
#define LED_PIN 25

const char* ssid = "local-wifi"; //"A52-telefon";
const char* password = "eren12345";

String serverUrl = "http://0.0.0.0:5000/fetch";

DHT dht(DHT_PIN, DHTTYPE);


void wifi_on() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  digitalWrite(LED_PIN, HIGH);
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


};


void loop() {
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
  Serial.println("********************");

  wifi_on();

  Serial.println("Data is transferred...");
  send_data(temp, hum, fotores, soil);
  Serial.println("Data Transfer Success!");
  
  delay(10000);
  wifi_off();

};