#include "BrewManager.h"

BrewManager *BrewManager::instance = nullptr;

void BrewManager::begin() {
  machine.begin();
  sManager = ScaleManager::getInstance();
}

void BrewManager::saveSettings() {
  if (!preferences.begin("brewsettings", false)) {
    return;
  }

  preferences.putBool("enabled", prefs.isEnabled);
  preferences.putFloat("reg", prefs.regularPreset);
  preferences.putFloat("dec", prefs.decafPreset);
  preferences.putInt("decHr", prefs.decafStartHour);
  preferences.putString("tz", prefs.timezone);
  preferences.putInt("pmode", (int)prefs.pMode);
  preferences.putFloat("lr", prefs.learningRate);
  preferences.putUChar("hist", prefs.historyLength);

  preferences.putFloat("fact0", flowCompFactors[0]);
  preferences.putFloat("fact1", flowCompFactors[1]);
  preferences.putUInt("shotCtr", globalShotCounter);

  preferences.putBytes("histP0", recentShotsProfile0,
                       sizeof(recentShotsProfile0));
  preferences.putBytes("histP1", recentShotsProfile1,
                       sizeof(recentShotsProfile1));

  preferences.end();
}

void BrewManager::loadSettings() {
  preferences.begin("brewsettings", true); // read-only

  prefs.isEnabled = preferences.getBool("enabled", true);
  prefs.regularPreset = preferences.getFloat("reg", 40.0f);
  prefs.timezone = preferences.getString("tz", "GMT0");
  prefs.decafPreset = preferences.getFloat("dec", 40.0f);
  prefs.decafStartHour = preferences.getInt("decHr", -1);
  prefs.pMode = PreinfusionMode(preferences.getInt("pmode", 0));

  float lr = preferences.getFloat("lr", DEFAULT_LEARNING_RATE);
  prefs.learningRate = constrain(lr, 0.1f, 1.0f);

  int hl = preferences.getUChar("hist", DEFAULT_HISTORY_LENGTH);
  prefs.historyLength = constrain(hl, 1, ABSOLUTE_MAX_HISTORY);

  flowCompFactors[0] = preferences.getFloat("fact0", DEFAULT_FLOW_COMP);
  flowCompFactors[1] = preferences.getFloat("fact1", DEFAULT_FLOW_COMP);

  globalShotCounter = preferences.getUInt("shotCtr", 1);

  size_t expectedSize = sizeof(recentShotsProfile0);

  if (preferences.getBytesLength("histP0") == expectedSize) {
    preferences.getBytes("histP0", recentShotsProfile0, expectedSize);
  } else {
    memset(recentShotsProfile0, 0, expectedSize);
  }

  if (preferences.getBytesLength("histP1") == expectedSize) {
    preferences.getBytes("histP1", recentShotsProfile1, expectedSize);
  } else {
    memset(recentShotsProfile1, 0, expectedSize);
  }

  preferences.end();
}

void BrewManager::setPrefs(BrewPrefs newPrefs) {
  prefs = newPrefs;

  prefs.learningRate = constrain(prefs.learningRate, 0.1f, 1.0f);
  prefs.historyLength = constrain(prefs.historyLength, 1, ABSOLUTE_MAX_HISTORY);

  saveSettings();

  syncTimezone();
}

BrewPrefs BrewManager::getPrefs() { return prefs; }

void BrewManager::clearShotData() {
  flowCompFactors[0] = DEFAULT_FLOW_COMP;
  flowCompFactors[1] = DEFAULT_FLOW_COMP;

  memset(recentShotsProfile0, 0, sizeof(recentShotsProfile0));
  memset(recentShotsProfile1, 0, sizeof(recentShotsProfile1));

  saveSettings();
}

