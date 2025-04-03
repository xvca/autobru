// WebApi.cpp
#include "WebApi.h"
#include "credentials.h"
#include <cstdint>

WebAPI *WebAPI::instance = nullptr;

WebAPI::WebAPI() : server(80), ws("/ws"), lastWebSocketUpdate(0) {}

void WebAPI::setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("autobru");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }

  DEBUG_PRINTF("Connected to WiFi, IP: %s\n", WiFi.localIP().toString());
}

void WebAPI::setupWebSocket() {
  ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
      if (ws.count() > MAX_WS_CLIENTS) {
        ws.cleanupClients(MAX_WS_CLIENTS);
      }
      DEBUG_PRINTF("WebSocket client #%u connected from %s\n", client->id(),
                   client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      DEBUG_PRINTF("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      if (len == 4 && strncmp((char *)data, "ping", 4) == 0) {
        DEBUG_PRINTF("Ping received from client #%u\n", client->id());
        client->text("pong");
        DEBUG_PRINTF("Pong sent to client #%u\n", client->id());
      }
      break;

    case WS_EVT_ERROR:
      DEBUG_PRINTF("WebSocket client #%u error(%u): %s\n", client->id(),
                   *((uint16_t *)arg), (char *)data);
      break;
    }
  });

  server.addHandler(&ws);
}

