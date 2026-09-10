#include "BrewManager.h"
#include "ScaleManager.h"
#include <ESPAsyncHTTPUpdateServer.h>
#include <ESPAsyncWebServer.h>

struct BrewMetrics {
  float weight;
  float flowRate;
  float targetWeight;
  uint32_t time;
  uint8_t state;
  bool isActive;         // tells frontend whether we're actively scanning
  bool isScaleConnected; // tells fe whether we're connected
} __attribute__((packed));

static_assert(sizeof(BrewMetrics) == 19, "Bru expects a 19-byte metrics packet");

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
  static constexpr uint8_t WS_MAX_RATE_HZ = 20;
  static constexpr uint8_t WS_IDLE_RATE_HZ = 2;
  static constexpr uint32_t WS_RATE_INCREASE_INTERVAL = 2000;

  // Owned by the main loop, not the asynchronous WebSocket callbacks.
  struct TelemetryClient {
    uint32_t id = 0;
    uint32_t lastAttempt = 0;
    uint32_t lastIncrease = 0;
    uint8_t rateHz = 8;
    bool hasMetrics = false;
    BrewMetrics lastMetrics{};
  };
  TelemetryClient telemetryClients[MAX_WS_CLIENTS]{};

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
