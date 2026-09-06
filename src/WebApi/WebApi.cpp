#include "WebApi.h"
#include "credentials.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <cstdint>
#include <cmath>

WebAPI *WebAPI::instance = nullptr;

WebAPI::WebAPI() : server(80), ws("/ws"), lastWebSocketUpdate(0) {}

void WebAPI::setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("autobru");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    DEBUG_PRINTF("Attempting reconnect in 5...\n");
    delay(5000);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTF("Connected to WiFi, IP: %s\n", WiFi.localIP().toString());
    configTime(0, 0, "pool.ntp.org");
    bManager->syncTimezone();
  }
}

void WebAPI::setupWebSocket() {
  ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
      if (ws.count() > MAX_WS_CLIENTS) {
        DEBUG_PRINTF("count is %d max clients hit, cleaning up clients\n",
                     ws.count());
        ws.cleanupClients(MAX_WS_CLIENTS);
        DEBUG_PRINTF("count is %d after clearing\n", ws.count());
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
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                       "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                       "Content-Type, Authorization");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Private-Network",
                                       "true");

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      request->send(204);
    } else {
      request->send(404, "application/json", "{\"error\":\"Not Found\"}");
    }
  });

  auto handleError = [](AsyncWebServerRequest *request, int code,
                        const char *message) {
    request->send(code, "application/json",
                  "{\"error\": \"" + String(message) + "\"}");
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
        if (!bManager->isValidTargetWeight(targetWeight)) {
          String message = "Invalid target weight (must be > 0 and <= " +
                           String(bManager->getPrefs().maxShotWeight) + "g)";
          handleError(request, 400, message.c_str());
          return false;
        }

        if (!bManager->startBrew(targetWeight, true)) {
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

        bManager->abortBrew(true);

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json", "{\"message\": \"Brew stopped\"}");
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
        request->send(response);

        return true;
      });

  server.on("/wake", HTTP_POST,
            [this, &handleError](AsyncWebServerRequest *request) {
              if (!bManager->isEnabled()) {
                handleError(request, 400,
                            "Please enable your device in bru settings");
                return;
              } else if (!bManager->isActive()) {
                bManager->wake();
              } else {
                handleError(request, 400, "Already awake!");
                return;
              }

              AsyncWebServerResponse *response = request->beginResponse(
                  200, "application/json", "{\"message\": \"Waking ESP\"}");
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

        // Check for ALL required parameters
        if (!request->hasParam("isEnabled", true) ||
            !request->hasParam("regularPreset", true) ||
            !request->hasParam("decafPreset", true) ||
            !request->hasParam("pMode", true) ||
            !request->hasParam("decafStartHour", true) ||
            !request->hasParam("timezone", true) ||
            !request->hasParam("learningRate", true) ||
            !request->hasParam("systemLag", true) ||
            !request->hasParam("autoSavePreset", true) ||
            !request->hasParam("earlyStop", true) ||
            !request->hasParam("swapButtons", true) ||
            !request->hasParam("halfForTwoCup", true)) {
          handleError(request, 400, "Missing required parameters");
          return;
        }

        BrewPrefs prefs = bManager->getPrefs();
        if (request->hasParam("maxShotWeight", true)) {
          prefs.maxShotWeight =
              request->getParam("maxShotWeight", true)->value().toFloat();
        }
        if (!std::isfinite(prefs.maxShotWeight) || prefs.maxShotWeight < 1.0f ||
            prefs.maxShotWeight > MAX_CONFIGURABLE_SHOT_WEIGHT) {
          handleError(request, 400, "Maximum shot yield must be between 1-1000g");
          return;
        }
        if (bManager->isBrewing() &&
            prefs.maxShotWeight != bManager->getPrefs().maxShotWeight) {
          handleError(request, 400, "Cannot change maximum shot yield during a brew");
          return;
        }

        prefs.isEnabled =
            request->getParam("isEnabled", true)->value().equals("true");
        prefs.regularPreset =
            request->getParam("regularPreset", true)->value().toFloat();
        prefs.decafPreset =
            request->getParam("decafPreset", true)->value().toFloat();
        prefs.pMode =
            PreinfusionMode(request->getParam("pMode", true)->value().toInt());
        prefs.decafStartHour =
            request->getParam("decafStartHour", true)->value().toInt();
        prefs.timezone = request->getParam("timezone", true)->value();
        prefs.learningRate =
            request->getParam("learningRate", true)->value().toFloat();
        prefs.systemLag =
            request->getParam("systemLag", true)->value().toFloat();
        prefs.autoSavePreset =
            request->getParam("autoSavePreset", true)->value().equals("true");
        prefs.earlyStop =
            request->getParam("earlyStop", true)->value().equals("true");
        prefs.swapButtons =
            request->getParam("swapButtons", true)->value().equals("true");
        prefs.halfForTwoCup =
            request->getParam("halfForTwoCup", true)->value().equals("true");

        if (!std::isfinite(prefs.regularPreset) ||
            !std::isfinite(prefs.decafPreset) || prefs.regularPreset < 1.0f ||
            prefs.decafPreset < 1.0f || prefs.regularPreset > prefs.maxShotWeight ||
            prefs.decafPreset > prefs.maxShotWeight) {
          String message = "Preset weights must be between 1 and " +
                           String(prefs.maxShotWeight) + "g";
          handleError(request, 400, message.c_str());
          return;
        }

        if (prefs.learningRate < 0.0f || prefs.learningRate > 1.0) {
          handleError(request, 400, "Learning Rate must be 0 - 1");
          return;
        }

        if (prefs.systemLag < 0.0f || prefs.systemLag > 2.0f) {
          handleError(request, 400, "Lag must be 0 - 2");
          return;
        }

        bManager->setPrefs(prefs);

        bManager->syncTimezone();

        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json", "{\"message\": \"Preferences updated\"}");
        request->send(response);
      });

  server.on("/prefs", HTTP_GET,
            [this, &handleError](AsyncWebServerRequest *request) {
              if (!bManager) {
                handleError(request, 400, "Brew manager not initialized");
                return;
              }

              BrewPrefs prefs = bManager->getPrefs();

              JsonDocument doc;
              doc["isEnabled"] = prefs.isEnabled;
              doc["regularPreset"] = prefs.regularPreset;
              doc["decafPreset"] = prefs.decafPreset;
              doc["maxShotWeight"] = prefs.maxShotWeight;
              doc["pMode"] = prefs.pMode;
              doc["decafStartHour"] = prefs.decafStartHour;
              doc["timezone"] = prefs.timezone;
              doc["learningRate"] = prefs.learningRate;
              doc["systemLag"] = prefs.systemLag;
              doc["autoSavePreset"] = prefs.autoSavePreset;
              doc["earlyStop"] = prefs.earlyStop;
              doc["swapButtons"] = prefs.swapButtons;
              doc["halfForTwoCup"] = prefs.halfForTwoCup;

              bool loggingConfigured =
                  prefs.apiUrl.length() > 0 && prefs.apiToken.length() > 0;
              JsonObject autoLogging = doc["autoLogging"].to<JsonObject>();
              autoLogging["configured"] = loggingConfigured;
              autoLogging["apiUrl"] = loggingConfigured ? prefs.apiUrl : "";
              if (loggingConfigured && prefs.apiBarId > 0) {
                autoLogging["barId"] = prefs.apiBarId;
              } else {
                autoLogging["barId"] = nullptr;
              }

              String response;
              serializeJson(doc, response);

              AsyncWebServerResponse *resp =
                  request->beginResponse(200, "application/json", response);
              request->send(resp);
            });

  server.on(
      "/data", HTTP_GET, [this, &handleError](AsyncWebServerRequest *request) {
        if (!bManager) {
          handleError(request, 400, "Brew manager not initialized");
          return;
        }

        const Shot *shots0 = bManager->getRecentShots(0);
        const Shot *shots1 = bManager->getRecentShots(1);

        float factor0 = bManager->getFlowCompBias(0);
        float factor1 = bManager->getFlowCompBias(1);

        String response = "{";

        response += "\"p0\":{\"bias\":" + String(factor0) + ",\"shots\":[";
        for (int i = 0; i < MAX_HISTORY; i++) {
          if (shots0[i].id == 0)
            continue;

          if (i > 0 && shots0[i - 1].id != 0)
            response += ",";

          response += "{\"id\":" + String(shots0[i].id) +
                      ",\"targetWeight\":" + String(shots0[i].targetWeight) +
                      ",\"finalWeight\":" + String(shots0[i].finalWeight) +
                      ",\"lastFlowRate\":" + String(shots0[i].lastFlowRate) +
                      ",\"stopWeight\":" + String(shots0[i].stopWeight) +
                      ",\"drippage\":" +
                      String(shots0[i].finalWeight - shots0[i].stopWeight) +
                      "}";
        }
        response += "]},";

        response += "\"p1\":{\"bias\":" + String(factor1) + ",\"shots\":[";
        for (int i = 0; i < MAX_HISTORY; i++) {
          if (shots1[i].id == 0)
            continue;

          if (i > 0 && shots1[i - 1].id != 0)
            response += ",";

          response += "{\"id\":" + String(shots1[i].id) +
                      ",\"targetWeight\":" + String(shots1[i].targetWeight) +
                      ",\"finalWeight\":" + String(shots1[i].finalWeight) +
                      ",\"lastFlowRate\":" + String(shots1[i].lastFlowRate) +
                      ",\"stopWeight\":" + String(shots1[i].stopWeight) +
                      ",\"drippage\":" +
                      String(shots1[i].finalWeight - shots1[i].stopWeight) +
                      "}";
        }
        response += "]}";

        response += "}";

        AsyncWebServerResponse *resp =
            request->beginResponse(200, "application/json", response);
        request->send(resp);
      });

  server.on("/token", HTTP_POST,
            [this, &handleError](AsyncWebServerRequest *request) {
              if (!request->hasParam("apiUrl", true) ||
                  !request->hasParam("apiToken", true)) {
                handleError(request, 400, "Missing required parameters");
                return;
              }

              BrewPrefs prefs = bManager->getPrefs();
              prefs.apiUrl = request->getParam("apiUrl", true)->value();
              prefs.apiToken = request->getParam("apiToken", true)->value();
              prefs.apiBarId = 0;
              if (prefs.apiUrl.length() > 0 && prefs.apiToken.length() > 0 &&
                  request->hasParam("barId", true)) {
                String value = request->getParam("barId", true)->value();
                int barId = value.toInt();
                if (barId <= 0 || String(barId) != value) {
                  handleError(request, 400, "Invalid brew bar ID");
                  return;
                }
                prefs.apiBarId = barId;
              }

              bManager->setPrefs(prefs);

              AsyncWebServerResponse *response = request->beginResponse(
                  200, "application/json",
                  "{\"message\": \"Token configured successfully\"}");
              request->send(response);
            });
}

