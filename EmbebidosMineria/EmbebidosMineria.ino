#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "time.h"
#include <Wire.h>
#include <BH1750.h>
#include "DHT.h"

// ====== CONFIG WI-FI =======
const char* ssid = "Terraza";
const char* password = "Lopez123";

// ====== GOOGLE SHEETS ======
const char* host = "script.google.com";
const int httpsPort = 443;
const String urlPath = "/macros/s/AKfycbz5cDb9GjbvKODeTu36dUHqzClk5jQLyTB2wUZFLTiOOwUY6PcJcwb13kCL6NQZIwRR/exec";

// ====== NTP (Bolivia UTC-4) ======
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -4 * 3600;
const int daylightOffset_sec = 0;

// ====== SENSORES ======
// Lluvia
const int lluviaPin = 34;

// Luz
BH1750 lightMeter;

// Viento (Hall)
const int hallPin = 27;
volatile unsigned long pulsos = 0;
unsigned long ultimoTiempo = 0;

// DHT11
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ====== CONTAR PULSOS DEL HALL ======
void IRAM_ATTR contarRevolucion() {
  pulsos++;
}

// ====== Obtener timestamp ======
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "0";

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void setup() {
  Serial.begin(115200);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");

  // NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Esperar hora NTP
  struct tm timeinfo;
  while(!getLocalTime(&timeinfo)) {
    Serial.println("Esperando hora NTP...");
    delay(500);
  }

  // Lluvia
  pinMode(lluviaPin, INPUT);

  // Luz
  Wire.begin(21, 22);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  // Viento
  pinMode(hallPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(hallPin), contarRevolucion, FALLING);

  // DHT
  dht.begin();
}

void loop() {
  // ====== LLUVIA ======
  int lluviaRaw = analogRead(lluviaPin);  // 0–4095

  // ====== LUZ ======
  float lux = lightMeter.readLightLevel();

  // ====== VIENTO ======
  unsigned long ahora = millis();
  float viento_kmh = 0;

  if (ahora - ultimoTiempo >= 1000) {
    detachInterrupt(digitalPinToInterrupt(hallPin));

    unsigned long p = pulsos;
    pulsos = 0;

    attachInterrupt(digitalPinToInterrupt(hallPin), contarRevolucion, FALLING);
    ultimoTiempo = ahora;

    if (p > 0) {
      float rpm = p * 60.0;
      viento_kmh = rpm * 0.00942;  // calibración por diámetro 5 cm
    }
  }

  // ====== TEMPERATURA / HUMEDAD ======
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    h = 0;
    t = 0;
  }

  // ====== TIMESTAMP ======
  String timestamp = getTimestamp();

  // ====== ENVIAR A GOOGLE SHEETS ======
WiFiClientSecure client;
client.setInsecure();   // evitar problemas de certificado TLS

if (!client.connect(host, httpsPort)) {
  Serial.println("❌ Error conectando a Google");
} else {

  // Codificar timestamp (espacios → %20)
  String tsEncoded = timestamp;
  tsEncoded.replace(" ", "%20");

  String url = urlPath +
               "?ts=" + tsEncoded +
               "&lluvia=" + String(lluviaRaw) +
               "&lux=" + String(lux) +
               "&viento=" + String(viento_kmh, 2) +
               "&temp=" + String(t) +
               "&humedad=" + String(h);

  Serial.println("📤 URL enviada:");
  Serial.println(url);

  // Solicitud HTTP correcta
  client.println("GET " + url + " HTTP/1.1");
  client.println("Host: script.google.com");
  client.println("User-Agent: ESP32");
  client.println("Connection: close");
  client.println();

  // Leer respuesta
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
    Serial.println(line);
  }
}

client.stop();


  // ====== MONITOR SERIAL ======
  Serial.println("-----");
  Serial.println(timestamp);
  Serial.println("LLUVIA_RAW: " + String(lluviaRaw));
  Serial.println("LUX: " + String(lux));
  Serial.println("VIENTO_KMH: " + String(viento_kmh, 2));
  Serial.println("TEMP_C: " + String(t));
  Serial.println("HUMEDAD: " + String(h));

  delay(10000); // 10 segundos entre envíos
}
