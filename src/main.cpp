#include "./ScaleManager/ScaleManager.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

const static int scanTimeMs = 5000;
const static int NOTIFICATION_INTERVAL = 100;

ScaleManager sManager;

void setup() {
  Serial.begin(115200);
  Serial.printf("Starting autobru Client\n");

  sManager = ScaleManager();
  sManager.init();
}

void loop() { sManager.onLoop(); }
