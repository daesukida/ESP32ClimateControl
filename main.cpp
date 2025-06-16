#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_now.h>

// Wi-Fi credentials (só para WebServer, ESP-NOW usa modo STA)
const char* ssid = "Galaxy S23 FE 3B1E";
const char* password = "morrqdesgraca";

// Pin definitions
const int lm35Pin = 15;    // LM35 output pin
const int buzzerPin = 25;  // Buzzer pin

// Web server on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// State variables
float desiredTemp = 22.0;  // Initial desired temperature
bool acState = false;      // AC state (on/off)
int connectedClients = 0;  // Number of WebSocket clients
String receivedBody;       // Accumulate request body

// MAC Address do ESP32 receptor (alterar conforme seu receptor)
uint8_t peerAddress[] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF}; // << TROQUE PELO MAC REAL >>

// === Função para ler temperatura do LM35 ===
float readLM35Temperature() {
  int analogValue = analogRead(lm35Pin);
  float voltage = analogValue * (3.3 / 4095.0);
  float temperature = voltage * 100.0;
  if (temperature < -40.0 || temperature > 125.0) {
    Serial.printf("Invalid temperature reading: %.2f°C\n", temperature);
    return desiredTemp;
  }
  return temperature;
}
//

// === Função para enviar dados via ESP-NOW ===
void sendESPNowMessage(const char* message) {
  Serial.print("Sending ESP-NOW message: ");
  Serial.println(message);
  esp_err_t result = esp_now_send(peerAddress, (uint8_t*)message, strlen(message) + 1);
  if (result == ESP_OK) {
    Serial.println("ESP-NOW send success");
  } else {
    Serial.print("ESP-NOW send error: ");
    Serial.println(result);
  }
  digitalWrite(buzzerPin, HIGH);
  delay(200);
  digitalWrite(buzzerPin, LOW);
}

// === Enviar estado para todos os clientes WebSocket ===
void notifyClients() {
  StaticJsonDocument<200> doc;
  doc["currentTemp"] = readLM35Temperature();
  doc["desiredTemp"] = desiredTemp;
  doc["acState"] = acState;
  String response;
  serializeJson(doc, response);
  Serial.print("Notifying clients with: ");
  Serial.println(response);
  ws.textAll(response);
}

// === Eventos do WebSocket ===
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

// === Log de requisições HTTP ===
void logRequest(AsyncWebServerRequest *request) {
  Serial.printf("Request: %s %s from %s\n", request->methodToString(), request->url().c_str(), request->client()->remoteIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // === Inicializar LittleFS ===
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  // === Inicializar WiFi (station mode para ESP-NOW) ===
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  // === Inicializar ESP-NOW ===
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(peerAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Erro ao adicionar peer");
      return;
    }
  }
  Serial.println("ESP-NOW configurado");

  // === Inicializar WebSocket ===
  ws.onEvent(handleWebSocketEvent);
  server.addHandler(&ws);

  // === Servir arquivos estáticos ===
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setFilter([](AsyncWebServerRequest *request) {
    logRequest(request);
    return true;
  });

  // === Endpoint /data ===
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request);
    StaticJsonDocument<200> doc;
    doc["currentTemp"] = readLM35Temperature();
    doc["desiredTemp"] = desiredTemp;
    doc["acState"] = acState;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // === Endpoint /desiredTemp ===
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
          char tempStr[10];
          snprintf(tempStr, sizeof(tempStr), "%.0f", desiredTemp);
          sendESPNowMessage(tempStr);
          notifyClients();
          request->send(200, "application/json", "{}");
        } else {
          Serial.print("Temperature out of range: ");
          Serial.println(newTemp);
          request->send(400, "application/json", "{\"error\":\"Temperature out of range\"}");
        }
      }
    });

  // === Endpoint /acToggle ===
  server.on("/acToggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    logRequest(request);
    acState = !acState;
    Serial.print("Toggled AC state to: ");
    Serial.println(acState ? "On" : "Off");
    sendESPNowMessage(acState ? "11" : "01");
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
    float temp = readLM35Temperature();
    Serial.println("=== System Status ===");
    Serial.printf("Current Temp: %.2f°C\n", temp);
    Serial.printf("Desired Temp: %.2f°C\n", desiredTemp);
    Serial.printf("AC State: %s\n", acState ? "On" : "Off");
    Serial.printf("Connected Clients: %d\n", connectedClients);
    Serial.println("====================");
    lastUpdate = millis();
  }
}
