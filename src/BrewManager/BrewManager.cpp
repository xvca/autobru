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
  enabled = preferences.getBool("enabled", true);
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

bool BrewManager::startBrew(float target, bool shouldTriggerBrew) {
  if (!enabled)
    return false;

  if (!sManager->isConnected())
    return false;

  if (!isBrewing()) {
    targetWeight = target;
    brewStartTime = millis();

    sManager->startAndTare();

    setState(PREINFUSION);

    if (!shouldTriggerBrew) {
      return true;
    }

    if (pMode == SIMPLE) {
      handleSimplePreinfusion();
    } else if (pMode == WEIGHT_TRIGGERED) {
      handleWeightTriggeredPreinfusion();
    }

    return true;
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

void BrewManager::wake() {
  if (!enabled) {
    return;
  }

  active = true;
  lastActiveTime = millis();

  if (!sManager->isConnected()) {
    sManager->connectScale();
  }
}

void BrewManager::update() {

  if (!enabled) {
    return;
  }

  if (active && lastActiveTime + ACTIVITY_TIMEOUT <= millis()) {
    active = false;
    sManager->disconnectScale();
  }

  if ((isPressed(MANUAL_PIN) || isPressed(ONE_CUP_PIN) ||
       isPressed(TWO_CUP_PIN))) {

    if (!active) {
      // wait for button press to finish.
      // we do this because one of the main reasons to press the button prior to
      // brewing is probably to flush the grouphead, as such, the one or two cup
      // buttons might be held down for a longer duration of time. this way the
      // user can flush the group and wake the ESP at the same time.
      while (isPressed(MANUAL_PIN) || isPressed(ONE_CUP_PIN) ||
             isPressed(TWO_CUP_PIN)) {
        delay(100);
      }
      wake();
      return;
    } else {
      lastActiveTime = millis();
    }
  }

  if (!active)
    return;

  if (!isBrewing()) {
    if (isPressed(MANUAL_PIN)) {
      // since we are already triggering the brew by pressing physical button,
      // shouldTriggerBrew is false
      startBrew(preset1, false);
    } else if (isPressed(ONE_CUP_PIN)) {
      if (pMode == WEIGHT_TRIGGERED) {
        // triggering here cancels the brew
        triggerBrewSwitch();
        delay(50);

        // then we call startBrew with shouldTriggerBrew = true
        // this re-starts the brew with the manual brew button, allowing
        // us to pre-infuse for arbitrary length of time
        startBrew(preset2, true);
      } else {
        // if simple preinfusion mode, we just let the brew start via the 1
        // Cup button
        startBrew(preset2, false);
      }
    }
  }

  const ulong currentBrewTime = getBrewTime();

  // failsafe in case scale disconnects during brew, arbitrary stop at 25
  // seconds
  if (isBrewing() && !sManager->isConnected() && currentBrewTime >= 25 * 1000) {
    stopBrew();
    return;
  }

  if (isBrewing() && currentBrewTime >= DEBOUNCE_DELAY) {
    lastActiveTime = millis();

    // brew has been cancelled
    if (isPressed(MANUAL_PIN) || isPressed(ONE_CUP_PIN) ||
        isPressed(TWO_CUP_PIN)) {
      delay(DEBOUNCE_DELAY);
      setState(IDLE);
      return;
    }

    if (currentBrewTime >= MAX_SHOT_DURATION) {
      stopBrew();
      return;
    }

    if (!sManager->isConnected())
      return;

    const float currentFlowRate = sManager->getFlowRate();
    currentWeight = sManager->getWeight();

    /**
     * check for first drops
     * 2 second grace period is given to allow the scale to connect and tare
     * after brewing start, also we don't expect drops to appear immediately
     * anyway
     */
    if (state == PREINFUSION && pMode == WEIGHT_TRIGGERED &&
        currentWeight >= 2 && currentBrewTime >= 2000) {
      digitalWrite(BREW_SWITCH_PIN, LOW);
      setState(BREWING);
    }

    if (state != DRIPPING &&
        currentWeight + (currentFlowRate * flowCompFactor) >= targetWeight) {
      brewEndTime = currentBrewTime;
      finalFlowRate = currentFlowRate;

      setState(DRIPPING);
      triggerBrewSwitch();

      sManager->stopTimer();
    }

    if (state == DRIPPING &&
        currentBrewTime >= brewEndTime + DRIP_SETTLE_TIME) {
      finalizeBrew();
      setState(IDLE);
    }
  }
}

void BrewManager::setPrefs(BrewPrefs prefs) {
  enabled = prefs.isEnabled;
  preset1 = prefs.preset1;
  preset2 = prefs.preset2;
  pMode = prefs.pMode;

  preferences.begin("brewsettings", false);
  preferences.putBool("enabled", enabled);
  preferences.putFloat("preset1", preset1);
  preferences.putFloat("preset2", preset2);
  preferences.putInt("pmode", pMode);

  preferences.end();
}

BrewPrefs BrewManager::getPrefs() {
  BrewPrefs prefs = {enabled, preset1, preset2, pMode};
  return prefs;
}