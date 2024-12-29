#include "BrewManager.h"
#include <Arduino.h>

BrewManager::BrewManager() {}

void BrewManager::begin() {
  // Setup input pins with pullup resistors
  pinMode(MANUAL_PIN, INPUT_PULLUP);
  pinMode(TWO_CUP_PIN, INPUT_PULLUP);
  pinMode(ONE_CUP_PIN, INPUT_PULLUP);

  // Setup brew switch control pin
  pinMode(BREW_SWITCH_PIN, OUTPUT);
  digitalWrite(BREW_SWITCH_PIN, LOW);

  targetWeight = 40.0;
}

void BrewManager::triggerBrewSwitch(int duration = 10) {
  digitalWrite(BREW_SWITCH_PIN, HIGH);
  delay(duration);
  digitalWrite(BREW_SWITCH_PIN, LOW);
}

bool BrewManager::isPressed(uint8_t button) { return !digitalRead(button); }

bool BrewManager::startBrew() {
  if (!isBrewing()) {
    state = PREINFUSION;
    sManager->startAndTare();
    brewStartTime = millis();

    if (pMode == SIMPLE) {
      handleSimplePreinfusion();
      return true;
    } else if (pMode == WEIGHT_TRIGGERED) {
      handleWeightTriggeredPreinfusion();
      return true;
    }
  }

  return false;
}

void BrewManager::handleSimplePreinfusion() {
  triggerBrewSwitch();
  state = BREWING;
}

void BrewManager::handleWeightTriggeredPreinfusion() {
  digitalWrite(BREW_SWITCH_PIN, HIGH);
}

bool BrewManager::stopBrew() {
  triggerBrewSwitch();

  return false;
}

unsigned long BrewManager::getBrewTime() {
  if (!isBrewing())
    return 0;
  return millis() - brewStartTime;
}

void BrewManager::update() {
  const ulong currentBrewTime = getBrewTime();
  if (!isBrewing()) {
    if (isPressed(MANUAL_PIN)) {
      // use weight preset 1
      startBrew();
    } else if (isPressed(ONE_CUP_PIN)) {
      // use weight preset 2
      startBrew();
    }
  }

  if (isBrewing()) {
    const float currentWeight = sManager->getWeight();
    const float currentFlowRate = sManager->getFlowRate();

    // brew has been cancelled
    if (isPressed(MANUAL_PIN) || isPressed(ONE_CUP_PIN) ||
        isPressed(TWO_CUP_PIN)) {
      // delay 500ms for button debounce
      delay(500);
      setState(IDLE);
    }

    if (currentBrewTime >= MAX_SHOT_DURATION) {
      triggerBrewSwitch();
      state = IDLE;

      return;
    }

    /**
     * check for first drops
     * 1 second grace period is given to allow for the scale
     * to tare after brewing start + we don't expect drops to appear immediately
     * anyway
     */
    if (state == PREINFUSION && pMode == WEIGHT_TRIGGERED &&
        currentWeight >= 1 && currentBrewTime >= 1000) {
      digitalWrite(BREW_SWITCH_PIN, LOW);
      state = BREWING;
    }

    if (currentWeight + currentFlowRate >= targetWeight) {
      dripStartTime = millis();
      state = DRIPPING;
      triggerBrewSwitch();
    }

    if (state == DRIPPING &&
        currentBrewTime >= dripStartTime + DRIP_SETTLE_TIME) {
      state = IDLE;
    }
  }
}