#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SD.h>

// Definitions for the Dallas sensor
#define TEMP_SENSOR_PIN 4
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature temperatureSensors(&oneWire);

// Network settings
const char* wifiSSID = "Idkyet";
const char* wifiPassword = "euz38zqe";

// Web server setup
AsyncWebServer webServer(80);
AsyncWebSocket webSocket("/ws");

// JSON object for storing sensor data
JSONVar sensorData;

// Timing for sensor data updates
unsigned long previousMillis = 0;
unsigned long updateInterval = 3000;

// Initialize temperature sensor
void setupTemperatureSensor() {
  pinMode(TEMP_SENSOR_PIN, INPUT_PULLUP);
  temperatureSensors.begin();
}

// Fetch temperature and time, return as JSON string
String fetchSensorData() {
  temperatureSensors.requestTemperatures(); 
  sensorData["temp"] = String(temperatureSensors.getTempCByIndex(0));
  sensorData["time"] = String(millis());
  return JSON.stringify(sensorData) + "\n";
}

// Broadcast readings to all connected WebSocket clients
void broadcastReadings(String data) {
  webSocket.textAll(data.c_str());
}

// WebSocket message handling
void processWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *frame = (AwsFrameInfo*)arg;
  if (frame->final && frame->index == 0 && frame->len == len && frame->opcode == WS_TEXT) {
    String message(reinterpret_cast<char*>(data), len);
    if (message == "getReadings") {
      String readings = fetchSensorData();
      Serial.print(readings);
      broadcastReadings(readings);
    }
  }
}

// WebSocket events
void handleWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("Client %u connected\n", client->id());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("Client %u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      processWebSocketMessage(arg, data, len);
      break;
    default:
      break;
  }
}

// Setup WebSocket
void setupWebSocket() {
  webSocket.onEvent(handleWebSocketEvent);
  webServer.addHandler(&webSocket);
}

// Initialize file system
void setupFileSystem() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount file system");
    return;
  }
  Serial.println("File system mounted");
}

// Connect to WiFi
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPassword);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  setupTemperatureSensor();
  connectToWiFi();
  setupFileSystem();
  setupWebSocket();

  // Set up web server routing
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });
  webServer.serveStatic("/", SPIFFS, "/");

  webServer.begin();
}

void loop() {
  if ((millis() - previousMillis) > updateInterval) {
    String sensorData = fetchSensorData();
    Serial.print(sensorData);
    broadcastReadings(sensorData);
    previousMillis = millis();
  }

  webSocket.cleanupClients();
}
