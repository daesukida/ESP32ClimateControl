#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// Wi-Fi credentials
const char* ssid = "Casa 44 OI Fibra 2G";
const char* password = "J626100452";

// Pin definitions
const int buzzerPin = 25; // Buzzer pin
#define DHTPIN 15         // DHT22 data pin
#define DHTTYPE DHT22     // DHT22 sensor type

// Web server on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// State variables
float desiredTemp = 22.0;  // Initial desired temperature
bool acState = false;      // AC state (on/off)
int connectedClients = 0;  // Number of WebSocket clients
String receivedBody;       // Accumulate request body

DHT_Unified dht(DHTPIN, DHTTYPE);

// Read temperature from DHT22
float readDHT22Temperature() {
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println(F("Error reading temperature!"));
    return desiredTemp; // Fallback to desired temperature
  }
  return event.temperature;
}

// Read humidity from DHT22
float readDHT22Humidity() {
  sensors_event_t event;
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    Serial.println(F("Error reading humidity!"));
    return -1; // Indicate error
  }
  return event.relative_humidity;
}

// Notify all WebSocket clients
void notifyClients() {
  StaticJsonDocument<200> doc;
  doc["currentTemp"] = readDHT22Temperature();
  doc["currentHumidity"] = readDHT22Humidity();
  doc["desiredTemp"] = desiredTemp;
  doc["acState"] = acState;
  String response;
  serializeJson(doc, response);
  Serial.print("Notifying clients with: ");
  Serial.println(response);
  ws.textAll(response);
}

// WebSocket event handler
void handleWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    connectedClients++;
    Serial.printf("Client connected: %u. Total clients: %d\n", client->id(), connectedClients);
    notifyClients();
  } else if (type == WS_EVT_DISCONNECT) {
    connectedClients--;
    Serial.printf("Client disconnected: %u. Total clients: %d\n", client->id(), connectedClients);
  }
}

// Log HTTP requests
void logRequest(AsyncWebServerRequest *request) {
  Serial.printf("Request: %s %s from %s\n", request->methodToString(), request->url().c_str(), request->client()->remoteIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Initialize DHT22
  dht.begin();

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  // Setup WebSocket
  ws.onEvent(handleWebSocketEvent);
  server.addHandler(&ws);

  // Serve static files
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setFilter([](AsyncWebServerRequest *request) {
    logRequest(request);
    return true;
  });

  // /data endpoint
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request);
    StaticJsonDocument<200> doc;
    doc["currentTemp"] = readDHT22Temperature();
    doc["currentHumidity"] = readDHT22Humidity();
    doc["desiredTemp"] = desiredTemp;
    doc["acState"] = acState;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // /desiredTemp endpoint
  server.on(
    "/desiredTemp", HTTP_POST, [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      logRequest(request);
      receivedBody += String((char*)data, len);
      if (index + len == total) {
        Serial.print("Received desiredTemp request: ");
        Serial.println(receivedBody);
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, receivedBody);
        receivedBody = "";
        if (error) {
          Serial.print("JSON parse error: ");
          Serial.println(error.c_str());
          request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
        }
        if (!doc.containsKey("desiredTemp")) {
          Serial.println("Missing desiredTemp key");
          request->send(400, "application/json", "{\"error\":\"Missing desiredTemp\"}");
          return;
        }
        float newTemp = doc["desiredTemp"].as<float>();
        if (newTemp >= 17.0 && newTemp <= 30.0) {
          desiredTemp = newTemp;
          Serial.print("Updated desiredTemp to: ");
          Serial.println(desiredTemp);
          digitalWrite(buzzerPin, HIGH);
          delay(200);
          digitalWrite(buzzerPin, LOW);
          notifyClients();
          request->send(200, "application/json", "{}");
        } else {
          Serial.print("Temperature out of range: ");
          Serial.println(newTemp);
          request->send(400, "application/json", "{\"error\":\"Temperature out of range\"}");
        }
      }
    });

  // /acToggle endpoint
  server.on("/acToggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    logRequest(request);
    acState = !acState;
    Serial.print("Toggled AC state to: ");
    Serial.println(acState ? "On" : "Off");
    digitalWrite(buzzerPin, HIGH);
    delay(200);
    digitalWrite(buzzerPin, LOW);
    notifyClients();
    StaticJsonDocument<200> doc;
    doc["acState"] = acState;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.begin();
}

void loop() {
  ws.cleanupClients();

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 15000) {
    float temp = readDHT22Temperature();
    float hum = readDHT22Humidity();
    Serial.println("=== System Status ===");
    Serial.printf("Current Temp: %.2f°C\n", temp);
    Serial.printf("Current Humidity: %.2f%%\n", hum);
    Serial.printf("Desired Temp: %.2f°C\n", desiredTemp);
    Serial.printf("AC State: %s\n", acState ? "On" : "Off");
    Serial.printf("Connected Clients: %d\n", connectedClients);
    Serial.println("====================");
    lastUpdate = millis();
  }
}