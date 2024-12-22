#include "ScaleManager.h"
#include "WebApi.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

#define IN_PIN 5
#define OUT_PIN 22

#define MAX_SHOT_TIME_MS 30 * 1000
#define buttonDebounce 500

const static int scanTimeMs = 5000;
const static int NOTIFICATION_INTERVAL = 100;

static bool isBrewing = false;
static unsigned long shotStartTime = 0;

const float goalWeight = 40.0f;

static ScaleManager *sManager;
WebAPI *webApi;

void setup() {
  Serial.begin(115200);
  Serial.printf("Starting autobru Client\n");

  pinMode(IN_PIN, INPUT_PULLUP);
  pinMode(OUT_PIN, OUTPUT);

  sManager = new ScaleManager();
  sManager->init();

  webApi = new WebAPI(sManager);
  webApi->begin();
}

void loop() {
  sManager->onLoop();
  webApi->onLoop();

  if (!isBrewing && sManager->isConnected()) {
    if (!digitalRead(IN_PIN)) {
      isBrewing = true;
      shotStartTime = millis();
      sManager->tare();
    }
  }

  unsigned long currentShotTime = millis() - shotStartTime;

  if (isBrewing && currentShotTime > buttonDebounce) {
    if (!digitalRead(IN_PIN)) {
      Serial.printf("Brew cancelled.\n");
      isBrewing = false;
      shotStartTime = 0;
    }

    if (currentShotTime >= MAX_SHOT_TIME_MS) {
      isBrewing = false;
      digitalWrite(OUT_PIN, HIGH);
      delay(100);
      digitalWrite(OUT_PIN, LOW);
    }

    float currentWeight = sManager->getWeight();
    float currentFlowRate = sManager->getFlowRate();

    if (currentWeight + (currentFlowRate * 2.5) >= goalWeight) {
      isBrewing = false;
      shotStartTime = 0;
      digitalWrite(OUT_PIN, HIGH);
      delay(100);
      digitalWrite(OUT_PIN, LOW);
    }
  }
}