void WebAPI::begin() {
  sManager = ScaleManager::getInstance();
  bManager = BrewManager::getInstance();

  DEBUG_PRINTF("Entering wifi setup\n");
  setupWiFi();
  setupWebSocket();
  setupRoutes();
  updateServer.setup(&server);
  updateServer.onUpdateBegin = [](const UpdateType type, int &result) {};
  server.begin();
}

void WebAPI::update() {
  if (millis() - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }

  uint32_t currentInterval = bManager->isBrewing() ? 125 : 500;

  if (millis() - lastWebSocketUpdate >= currentInterval) {
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
  if (!ws.count() || !sManager || !bManager) {
    return;
  }

  bool scaleReady = sManager->isConnected();

  BrewMetrics metrics = {.weight = scaleReady ? sManager->getWeight() : 0.0f,
                         .flowRate =
                             scaleReady ? sManager->getFlowRate() : 0.0f,
                         .targetWeight = bManager->getTargetWeight(),
                         .time = scaleReady ? sManager->getTime() : 0,
                         .state = (uint8_t)bManager->getState(),
                         .isActive = bManager->isActive(),
                         .isScaleConnected = scaleReady};

  for (AsyncWebSocketClient &c : ws.getClients()) {
    if (c.canSend() && c.queueLen() < 5) {
      c.binary((uint8_t *)&metrics, sizeof(BrewMetrics));
    }
  }
}
