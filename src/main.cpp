#include "BrewManager.h"
#include "ScaleManager.h"
#include "WebApi.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

static ScaleManager *sManager;
static BrewManager *bManager;
static WebAPI *webApi;

void setup() {
  Serial.begin(115200);
  Serial.printf("Starting autobru Client\n");

  sManager = sManager->getInstance();
  sManager->begin();

  bManager = bManager->getInstance();
  bManager->begin();

  webApi = webApi->getInstance();
  webApi->begin();
}

void loop() {
  sManager->update();
  webApi->update();
}
