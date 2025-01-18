#include "BrewManager.h"
#include "ScaleManager.h"
#include "WebApi.h"
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
  // delay makes bootloop more noticeable if there is one
  Serial.begin(115200);
  delay(2000);

  pixels.begin();
  pixels.setBrightness(BRIGHTNESS);

  pixels.setPixelColor(0, 255, 255, 255);
  pixels.show();

  sManager = ScaleManager::getInstance();

  sManager->begin();

  bManager = bManager->getInstance();

  bManager->begin();

  webApi = webApi->getInstance();

  webApi->begin();

  Serial.println("All managers started successfully");
}

void loop() {
  sManager->update();
  bManager->update();
  webApi->update();
}