void BrewManager::finalizeBrew() {
  globalShotCounter++;

  float error = (currentWeight - targetWeight) / targetWeight;

  /*
   * in these cases we assume the user has accidentally raised the cup before
   * end of brew or accidentally touched the scale and thus we can exclude it
   * from flow comp calculation and shot history
   */
  if (abs(error) > 0.15)
    return;

  Shot *recentShots =
      (currentProfileIndex == 0) ? recentShotsProfile0 : recentShotsProfile1;

  for (int i = ABSOLUTE_MAX_HISTORY - 1; i > 0; i--) {
    recentShots[i] = recentShots[i - 1];
  }

  // Add newest shot at index 0
  recentShots[0] = {.id = globalShotCounter,
                    .targetWeight = targetWeight,
                    .finalWeight = currentWeight,
                    .lastFlowRate = lastFlowRate,
                    .stopWeight = stopWeight};

  computeCompFactor();
  saveSettings();
  pendingBeeps = 3;
}

void BrewManager::computeCompFactor() {
  Shot *recentShots =
      (currentProfileIndex == 0) ? recentShotsProfile0 : recentShotsProfile1;

  int shotCount = 0;
  float totalWeightedTime = 0;
  float totalWeight = 0;

  int loopLimit = min((int)prefs.historyLength, (int)ABSOLUTE_MAX_HISTORY);

  for (int i = 0; i < loopLimit; i++) {
    if (recentShots[i].targetWeight <= 0)
      continue;
    shotCount++;

    float drippage = recentShots[i].finalWeight - recentShots[i].stopWeight;
    float flowRate = recentShots[i].lastFlowRate;

    if (flowRate < 0.05 || drippage < 0)
      continue;

    float predictionTime = drippage / flowRate;
    float weight = 1.0f / sqrt(i + 1);

    totalWeightedTime += predictionTime * weight;
    totalWeight += weight;
  }

  if (totalWeight > 0) {
    float calculatedNewFactor = totalWeightedTime / totalWeight;

    float &factor = flowCompFactors[currentProfileIndex];

    factor = (1.0f - prefs.learningRate) * factor +
             prefs.learningRate * calculatedNewFactor;
    factor = constrain(factor, MIN_FLOW_COMP, MAX_FLOW_COMP);
  } else {
    flowCompFactors[currentProfileIndex] = DEFAULT_FLOW_COMP;
  }
}

unsigned long BrewManager::getBrewTime() {
  if (!isBrewing())
    return 0;
  return millis() - brewStartTime;
}

void BrewManager::wake() {
  if (!prefs.isEnabled) {
    return;
  }

  active = true;
  lastActiveTime = millis();

  if (!sManager->isConnected()) {
    sManager->connectScale();
  }
}

void BrewManager::update() {
  if (!prefs.isEnabled)
    return;

  // DEBUG_PRINTF("entering update, state = %d\n", state);

  machine.update();

  if (pendingBeeps > 0 && millis() - lastBeepTime > 150) {
    sManager->beep();
    pendingBeeps--;
    lastBeepTime = millis();
  }

  if (active && millis() - lastActiveTime > ACTIVITY_TIMEOUT) {
    active = false;
    sManager->disconnectScale();
  }

  if (machine.isTwoCupStart()) {
    wake();
    return;
  }

  if (!active)
    return;

  if (state == IDLE) {
    handleIdleState();
  } else {
    handleActiveState();
  }
}

void BrewManager::handleIdleState() {
  if (waitingForMacro) {
    if (machine.isMacroComplete()) {
      waitingForMacro = false;
      // macro only runs when weight triggered preinfusion is enabled and user
      // triggers a brew using the one cup button which doesn't support
      // arbitrary length preinfusion on hold so we can take the regular/decaf
      // preset and half it to get the target
      float target =
          isDecafTime() ? prefs.decafPreset / 2 : prefs.regularPreset / 2;
      startBrew(target, false);
    }
    return;
  }

  float baseTarget = prefs.regularPreset;

  if (isDecafTime()) {
    baseTarget = prefs.decafPreset;
  }

  if (machine.isManualStart()) {
    startBrew(baseTarget, false);
  } else if (machine.isOneCupStart()) {

    float halfTarget = baseTarget / 2.0f;

    if (prefs.pMode == WEIGHT_TRIGGERED) {
      machine.startPreinfusionMacro();
      waitingForMacro = true;
    } else {
      startBrew(halfTarget, false);
    }
  }
}

