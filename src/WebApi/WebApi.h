#include "ScaleManager.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

class WebAPI {
private:
  AsyncWebServer server;
  AsyncWebSocket ws;
  ScaleManager *sManager;

  ulong lastWebSocketUpdate = 0;
  const ushort WEBSOCKET_INTERVAL = 100;

  void setupWiFi();
  void setupRoutes();
  void setupWebSocket();

public:
  WebAPI(ScaleManager *manager);
  void begin();
  void onLoop();
};
