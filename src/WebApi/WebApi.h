#include "BrewManager.h"
#include "ScaleManager.h"
#include <ESPAsyncHTTPUpdateServer.h>
#include <ESPAsyncWebServer.h>

struct BrewMetrics {
  float weight;
  float flowRate;
  float time;
  BrewState state;
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
  static constexpr ushort WEBSOCKET_INTERVAL = 100;

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
