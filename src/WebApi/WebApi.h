#include "BrewManager.h"
#include "ScaleManager.h"
#include <ESPAsyncHTTPUpdateServer.h>
#include <ESPAsyncWebServer.h>

struct BrewMetrics {
  float weight;
  float flowRate;
  float targetWeight;
  ulong time;
  uint8_t state;
  bool isActive;         // tells frontend whether we're actively scanning
  bool isScaleConnected; // tells fe whether we're connected
} __attribute__((packed));

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
  uint32_t lastWiFiAttempt = 0;
  uint32_t lastClientCleanup = 0;
  bool wifiConnected = false;

  static constexpr ushort MAX_WS_CLIENTS = 8;
  static constexpr uint32_t WIFI_RETRY_INTERVAL = 10 * 1000;
  static constexpr uint32_t WS_CLEANUP_INTERVAL = 1000;

  void checkWiFiConnection();

  void broadcastBrewMetrics();

  void setupWiFi();
  void setupRoutes();
  void setupWebSocket();

public:
  void begin();
  void update();

  int getWebSocketClientCount() {
    ws.cleanupClients(MAX_WS_CLIENTS);
    return ws.count();
  }

  static WebAPI *getInstance() {
    if (!instance) {
      instance = new WebAPI();
    }
    return instance;
  }
};
