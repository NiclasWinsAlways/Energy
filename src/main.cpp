/**
 * @file main.cpp
 * @brief Main application code for the ESP32-based weather monitoring system.
 * This file includes all necessary dependencies and the setup and loop logic.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SD.h>
#include <ESPmDNS.h>
#include <time.h>

#define TEMP_SENSOR_PIN 4 /**< GPIO pin number for the temperature sensor. */
#define SD_CS_PIN 5 /**< GPIO pin number for the SD card Chip Select. */

OneWire oneWire(TEMP_SENSOR_PIN); /**< OneWire protocol instance for communication with temperature sensors. */
DallasTemperature temperatureSensors(&oneWire); /**< Instance to control Dallas Temperature sensors. */

AsyncWebServer server(80); /**< Instance of the web server running on port 80. */
AsyncWebSocket webSocket("/ws"); /**< Web socket server running on path /ws. */
JSONVar sensorData; /**< JSON variable to store sensor data. */
unsigned long previousMillis = 0; /**< Store the last time readings were sent. */
const unsigned long updateInterval = 3000; /**< Interval to send data in milliseconds. */

const char* ssidPath = "/ssid.txt"; /**< Path to SSID storage on SPIFFS. */
const char* passPath = "/pass.txt"; /**< Path to password storage on SPIFFS. */
const char* ntpServer = "pool.ntp.org"; /**< NTP server used for time synchronization. */
const long gmtOffset_sec = 3600; /**< GMT offset in seconds. */
const int daylightOffset_sec = 3600; /**< Daylight saving time offset in seconds. */

/**
 * Function prototypes to declare ahead of time.
 */
void handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
void handleWebSocketData(AsyncWebSocketClient* client, void* arg, uint8_t* data, size_t len);
String readFile(fs::FS& fs, const char* path);
bool connectToWiFi();
void setupTemperatureSensor();
void setupFileSystem();
void setupWebSocket();
void broadcastReadings(String data);
String fetchSensorData();
void setupSDCard();
void logDataToSD(String data);
void startAccessPoint();
void printLocalTime();
void syncTime();

/**
 * @brief Set up the temperature sensor.
 */
void setupTemperatureSensor() {
    pinMode(TEMP_SENSOR_PIN, INPUT_PULLUP);
    temperatureSensors.begin();
}

/**
 * @brief Fetch sensor data and format it as a JSON string.
 * @return String formatted as JSON.
 */
String fetchSensorData() {
    temperatureSensors.requestTemperatures();
    sensorData["temp"] = String(temperatureSensors.getTempCByIndex(0));

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        sensorData["time"] = "N/A";
    } else {
        char timeStringBuff[50];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%FT%T%z", &timeinfo);
        sensorData["time"] = String(timeStringBuff);
    }

    return JSON.stringify(sensorData) + "\n";
}

/**
 * @brief Broadcast readings to all connected WebSocket clients and log to SD card.
 * @param data String data to be sent and logged.
 */
void broadcastReadings(String data) {
    webSocket.textAll(data.c_str());
    logDataToSD(data);
}

/**
 * @brief Handle events on the WebSocket connection.
 * @param server WebSocket server instance.
 * @param client WebSocket client instance.
 * @param type Type of WebSocket event.
 * @param arg Additional arguments.
 * @param data Data received from the WebSocket.
 * @param len Length of the data received.
 */
void handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketData(client, arg, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

/**
 * @brief Handle data received from a WebSocket client.
 * @param client WebSocket client instance.
 * @param arg Additional arguments.
 * @param data Data received from the WebSocket.
 * @param len Length of the data received.
 */
void handleWebSocketData(AsyncWebSocketClient* client, void* arg, uint8_t* data, size_t len) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0; // Null-terminate the data
        String msg = (char*)data;
        Serial.printf("WebSocket message received: %s\n", msg.c_str());
        if (msg == "getReadings") {
            String sensorData = fetchSensorData();
            client->text(sensorData);
        }
    }
}

/**
 * @brief Set up the WebSocket communication.
 */
void setupWebSocket() {
    webSocket.onEvent(handleWebSocketEvent);
    server.addHandler(&webSocket);
}

/**
 * @brief Set up the file system for data storage.
 */
void setupFileSystem() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount file system");
        return;
    }
    Serial.println("SPIFFS mounted successfully");
}

/**
 * @brief Set up the SD card for data logging.
 */
void setupSDCard() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD Card Mount Failed");
        return;
    }
    Serial.println("SD Card initialized.");

    if (!SD.exists("/data")) {
        SD.mkdir("/data");
    }
}

/**
 * @brief Log data to an SD card.
 * @param data String data to be logged.
 */
void logDataToSD(String data) {
    String filePath = "/data/sensorData.log";
    File file = SD.open(filePath, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open file on SD card for writing");
        return;
    }
    if (file.println(data)) {
        Serial.println("Data logged to SD");
    } else {
        Serial.println("Failed to write data to SD");
    }
    file.close();
}

