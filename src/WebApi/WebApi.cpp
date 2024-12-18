#include "WebAPI.h"
#include "credentials.h"
#include <WiFi.h>

const int WebAPI::port = 80;

WebAPI::WebAPI(ScaleManager *manager) : server(port) {
  this->sManager = manager;
}

void WebAPI::begin() {
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected to WiFi. IP: ");
  Serial.println(WiFi.localIP());

  setupRoutes();
  server.begin();
}

void WebAPI::setupRoutes() {
  server.on("/tare", HTTP_GET, [this]() {
    sManager->tare();
    server.send(200, "text/plain", "Tare command received");
  });

  server.on("/start-timer", HTTP_GET, [this]() {
    sManager->startTimer();
    server.send(200, "text/plain", "Timer start command received");
  });

  server.on("/stop-timer", HTTP_GET, [this]() {
    sManager->stopTimer();
    server.send(200, "text/plain", "Timer stop command received");
  });

  server.on("/reset-timer", HTTP_GET, [this]() {
    sManager->resetTimer();
    server.send(200, "text/plain", "Timer reset command received");
  });

  server.on("/start-and-tare", HTTP_GET, [this]() {
    sManager->startAndTare();
    server.send(200, "text/plain", "Start timer and tare command received");
  });

  server.on("/weight", HTTP_GET, [this]() {
    float weight = sManager->getWeight();
    String response = String(weight, 2);
    server.send(200, "text/plain", response);
  });

  server.onNotFound([this]() { server.send(404, "text/plain", "Not found"); });
}

void WebAPI::handleClient() { server.handleClient(); }
