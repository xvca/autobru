#include "BrewManager.h"
#include "ScaleManager.h"
#include "WebApi.h"
#include "debug.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <NimBLEDevice.h>

static ScaleManager *sManager;
static BrewManager *bManager;
static WebAPI *webApi;

#define LED_PIN 21
#define NUM_LEDS 1
#define BRIGHTNESS 10

Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(2000);

  pixels.begin();
  pixels.setBrightness(BRIGHTNESS);

  pixels.setPixelColor(0, 255, 255, 255);
  pixels.show();

  DEBUG_PRINTF("SETUP...\n");

  sManager = ScaleManager::getInstance();
  bManager = BrewManager::getInstance();
  webApi = WebAPI::getInstance();

  sManager->begin();
  DEBUG_PRINTF("Started ScaleManager\n");
  bManager->begin();
  DEBUG_PRINTF("Started BrewManager\n");
  webApi->begin();
  DEBUG_PRINTF("Started WebAPI\n");
}

void loop() {
  webApi->update();

  if (bManager->isEnabled())
    bManager->update();

  if (!bManager->isActive()) {
    delay(100);
  } else {
    sManager->update();
  }
}
