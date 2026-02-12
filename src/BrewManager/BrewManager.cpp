#include "BrewManager.h"
#include "WebApi.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

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
  preferences.putFloat("lag", prefs.systemLag);

  preferences.putFloat("bias0", flowCompBias[0]);
  preferences.putFloat("bias1", flowCompBias[1]);
  preferences.putUInt("shotCtr", globalShotCounter);

  preferences.putBytes("histP0", recentShotsProfile0,
                       sizeof(recentShotsProfile0));
  preferences.putBytes("histP1", recentShotsProfile1,
                       sizeof(recentShotsProfile1));

  preferences.putString("apiUrl", prefs.apiUrl);
  preferences.putString("apiToken", prefs.apiToken);
  preferences.putBool("autoSave", prefs.autoSavePreset);

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
  prefs.learningRate = constrain(lr, 0.0f, 1.0f);

  float lag = preferences.getFloat("lag", 0.8f);
  prefs.systemLag = constrain(lag, 0.0f, 2.0f);

  flowCompBias[0] = preferences.getFloat("bias0", 1.0f);
  flowCompBias[1] = preferences.getFloat("bias1", 1.0f);

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

  prefs.apiUrl = preferences.getString("apiUrl", "");
  prefs.apiToken = preferences.getString("apiToken", "");
  prefs.autoSavePreset = preferences.getBool("autoSave", false);

  preferences.end();
}

void BrewManager::setPrefs(BrewPrefs newPrefs) {
  prefs = newPrefs;

  prefs.learningRate = constrain(prefs.learningRate, 0.0f, 1.0f);
  prefs.systemLag = constrain(prefs.systemLag, 0.0f, 2.0f);

  saveSettings();

  syncTimezone();
}

BrewPrefs BrewManager::getPrefs() { return prefs; }

void BrewManager::clearShotData() {
  flowCompBias[0] = 1.0f;
  flowCompBias[1] = 1.0f;

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

  for (int i = MAX_HISTORY - 1; i > 0; i--) {
    recentShots[i] = recentShots[i - 1];
  }

  // Add newest shot at index 0
  recentShots[0] = {.id = globalShotCounter,
                    .targetWeight = targetWeight,
                    .finalWeight = currentWeight,
                    .lastFlowRate = lastFlowRate,
                    .stopWeight = stopWeight};

  updateFlowBias();

  if (prefs.autoSavePreset) {
    if (isDecafTime()) {
      prefs.decafPreset = targetWeight;
    } else {
      prefs.regularPreset = targetWeight;
    }
  }

  saveSettings();
  pendingBeeps = 3;

  WebAPI *webApi = WebAPI::getInstance();
  if (webApi && webApi->getWebSocketClientCount() == 0) {
    sendAutoBrewLog();
  }
}

void BrewManager::updateFlowBias() {
  float totalDrippage = currentWeight - stopWeight;

  float lagComponent = lastFlowRate * prefs.systemLag;

  float observedBias = totalDrippage - lagComponent;

  float alpha = prefs.learningRate;
  float &bias = flowCompBias[currentProfileIndex];

  bias = (bias * (1.0f - alpha)) + (observedBias * alpha);

  bias = constrain(bias, MIN_BIAS, MAX_BIAS);
}

int BrewManager::getBrewTimeSeconds() {
  if (brewEndTime > 0 && brewStartTime > 0) {
    return (brewEndTime - brewStartTime) / 1000;
  }
  return 0;
}

void BrewManager::sendAutoBrewLog() {
  if (prefs.apiUrl.length() == 0 || prefs.apiToken.length() == 0) {
    DEBUG_PRINTF("Auto-brew logging not configured\n");
    return;
  }

  HTTPClient http;
  WiFiClient client;

  String url = prefs.apiUrl + "/api/brews/auto-create";

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + prefs.apiToken);

  JsonDocument doc;
  doc["yieldWeight"] = targetWeight;
  doc["brewTime"] = getBrewTimeSeconds();
  doc["isDecaf"] = isDecafTime();

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);

  if (httpCode == 201) {
    DEBUG_PRINTF("Auto-brew logged successfully\n");
    pendingBeeps = 4;
  } else {
    DEBUG_PRINTF("Failed to log brew: HTTP %d\n", httpCode);
    if (httpCode > 0) {
      DEBUG_PRINTF("Response: %s\n", http.getString().c_str());
    }
  }

  http.end();
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
    startBrew(baseTarget, true);
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
  if (state != DRIPPING && getBrewTime() >= MAX_SHOT_DURATION) {
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
    float dynamicDrippage = flowRate * prefs.systemLag;

    float staticDrippage = flowCompBias[currentProfileIndex];

    float projectedFinalWeight =
        currentWeight + dynamicDrippage + staticDrippage;

    if (projectedFinalWeight >= targetWeight) {
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

  if (state == IDLE || state == DRIPPING)
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

float BrewManager::getFlowCompBias(int profileIndex) {
  if (profileIndex == 0)
    return flowCompBias[0];
  return flowCompBias[1];
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