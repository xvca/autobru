// WebApi.cpp
#include "WebApi.h"
#include "credentials.h"

WebAPI *WebAPI::instance = nullptr;

WebAPI::WebAPI() : server(80), ws("/ws"), lastWebSocketUpdate(0) {}

void WebAPI::setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("autobru-esp32");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }
}

void WebAPI::setupWebSocket() {
  ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
      break;
    case WS_EVT_DISCONNECT:
      break;
    case WS_EVT_ERROR:
      break;
    }
  });

  server.addHandler(&ws);
}

void WebAPI::setupRoutes() {
  // server.on("/tare", HTTP_GET, [this](AsyncWebServerRequest *request) {
  //   if (!sManager) {
  //     request->send(400, "text/plain", "Scale manager not initialized");
  //     return;
  //   }
  //   if (!sManager->isConnected()) {
  //     request->send(400, "text/plain", "Scale not connected");
  //     return;
  //   }
  //   sManager->tare();
  //   request->send(200, "text/plain", "Tare command received");
  // });

  // server.on("/start-timer", HTTP_GET, [this](AsyncWebServerRequest *request)
  // {
  //   if (!sManager) {
  //     request->send(400, "text/plain", "Scale manager not initialized");
  //     return;
  //   }
  //   if (!sManager->isConnected()) {
  //     request->send(400, "text/plain", "Scale not connected");
  //     return;
  //   }
  //   sManager->startTimer();
  //   request->send(200, "text/plain", "Timer start command received");
  // });

  // server.on("/stop-timer", HTTP_GET, [this](AsyncWebServerRequest *request) {
  //   if (!sManager) {
  //     request->send(400, "text/plain", "Scale manager not initialized");
  //     return;
  //   }
  //   if (!sManager->isConnected()) {
  //     request->send(400, "text/plain", "Scale not connected");
  //     return;
  //   }
  //   sManager->stopTimer();
  //   request->send(200, "text/plain", "Timer stop command received");
  // });

  // server.on("/reset-timer", HTTP_GET, [this](AsyncWebServerRequest *request)
  // {
  //   if (!sManager) {
  //     request->send(400, "text/plain", "Scale manager not initialized");
  //     return;
  //   }
  //   if (!sManager->isConnected()) {
  //     request->send(400, "text/plain", "Scale not connected");
  //     return;
  //   }
  //   sManager->resetTimer();
  //   request->send(200, "text/plain", "Timer reset command received");
  // });

  // server.on(
  //     "/start-and-tare", HTTP_GET, [this](AsyncWebServerRequest *request) {
  //       if (!sManager) {
  //         request->send(400, "text/plain", "Scale manager not initialized");
  //         return;
  //       }
  //       if (!sManager->isConnected()) {
  //         request->send(400, "text/plain", "Scale not connected");
  //         return;
  //       }
  //       sManager->startAndTare();
  //       request->send(200, "text/plain",
  //                     "Tare and start timer command received");
  //     });

  server.on("/start-brew", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!sManager) {
      request->send(400, "text/plain", "Scale manager not initialized");
      return;
    }
    if (!bManager) {
      request->send(400, "text/plain", "Brew manager not initialized");
      return;
    }
    if (!sManager->isConnected()) {
      request->send(400, "text/plain", "Scale not connected");
      return;
    }

    if (!request->hasParam("weight")) {
      request->send(400, "text/plain", "Missing target weight parameter");
      return;
    }

    float targetWeight = request->getParam("weight")->value().toFloat();
    if (targetWeight <= 0 || targetWeight > 100) {
      request->send(400, "text/plain",
                    "Invalid target weight (must be between 0-100g)");
      return;
    }

    if (!bManager->startBrew(targetWeight, true)) {
      request->send(500, "text/plain", "Failed to start brew");
      return;
    }

    request->send(200, "text/plain",
                  "Brew started with target: " + String(targetWeight) + "g");
  });

  server.on("/stop-brew", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!sManager) {
      request->send(400, "text/plain", "Scale manager not initialized");
      return;
    }
    if (!bManager) {
      request->send(400, "text/plain", "Brew manager not initialized");
      return;
    }

    bManager->stopBrew();

    request->send(200, "text/plain", "Brew Cancelled");
    return;
  });

  server.on("/clear-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!bManager) {
      request->send(400, "text/plain", "Brew manager not initialized");
      return;
    }

    bManager->clearShotData();

    request->send(200, "text/plain", "Cleared all shot data");
    return;
  });
}

void WebAPI::begin() {
  setupWiFi();
  setupWebSocket();
  setupRoutes();
  updateServer.setup(&server);
  server.begin();

  sManager = ScaleManager::getInstance();
  bManager = BrewManager::getInstance();
}

void WebAPI::update() {
  if (millis() - lastWebSocketUpdate >= WEBSOCKET_INTERVAL) {
    broadcastBrewMetrics();
    lastWebSocketUpdate = millis();
  }
}

void WebAPI::broadcastBrewMetrics() {
  if (!ws.count() || !sManager || !sManager->isConnected() || !bManager) {
    return;
  }

  BrewMetrics metrics{.weight = sManager->getWeight(),
                      .flowRate = sManager->getFlowRate(),
                      .time = sManager->getTime(),
                      .state = bManager->getState()};

  ws.textAll(serializeBrewMetrics(metrics));
}

String WebAPI::serializeBrewMetrics(const BrewMetrics &metrics) {
  return String("{") + "\"weight\":" + String(metrics.weight, 1) + "," +
         "\"flowRate\":" + String(metrics.flowRate, 1) + "," +
         "\"time\":" + String(metrics.time) + "," +
         "\"state\":" + String(metrics.state) + "}";
}