#include "ScaleManager.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

class WebAPI {
private:
  WebAPI();
  static WebAPI *instance;

  AsyncWebServer server;
  AsyncWebSocket ws;
  ScaleManager *sManager;

  ulong lastWebSocketUpdate = 0;
  const ushort WEBSOCKET_INTERVAL = 100;

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