void BrewManager::handleActiveState() {
  // check for brew cancellation
  if (machine.isStopPressed()) {
    abortBrew(false);
    return;
  }

  // failsafe
  if (getBrewTime() >= MAX_SHOT_DURATION) {
    finishBrew();
    return;
  }

  if (!sManager->isConnected())
    return;

  float rawWeight = sManager->getWeight();
  float flowRate = sManager->getFlowRate();
  uint32_t lastPacket = sManager->getLastPacketTime();

  // time from last scale update
  float timeDelta = (millis() - lastPacket) / 1000.0f;

  currentWeight = rawWeight + (flowRate * timeDelta);

  ulong brewTime = getBrewTime();

  // transition preinf -> brewing if in weight triggered mode we lrelease relay
  // to go full pressure once first drops are detected
  if (state == PREINFUSION && prefs.pMode == WEIGHT_TRIGGERED &&
      currentWeight >= 2.0f && brewTime > 2000) {
    machine.releaseRelay();
    state = BREWING;
  }

  // transition brewing | preinf -> dripping
  if (state == BREWING || state == PREINFUSION) {
    float activeFactor = flowCompFactors[currentProfileIndex];

    float projectedWeight = currentWeight + (flowRate * activeFactor);

    if (projectedWeight >= targetWeight) {
      finishBrew();
    }
  }

  // transition dripping -> idle
  if (state == DRIPPING && millis() >= brewEndTime + DRIP_SETTLE_TIME) {
    finalizeBrew();
    state = IDLE;
  }
}

bool BrewManager::startBrew(float target, bool shouldTriggerRelay) {
  if (!prefs.isEnabled || !sManager->isConnected() || isBrewing())
    return false;

  targetWeight = target;
  lastActiveTime = millis();

  if (targetWeight < PROFILE_THRESHOLD_WEIGHT) {
    currentProfileIndex = 0;
  } else {
    currentProfileIndex = 1;
  }

  brewStartTime = millis();
  sManager->startAndTare();

  if (!shouldTriggerRelay) {
    state = (prefs.pMode == SIMPLE) ? BREWING : PREINFUSION;
  } else {
    if (prefs.pMode == SIMPLE) {
      machine.clickRelay();
      state = BREWING;
    } else {
      machine.holdRelay();
      state = PREINFUSION;
    }
  }

  return true;
}

bool BrewManager::abortBrew(bool shouldTriggerRelay) {
  // user pressed the button so machine is stopping physically, just need to
  // reset logic

  if (state == IDLE)
    return false;

  if (state == PREINFUSION) {
    machine.stopFromPreinfusion();
  }

  if (shouldTriggerRelay) {
    machine.clickRelay();
  }

  state = IDLE;
  sManager->stopTimer();
  waitingForMacro = false;
  return true;
}

bool BrewManager::finishBrew() {
  if (state == IDLE)
    return false;

  // software stop here, use relay to stop the brew
  if (state == PREINFUSION && prefs.pMode == WEIGHT_TRIGGERED) {
    machine.stopFromPreinfusion();
  } else {
    machine.clickRelay();
  }

  DEBUG_PRINTF("SETTING BREW TO DRIPPING\n");

  state = DRIPPING;
  brewEndTime = millis();
  lastFlowRate = sManager->getFlowRate();
  stopWeight = sManager->getWeight();

  sManager->stopTimer();
  return true;
}

Shot *BrewManager::getRecentShots(int profileIndex) {
  if (profileIndex == 0)
    return recentShotsProfile0;
  return recentShotsProfile1;
}

float BrewManager::getFlowCompFactor(int profileIndex) {
  if (profileIndex == 0)
    return flowCompFactors[0];
  return flowCompFactors[1];
}

bool BrewManager::isDecafTime() {
  if (prefs.decafStartHour < 0)
    return false;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }

  return (timeinfo.tm_hour >= prefs.decafStartHour);
}

void BrewManager::syncTimezone() {
  setenv("TZ", prefs.timezone.c_str(), 1);
  tzset();
}