#include "BrewManager.h"
#include <Arduino.h>

BrewManager *BrewManager::instance = nullptr;

void BrewManager::begin() {
  pinMode(MANUAL_PIN, INPUT_PULLUP);
  pinMode(ONE_CUP_PIN, INPUT_PULLUP);
  pinMode(TWO_CUP_PIN, INPUT_PULLUP);

  pinMode(BREW_SWITCH_PIN, OUTPUT);
  digitalWrite(BREW_SWITCH_PIN, LOW);

  sManager = ScaleManager::getInstance();
}

void BrewManager::saveSettings() {
  preferences.begin("brewsettings", false); // read-write
  preferences.putFloat("flowcomp", flowCompFactor);

  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    String key = "shot" + String(i);
    preferences.putFloat((key + "t").c_str(), recentShots[i].targetWeight);
    preferences.putFloat((key + "f").c_str(), recentShots[i].finalWeight);
    preferences.putFloat((key + "r").c_str(), recentShots[i].lastFlowRate);
  }
  preferences.putInt("shotindex", currentShotIndex);

  preferences.end();
}

void BrewManager::loadSettings() {
  preferences.begin("brewsettings", true); // read-only
  flowCompFactor = preferences.getFloat("flowcomp", 1.0f);
  preset1 = preferences.getFloat("preset1", 20.0f);
  preset2 = preferences.getFloat("preset2", 40.0f);
  pMode = PreinfusionMode(preferences.getInt("pmode", 0));

  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    String key = "shot" + String(i);
    recentShots[i].targetWeight = preferences.getFloat((key + "t").c_str(), 0);
    recentShots[i].finalWeight = preferences.getFloat((key + "f").c_str(), 0);
    recentShots[i].lastFlowRate = preferences.getFloat((key + "r").c_str(), 0);
  }
  currentShotIndex = preferences.getInt("shotindex", 0);

  preferences.end();
}

void BrewManager::setPreset1(float target = 20) {
  preferences.begin("brewsettings", false);
  preferences.putFloat("preset1", target);
  preferences.end();

  preset1 = target;
}

void BrewManager::setPreset2(float target = 40) {
  preferences.begin("brewsettings", false);
  preferences.putFloat("preset2", target);
  preferences.end();

  preset2 = target;
}

void BrewManager::setPreinfusionMode(PreinfusionMode mode) {
  preferences.begin("brewsettings", false);
  preferences.putInt("pmode", mode);
  preferences.end();

  pMode = mode;
}

void BrewManager::clearShotData() {
  preferences.begin("brewsettings", false); // read-write
  preferences.putFloat("flowcomp", flowCompFactor);

  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    String key = "shot" + String(i);
    preferences.putFloat((key + "t").c_str(), 0);
    preferences.putFloat((key + "f").c_str(), 0);
    preferences.putFloat((key + "r").c_str(), 0);
  }

  currentShotIndex = 0;
  preferences.putInt("shotindex", currentShotIndex);
  preferences.end();
}

void BrewManager::triggerBrewSwitch(int duration = 100) {
  digitalWrite(BREW_SWITCH_PIN, HIGH);
  delay(duration);
  digitalWrite(BREW_SWITCH_PIN, LOW);
}

bool BrewManager::isPressed(uint8_t button) { return !digitalRead(button); }

bool BrewManager::startBrew(float target = 40, bool shouldTriggerBrew = true) {
  targetWeight = target;

  if (!isBrewing()) {
    state = PREINFUSION;
    sManager->startAndTare();
    brewStartTime = millis();

    if (!shouldTriggerBrew) {
      return true;
    }

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

bool BrewManager::stopBrew() {
  triggerBrewSwitch();
  state = IDLE;
  sManager->stopTimer();

  return true;
}

void BrewManager::finalizeBrew() {
  recentShots[currentShotIndex] = {targetWeight, currentWeight, finalFlowRate};

  currentShotIndex = (currentShotIndex + 1) % MAX_STORED_SHOTS;

  float totalError = 0;
  float totalWeight = 0;

  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    if (recentShots[i].targetWeight > 0) {
      float error = recentShots[i].finalWeight - recentShots[i].targetWeight;
      float weight = 1.0 / (MAX_STORED_SHOTS - i);
      totalError += error * weight;
      totalWeight += weight;
    }
  }

  if (totalWeight > 0) {
    float avgError = totalError / totalWeight;
    float adjustment = (avgError / finalFlowRate) * LEARNING_RATE;
    flowCompFactor =
        constrain(flowCompFactor + adjustment, MIN_FLOW_COMP, MAX_FLOW_COMP);

    saveSettings();
  }
}

void BrewManager::handleSimplePreinfusion() {
  triggerBrewSwitch();

  state = BREWING;
}

void BrewManager::handleWeightTriggeredPreinfusion() {
  digitalWrite(BREW_SWITCH_PIN, HIGH);
}

unsigned long BrewManager::getBrewTime() {
  if (!isBrewing())
    return 0;
  return millis() - brewStartTime;
}

void BrewManager::update() {

  if (!isBrewing()) {
    if (isPressed(MANUAL_PIN)) {
      // since we are already triggering the brew by pressing physical button,
      // shouldTriggerBrew is false
      startBrew(preset1, false);
    } else if (isPressed(ONE_CUP_PIN)) {
      startBrew(preset2, false);
    }
  }

  const ulong currentBrewTime = getBrewTime();

  if (isBrewing() && currentBrewTime >= DEBOUNCE_DELAY) {
    const float currentFlowRate = sManager->getFlowRate();

    currentWeight = sManager->getWeight();

    // brew has been cancelled
    if (isPressed(MANUAL_PIN) || isPressed(ONE_CUP_PIN) ||
        isPressed(TWO_CUP_PIN)) {
      // delay 500ms for button debounce
      delay(DEBOUNCE_DELAY);
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

    if (state != DRIPPING &&
        currentWeight + (currentFlowRate * flowCompFactor) >= targetWeight) {
      brewEndTime = currentBrewTime;
      finalFlowRate = currentFlowRate;
      state = DRIPPING;
      triggerBrewSwitch();
      sManager->stopTimer();
    }

    if (state == DRIPPING &&
        currentBrewTime >= brewEndTime + DRIP_SETTLE_TIME) {
      finalizeBrew();
      state = IDLE;
    }
  }
}