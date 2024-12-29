// WebApi.cpp
#include "WebApi.h"
#include "credentials.h"

WebAPI::WebAPI() : server(80), ws("/ws"), lastWebSocketUpdate(0) {}

void WebAPI::setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("autobru-esp32");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected to WiFi. IP: ");
  Serial.println(WiFi.localIP());
}

void WebAPI::setupWebSocket() {
  ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("WebSocket client #%u connected\n", client->id());
    }
  });
  server.addHandler(&ws);
}

void WebAPI::setupRoutes() {
  server.on("/tare", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (sManager->isConnected())
      sManager->tare();
    request->send(200, "text/plain", "Tare command received");
  });

  server.on("/start-timer", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (sManager->isConnected())
      sManager->startTimer();
    request->send(200, "text/plain", "Timer start command received");
  });

  server.on("/stop-timer", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (sManager->isConnected())
      sManager->stopTimer();
    request->send(200, "text/plain", "Timer stop command received");
  });

  server.on("/reset-timer", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (sManager->isConnected())
      sManager->resetTimer();
    request->send(200, "text/plain", "Timer reset command received");
  });

  server.on("/start-and-tare", HTTP_GET,
            [this](AsyncWebServerRequest *request) {
              sManager->startAndTare();
              request->send(200, "text/plain",
                            "Tare and start timer command received");
            });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <title>AutoBru Controller</title>
            <style>
                body { font-family: Arial, sans-serif; margin: 20px; }
                button { margin: 5px; padding: 10px; }
            </style>
        </head>
        <body>
            <h1>Weight: <span id="weight">0.00</span>g</h1>
            <h1>Time: <span id="time">0</span>s</h1>
            <h1>Flow Rate: <span id="flow-rate">0</span>g/s</h1>
            <button onclick="fetch('/tare')">Tare</button>
            <button onclick="fetch('/start-timer')">Start Timer</button>
            <button onclick="fetch('/stop-timer')">Stop Timer</button>
            <button onclick="fetch('/reset-timer')">Reset Timer</button>
            <button onclick="fetch('/start-and-tare')">Start & Tare</button>
            
            <script>
                let ws = new WebSocket('ws://' + window.location.hostname + '/ws');
                ws.onmessage = function(event) {
                    const data = JSON.parse(event.data);
                    document.getElementById('weight').textContent = data.weight;
                    document.getElementById('time').textContent = (data.time / 1000).toFixed(1);
                    document.getElementById('flow-rate').textContent = data.flowRate;
                };
            </script>
        </body>
        </html>
    )rawliteral");
  });
}

void WebAPI::begin() {
  setupWiFi();
  setupWebSocket();
  setupRoutes();
  server.begin();
}

void WebAPI::update() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastWebSocketUpdate >= WEBSOCKET_INTERVAL) {
    lastWebSocketUpdate = currentMillis;
    if (ws.count() > 0 && sManager->isConnected()) {
      String jsonData = "{\"weight\":";
      jsonData += String(sManager->getWeight(), 1);
      jsonData += ",\"time\":";
      jsonData += String(sManager->getTime());
      jsonData += ",\"flowRate\":";
      jsonData += String(sManager->getFlowRate(), 1);
      jsonData += "}";
      ws.textAll(jsonData);
    }
  }
}