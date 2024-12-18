#include "ScaleManager.h"
#include <WebServer.h>

class WebAPI {
private:
  WebServer server;
  static const int port;
  ScaleManager *sManager;

  void setupRoutes();

public:
  WebAPI(ScaleManager *manager);
  void begin();
  void handleClient();
};