void WebAPI::setupRoutes() {
  auto handleError = [](AsyncWebServerRequest *request, int code,
                        const char *message) {
    AsyncWebServerResponse *response = request->beginResponse(
        code, "application/json", "{\"error\": \"" + String(message) + "\"}");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  };

  server.on(
      "/start", HTTP_POST,
      [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return false;
        }

        if (!bManager->isEnabled()) {
          handleError(
              request, 400,
              "Brew control is currently disabled. Please enable in settings");
          return false;
        }

        if (!request->hasParam("weight", true)) {
          handleError(request, 400, "Missing target weight parameter");
          return false;
        }

        float targetWeight =
            request->getParam("weight", true)->value().toFloat();
        if (targetWeight <= 0 || targetWeight > 100) {
          handleError(request, 400,
                      "Invalid target weight (must be between 0-100g)");
          return false;
        }

        if (!bManager->startBrew(targetWeight)) {
          if (bManager->isBrewing()) {
            handleError(request, 409, "A brew is already running");
            return false;
          } else if (!bManager->isEnabled()) {
            handleError(request, 403, "Brewing is currently disabled");
            return false;
          }
          handleError(request, 500, "Failed to start brew");
          return false;
        }

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json",
            "{\"message\": \"Brew started\", \"target\": " +
                String(targetWeight) + "}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);

        return true;
      });

  server.on(
      "/stop", HTTP_POST, [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return false;
        }

        if (!bManager->isEnabled()) {
          handleError(
              request, 400,
              "Brew control is currently disabled. Please enable in settings");
          return false;
        }

        bManager->stopBrew();

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json", "{\"message\": \"Brew stopped\"}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);

        return true;
      });

  server.on(
      "/clear-data", HTTP_POST,
      [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return false;
        }

        if (!bManager->isEnabled()) {
          handleError(
              request, 400,
              "Brew control is currently disabled. Please enable in settings");
          return false;
        }

        bManager->clearShotData();

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json", "{\"message\": \"Shot data cleared\"}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);

        return true;
      });

  server.on(
      "/clear-shot", HTTP_POST,
      [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return false;
        }

        if (!bManager->isEnabled()) {
          handleError(
              request, 400,
              "Brew control is currently disabled. Please enable in settings");
          return false;
        }

        if (!request->hasParam("index", true)) {
          handleError(request, 400, "Missing target weight parameter");
          return false;
        }

        int index = request->getParam("index", true)->value().toInt();
        if (index < 0 || index > MAX_STORED_SHOTS) {
          handleError(request, 400,
                      "Invalid shot index (must be between 0-" +
                          MAX_STORED_SHOTS);
          return false;
        }

        bManager->clearSingleShotData(index);

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json",
            "{\"message\": \"Deleted shot\", \"index\": " + String(index) +
                "}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);

        return true;
      });

  server.on(
      "/recalc-comp-factor", HTTP_POST,
      [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return false;
        }

        if (!bManager->isEnabled()) {
          handleError(
              request, 400,
              "Brew control is currently disabled. Please enable in settings");
          return false;
        }

        bManager->recalculateCompFactor();

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json",
            "{\"message\": \"new flow comp factor\", \"\": " +
                String(bManager->getFlowCompFactor()) + "}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);

        return true;
      });

  server.on("/wake", HTTP_POST,
            [this, &handleError](AsyncWebServerRequest *request) {
              if (!bManager->isActive()) {
                bManager->wake();
              } else {
                handleError(request, 400, "Brew manager already active");
                return;
              }

              AsyncWebServerResponse *response = request->beginResponse(
                  200, "application/json", "{\"message\": \"Waking ESP\"}");
              response->addHeader("Access-Control-Allow-Origin", "*");
              request->send(response);

              return;
            });

  server.on(
      "/prefs", HTTP_POST,
      [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return;
        }

        // Print all received parameters
        if (!request->hasParam("isEnabled", true) ||
            !request->hasParam("preset1", true) ||
            !request->hasParam("preset2", true) ||
            !request->hasParam("pMode", true)) {
          handleError(request, 400, "Missing required parameters");
          return;
        }

        BrewPrefs prefs;

        prefs.isEnabled =
            request->getParam("isEnabled", true)->value().equals("true");

        prefs.preset1 = request->getParam("preset1", true)->value().toFloat();

        prefs.preset2 = request->getParam("preset2", true)->value().toFloat();

        prefs.pMode =
            PreinfusionMode(request->getParam("pMode", true)->value().toInt());

        bManager->setPrefs(prefs);

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json", "{\"message\": \"Preferences updated\"}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
      });

  server.on(
      "/prefs", HTTP_GET, [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return;
        }

        BrewPrefs prefs = bManager->getPrefs();
        String response =
            "{\"isEnabled\":" + String(prefs.isEnabled ? "true" : "false") +
            ",\"preset1\":" + String(prefs.preset1) +
            ",\"preset2\":" + String(prefs.preset2) +
            ",\"pMode\":" + String(prefs.pMode) + "}";

        AsyncWebServerResponse *resp =
            request->beginResponse(200, "application/json", response);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        request->send(resp);
      });

  server.on("/data", HTTP_GET,
            [this, &handleError](AsyncWebServerRequest *request) {
              if (!bManager) {
                handleError(request, 400, "Brew manager not initialized");
                return;
              }

              const Shot *shots = bManager->getRecentShots();
              float flowComp = bManager->getFlowCompFactor();

              String response = "{\"shots\":[";
              for (int i = 0; i < MAX_STORED_SHOTS; i++) {
                if (i > 0)
                  response += ",";
                response +=
                    "{\"targetWeight\":" + String(shots[i].targetWeight) +
                    ",\"finalWeight\":" + String(shots[i].finalWeight) +
                    ",\"lastFlowRate\":" + String(shots[i].lastFlowRate) + "}";
              }
              response += "],\"flowCompFactor\":" + String(flowComp) + "}";

              AsyncWebServerResponse *resp =
                  request->beginResponse(200, "application/json", response);
              resp->addHeader("Access-Control-Allow-Origin", "*");
              request->send(resp);
            });

  // Add OPTIONS handlers for CORS
  server.on("/start", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(204);
    response->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
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
  if (millis() - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }

  if (millis() - lastWebSocketUpdate >= WEBSOCKET_INTERVAL) {
    broadcastBrewMetrics();
    lastWebSocketUpdate = millis();
  }
}

void WebAPI::checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTF("WiFi disconnected, attempting reconnection...\n");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Wait up to 10 seconds for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(1000);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTF("Reconnected to WiFi, IP: %s\n", WiFi.localIP().toString());
    }
  }
}

void WebAPI::broadcastBrewMetrics() {
  if (!ws.count() || !sManager || !sManager->isConnected() || !bManager) {
    return;
  }

  BrewMetrics metrics = {.weight = sManager->getWeight(),
                         .flowRate = sManager->getFlowRate(),
                         .targetWeight = bManager->getTargetWeight(),
                         .time = sManager->getTime(),
                         .state = bManager->getState()};

  for (AsyncWebSocketClient &c : ws.getClients()) {
    if (c.canSend() && c.queueLen() < 5) {
      c.binary((uint8_t *)&metrics, sizeof(BrewMetrics));
    }
  }
}