/**
 * @brief Connect to WiFi using stored SSID and password.
 * @return true if connected successfully, false otherwise.
 */
bool connectToWiFi() {
    String ssid = readFile(SPIFFS, ssidPath);
    String pass = readFile(SPIFFS, passPath);

    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid.c_str(), pass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(1000);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected!");
        Serial.println("IP address: " + WiFi.localIP().toString());
        if (!MDNS.begin("esp32")) {
            Serial.println("Error setting up MDNS responder!");
        } else {
            Serial.println("MDNS responder started");
            MDNS.addService("http", "tcp", 80);
        }
        return true;
    } else {
        Serial.println("\nFailed to connect.");
        return false;
    }
}

/**
 * @brief Read a file from the file system.
 * @param fs File system to read from.
 * @param path Path to the file.
 * @return Content of the file as a string.
 */
String readFile(fs::FS& fs, const char* path) {
    Serial.printf("Reading file: %s\n", path);
    File file = fs.open(path, "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return String();
    }

    String fileContent;
    while (file.available()) {
        fileContent += char(file.read());
    }
    file.close();
    return fileContent;
}


/**
 * @brief Write a message to a file in the file system.
 * @param fs File system where the file is stored.
 * @param path Path to the file.
 * @param message Message to write.
 */
void writeFile(fs::FS& fs, const char* path, const char* message) {
    Serial.printf("Writing file: %s\n", path);
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    if (file.print(message)) {
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}


/**
 * @brief Starts an access point and configures the web server routes.
 *
 * This function starts a WiFi access point with predefined credentials and
 * sets up HTTP routes to serve HTML, CSS, and JavaScript files. It also handles
 * saving network settings received via POST requests.
 */
void startAccessPoint() {
    WiFi.softAP("ESP32-Setup-AP", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Serve the setup HTML page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/setup.html", "text/html");
    });

    // Serve the setup CSS file
    server.on("/setup.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/setup.css", "text/css");
    });

    // Serve the setup JavaScript file
    server.on("/setup.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/setup.js", "application/javascript");
    });

    // Handle POST requests to save WiFi settings and restart the ESP
    server.on("/setup_wifi", HTTP_POST, [](AsyncWebServerRequest* request) {
        int params = request->params();
        for (int i = 0; i < params; i++) {
            AsyncWebParameter* p = request->getParam(i);
            if (p->isPost()) {
                // Save the SSID or password based on the input name
                if (p->name() == "ssid") {
                    writeFile(SPIFFS, ssidPath, p->value().c_str());
                } else if (p->name() == "password") {
                    writeFile(SPIFFS, passPath, p->value().c_str());
                }
            }
        }
        request->send(200, "text/plain", "Network settings saved. Restarting...");
        delay(3000);
        ESP.restart();
    });

    server.begin();
}

/**
 * @brief Setup function that initializes the ESP32 device.
 *
 * This function initializes serial communication, sensors, filesystem, web server,
 * and attempts to connect to WiFi. If the connection fails, it starts an access point.
 */
void setup() {
    Serial.begin(115200);
    setupTemperatureSensor();
    setupFileSystem();
    setupWebSocket();
    setupSDCard();

    if (!connectToWiFi()) {
        startAccessPoint();
    } else {
        // NTP time synchronization
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        syncTime();
        printLocalTime();

        // Define server routes for the main application
        server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send(SPIFFS, "/index.html", "text/html");
        });
        server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send(SPIFFS, "/style.css", "text/css");
        });
        server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send(SPIFFS, "/script.js", "application/javascript");
        });
        server.on("/downloadcsv", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send(SD, "/data/sensorData.log", "text/csv", true);
        });
        server.on("/clearcsv", HTTP_GET, [](AsyncWebServerRequest* request) {
            SD.remove("/data/sensorData.log");
            request->send(200, "text/plain", "CSV data cleared.");
        });

        server.begin();
    }
}

/**
 * @brief Main loop function that continuously checks and handles WiFi connectivity and data broadcasting.
 *
 * This function checks if the device is connected to WiFi and sends sensor data at predefined intervals.
 */
void loop() {
    if (WiFi.status() == WL_CONNECTED && (millis() - previousMillis) > updateInterval) {
        String sensorData = fetchSensorData();
        Serial.print(sensorData);
        broadcastReadings(sensorData);
        previousMillis = millis();
    }
}

/**
 * @brief Prints the local time formatted as a human-readable string.
 *
 * This function retrieves the local time from the system and prints it.
 */
void printLocalTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

/**
 * @brief Synchronizes system time with NTP server.
 *
 * This function waits until the system time has been successfully synchronized with the NTP server.
 */
void syncTime() {
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
        Serial.println("Waiting for time synchronization...");
        delay(1000);
    }
    Serial.println("Time synchronized");
}
