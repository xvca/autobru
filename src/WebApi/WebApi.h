#include "BrewManager.h"
#include "ScaleManager.h"
#include "debug.h"
#include <ESPAsyncHTTPUpdateServer.h>
#include <ESPAsyncWebServer.h>

struct BrewMetrics {
  float weight;
  float flowRate;
  ulong time;
  BrewState state;
  float targetWeight;
};

class WebAPI {
private:
  WebAPI();
  WebAPI(const WebAPI &) = delete;
  WebAPI &operator=(const WebAPI &) = delete;

  static WebAPI *instance;

  AsyncWebServer server;
  AsyncWebSocket ws;
  ESPAsyncHTTPUpdateServer updateServer;

  ScaleManager *sManager;
  BrewManager *bManager;

  ulong lastWebSocketUpdate = 0;
  ulong lastWiFiCheck = 0;

  static constexpr ushort MAX_WS_CLIENTS = 1;
  static constexpr ushort WEBSOCKET_INTERVAL = 250;
  static constexpr ulong WIFI_CHECK_INTERVAL = 60 * 1000;

  void checkWiFiConnection();

  void broadcastBrewMetrics();
  String serializeBrewMetrics(const BrewMetrics &metrics);

  void setupWiFi();
  void setupRoutes();
  void setupWebSocket();

public:
  void begin();
  void update();

  static WebAPI *getInstance() {
    if (!instance) {
      instance = new WebAPI();
    }
    return instance;
  }
};
