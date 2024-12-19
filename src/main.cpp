#include "ScaleManager.h"
#include "WebApi.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

const static int scanTimeMs = 5000;
const static int NOTIFICATION_INTERVAL = 100;

static ScaleManager *sManager;
WebAPI *webApi;

void setup() {
  Serial.begin(115200);
  Serial.printf("Starting autobru Client\n");

  sManager = new ScaleManager();
  sManager->init();

  webApi = new WebAPI(sManager);
  webApi->begin();
}

void loop() {
  sManager->onLoop();
  webApi->onLoop();
}
