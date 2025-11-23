#include "BrewManager.h"

BrewManager *BrewManager::instance = nullptr;

void BrewManager::begin() {
  machine.begin();
  sManager = ScaleManager::getInstance();
}

void BrewManager::saveSettings() {
  preferences.begin("brewsettings", false); // read-write

  preferences.putFloat("fact0", flowCompFactors[0]);
  preferences.putFloat("fact1", flowCompFactors[1]);

  // save history for profile 0;
  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    String key = "p0_" + String(i);
    preferences.putFloat((key + "t").c_str(),
                         recentShotsProfile0[i].targetWeight);
    preferences.putFloat((key + "f").c_str(),
                         recentShotsProfile0[i].finalWeight);
    preferences.putFloat((key + "r").c_str(),
                         recentShotsProfile0[i].lastFlowRate);
    preferences.putFloat((key + "s").c_str(),
                         recentShotsProfile0[i].stopWeight);
  }

  // save history for profile 1;
  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    String key = "p1_" + String(i);
    preferences.putFloat((key + "t").c_str(),
                         recentShotsProfile1[i].targetWeight);
    preferences.putFloat((key + "f").c_str(),
                         recentShotsProfile1[i].finalWeight);
    preferences.putFloat((key + "r").c_str(),
                         recentShotsProfile1[i].lastFlowRate);
    preferences.putFloat((key + "s").c_str(),
                         recentShotsProfile1[i].stopWeight);
  }

  globalShotCounter = preferences.getUInt("shotCtr", 1);

  preferences.end();
}

void BrewManager::loadSettings() {
  preferences.begin("brewsettings", true); // read-only

  enabled = preferences.getBool("enabled", true);
  preset1 = preferences.getFloat("preset1", 20.0f);
  preset2 = preferences.getFloat("preset2", 40.0f);
  pMode = PreinfusionMode(preferences.getInt("pmode", 0));

  flowCompFactors[0] = preferences.getFloat("fact0", DEFAULT_FLOW_COMP);
  flowCompFactors[1] = preferences.getFloat("fact1", DEFAULT_FLOW_COMP);

  // load profile 0
  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    String key = "p0_" + String(i);
    recentShotsProfile0[i].targetWeight =
        preferences.getFloat((key + "t").c_str(), 0);
    recentShotsProfile0[i].finalWeight =
        preferences.getFloat((key + "f").c_str(), 0);
    recentShotsProfile0[i].lastFlowRate =
        preferences.getFloat((key + "r").c_str(), 0);
    recentShotsProfile0[i].stopWeight =
        preferences.getFloat((key + "s").c_str(), 0);
  }

  // load profile 1
  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    String key = "p1_" + String(i);
    recentShotsProfile1[i].targetWeight =
        preferences.getFloat((key + "t").c_str(), 0);
    recentShotsProfile1[i].finalWeight =
        preferences.getFloat((key + "f").c_str(), 0);
    recentShotsProfile1[i].lastFlowRate =
        preferences.getFloat((key + "r").c_str(), 0);
    recentShotsProfile1[i].stopWeight =
        preferences.getFloat((key + "s").c_str(), 0);
  }

  preferences.putUInt("shotCtr", globalShotCounter);

  preferences.end();
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

void BrewManager::clearShotData() {
  flowCompFactors[0] = DEFAULT_FLOW_COMP;
  flowCompFactors[1] = DEFAULT_FLOW_COMP;

  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
    recentShotsProfile0[i] = {0, 0, 0, 0};
    recentShotsProfile1[i] = {0, 0, 0, 0};
  }
  saveSettings();
}

bool BrewManager::deleteShotById(uint32_t id) {
  auto deleteFromList = [&](Shot *list, int profileIdx) -> bool {
    for (int i = 0; i < MAX_STORED_SHOTS; i++) {
      if (list[i].id == id) {
        for (int j = i; j < MAX_STORED_SHOTS - 1; j++) {
          list[j] = list[j + 1];
        }
        list[MAX_STORED_SHOTS - 1] = {0, 0, 0, 0, 0};

        computeCompFactorFromScratch(profileIdx);
        return true;
      }
    }
    return false;
  };

  if (deleteFromList(recentShotsProfile0, 0)) {
    saveSettings();
    return true;
  }

  if (deleteFromList(recentShotsProfile1, 1)) {
    saveSettings();
    return true;
  }

  return false;
}

void BrewManager::finalizeBrew() {
  /*
   * in these cases we assume the user has accidentally raised the cup before
   * end of brew or accidentally touched the scale and thus we can exclude it
   * from flow comp calculation and shot history
   */

  globalShotCounter++;

  float error = (currentWeight - targetWeight) / targetWeight;

  if (abs(error) > 0.15)
    return;

  Shot *recentShots =
      (currentProfileIndex == 0) ? recentShotsProfile0 : recentShotsProfile1;

  // shift the recent shot array and discard least recent
  for (int i = MAX_STORED_SHOTS - 1; i > 0; i--) {
    if (recentShots[i - 1].targetWeight != 0) {
      recentShots[i] = recentShots[i - 1];
    }
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

  for (int i = 0; i < MAX_STORED_SHOTS; i++) {
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

    factor =
        (1.0f - LEARNING_RATE) * factor + LEARNING_RATE * calculatedNewFactor;
    factor = constrain(factor, MIN_FLOW_COMP, MAX_FLOW_COMP);
  } else {
    flowCompFactors[currentProfileIndex] = DEFAULT_FLOW_COMP;
  }
}

void BrewManager::computeCompFactorFromScratch(int profileIdx) {
  computeCompFactor();
}

void BrewManager::recalculateCompFactor() {
  computeCompFactorFromScratch(currentProfileIndex);
  saveSettings();
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
  if (!enabled)
    return;

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
      startBrew(preset2, true);
    }
    return;
  }

  if (machine.isManualStart()) {
    startBrew(preset1, true);
  } else if (machine.isOneCupStart()) {
    if (pMode == WEIGHT_TRIGGERED) {
      machine.startPreinfusionMacro();
      waitingForMacro = true;
    } else {
      startBrew(preset2, false);
    }
  }
}

void BrewManager::handleActiveState() {
  // check for brew cancellation
  if (machine.isStopPressed()) {
    abortBrew();
    return;
  }

  // check for manual release
  if (machine.isManualReleased()) {
    abortBrew();
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
  if (state == PREINFUSION && pMode == WEIGHT_TRIGGERED &&
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

bool BrewManager::startBrew(float target, bool manualOverride) {
  if (!enabled || !sManager->isConnected() || isBrewing())
    return false;

  targetWeight = target;

  if (targetWeight < PROFILE_THRESHOLD_WEIGHT) {
    currentProfileIndex = 0;
  } else {
    currentProfileIndex = 1;
  }

  brewStartTime = millis();
  sManager->startAndTare();

  if (manualOverride) {
    state = (pMode == SIMPLE) ? BREWING : PREINFUSION;
  } else {
    if (pMode == SIMPLE) {
      machine.clickRelay();
      state = BREWING;
    } else {
      machine.holdRelay();
      state = PREINFUSION;
    }
  }

  return true;
}

bool BrewManager::abortBrew() {
  // user pressed the button so machine is stopping physically, just need to
  // reset logic

  if (state == IDLE)
    return false;

  if (state == PREINFUSION) {
    machine.releaseRelay();
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
  if (state == PREINFUSION && pMode == WEIGHT_TRIGGERED) {
    machine.stopFromPreinfusion();
  } else {
    machine.clickRelay();
  }

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