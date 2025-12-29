#include "ScaleManager.h"

ScaleManager *ScaleManager::instance = nullptr;
NimBLEUUID ScaleManager::serviceUUID("0FFE");
NimBLEUUID ScaleManager::commandUUID("FF12");
NimBLEUUID ScaleManager::weightUUID("FF11");

ScaleManager::ScaleManager()
    : pClient(nullptr), pScan(nullptr), commandChar(nullptr),
      weightChar(nullptr), clientCallbacks(nullptr), scanCallbacks(nullptr) {
  instance = this;
}

void ScaleManager::onClientConnect() {
  shouldConnect = false;
  connected = true;
  lastPacketTime.store(millis());
}

void ScaleManager::onClientConnectFail(int reason) {
  shouldConnect = false;
  connected = false;

  if (bManager->isActive())
    shouldScan = true;
}

void ScaleManager::onClientDisconnect(int reason) {
  DEBUG_PRINTF("Scale Disconnected (Reason: %d)\n", reason);
  cleanUpConnectionState();
}

void ScaleManager::onScanResult(
    const NimBLEAdvertisedDevice *advertisedDevice) {
  if (!advertisedDevice || !pScan)
    return;
  if (!advertisedDevice->haveName())
    return;

  const std::string &name = advertisedDevice->getName();
  if (name.rfind("BOOKOO", 0) == 0) {
    shouldScan = false;
    pScan->stop();
    {
      std::lock_guard<std::mutex> lock(instance->scaleMutex);
      targetAddress = advertisedDevice->getAddress();
    }
    shouldConnect = true;
    DEBUG_PRINTF("scale found. scan stopped. ready to connect.\n");
  }
}

void ScaleManager::onScanEnd(const NimBLEScanResults &results, int reason) {
  DEBUG_PRINTF("scan finished w/ reason: %d\n", reason);
}

void ScaleManager::notifyCallback(
    NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
    size_t length, bool isNotify) {

  if (!instance)
    return;

  // throttling, reenable if needed
  // uint32_t currentTime = millis();
  // if (currentTime - instance->lastPacketTime.load() < NOTIFICATION_INTERVAL)
  // {
  //   return;
  // }

  uint32_t now = millis();

  ScaleData sData = instance->parseScaleData(pData, length);

  instance->lastPacketTime.store(now);

  if (instance->tarePending) {
    bool isTimeout = (now - instance->tareRequestTime > 1500);
    bool isZeroed = (abs(sData.weightGrams) < 2.0f);

    if (isZeroed || isTimeout) {
      instance->tarePending = false;
    } else {
      DEBUG_PRINTF("Ignoring old weight: %.2f while taring...\n",
                   sData.weightGrams);
      return;
    }
  }

  float smoothedFlowRate;

  {
    std::lock_guard<std::mutex> lock(instance->scaleMutex);

    instance->flowBuffer[instance->bufHead].timeSecs =
        (float)sData.milliseconds / 1000.0f;
    instance->flowBuffer[instance->bufHead].weight = sData.weightGrams;

    instance->bufHead = (instance->bufHead + 1) % FLOW_WINDOW_SIZE;

    if (instance->bufCount < FLOW_WINDOW_SIZE) {
      instance->bufCount++;
    }

    if (instance->bufCount >= 3) {
      smoothedFlowRate = instance->calculateLinearRegressionFlow();
    } else {
      smoothedFlowRate = 0.0f;
    }
  }

  instance->latestWeight.store(sData.weightGrams);
  instance->latestTime.store(sData.milliseconds);
  instance->latestFlowRate.store(smoothedFlowRate);
}

bool ScaleManager::connectToServer() {
  NimBLEAddress tmp;

  {
    std::lock_guard<std::mutex> lock(scaleMutex);
    tmp = targetAddress;
  }

  if (tmp.equals(NimBLEAddress())) {
    shouldConnect = false;
    return false;
  }

  if (pScan && pScan->isScanning()) {
    pScan->stop();
    delay(10);
  }

  if (!pClient) {
    if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
      return false;
    }

    pClient = NimBLEDevice::createClient();

    if (!clientCallbacks) {
      clientCallbacks = new ClientCallbacks(this);
    }
    pClient->setClientCallbacks(clientCallbacks);
  }

  if (pClient->isConnected()) {
    // already connected...
  } else {
    DEBUG_PRINTF("ScaleManager: Connecting to %s...\n",
                 targetAddress.toString().c_str());
    if (!pClient->connect(tmp, false, false, false)) {
      DEBUG_PRINTF("connection failed....\n");
      return false;
    }
  }

  pClient->updateConnParams(120, 120, 0, 200);

  NimBLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    pClient->disconnect();
    cleanUpConnectionState();
    return false;
  }

  commandChar = pRemoteService->getCharacteristic(commandUUID);
  if (commandChar == nullptr) {
    pClient->disconnect();
    cleanUpConnectionState();
    return false;
  }

  weightChar = pRemoteService->getCharacteristic(weightUUID);
  if (weightChar == nullptr || !weightChar->canRead()) {
    pClient->disconnect();
    cleanUpConnectionState();
    return false;
  }

  if (weightChar->canNotify()) {
    if (!weightChar->subscribe(true, notifyCallback)) {
      pClient->disconnect();
      cleanUpConnectionState();
      return false;
    }
  }

  return true;
}

ScaleData ScaleManager::parseScaleData(const uint8_t *data, size_t length) {
  if (length < 20 || data[0] != 0x03 || data[1] != 0x0B) {
    throw std::runtime_error("Invalid data format");
  }

  ScaleData result;

  // Parse milliseconds (bytes 2-4)
  result.milliseconds =
      (uint32_t)data[2] << 16 | (uint32_t)data[3] << 8 | data[4];

  // Weight unit (byte 5)
  result.weightUnit = data[5];

  // Weight value (bytes 6-9)
  uint32_t rawWeight =
      (uint32_t)data[7] << 16 | (uint32_t)data[8] << 8 | data[9];
  result.weightGrams = (rawWeight / 100.0f) * (data[6] == 0x2B ? 1.0f : -1.0f);

  // Flow rate value (bytes 10-12)
  uint16_t rawFlowRate = (uint16_t)data[11] << 8 | data[12];
  result.flowRate = (rawFlowRate / 100.0f) * (data[10] == 0x2B ? 1.0f : -1.0f);

  // Battery percentage (byte 13)
  result.batteryPercent = data[13];

  // Standby minutes (bytes 14-15)
  result.standbyMinutes = (uint16_t)data[14] << 8 | data[15];

  // Buzzer gear and flow rate smoothing (bytes 16-17)
  result.buzzerGear = data[16];
  result.flowRateSmoothing = data[17];

  return result;
}

void ScaleManager::printScaleData(const ScaleData &data) {
  Serial.println(F("------ Data ------"));
  Serial.print(F("Time (ms): "));
  Serial.println(data.milliseconds);

  Serial.print(F("Weight (g): "));
  Serial.println(data.weightGrams, 2);

  Serial.print(F("Flow rate (g/s): "));
  Serial.println(data.flowRate, 2);

  Serial.print(F("Battery (%): "));
  Serial.println(data.batteryPercent);
  Serial.println(F("----------------------"));
}

float ScaleManager::calculateLinearRegressionFlow() {
  if (bufCount < 2)
    return 0.0f;

  float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;

  // find index of oldest point (where the window starts)
  size_t oldestIndex =
      (bufHead + FLOW_WINDOW_SIZE - bufCount) % FLOW_WINDOW_SIZE;

  float oldestTimestamp = flowBuffer[oldestIndex].timeSecs;

  size_t newestIndex = (bufHead + FLOW_WINDOW_SIZE - 1) % FLOW_WINDOW_SIZE;
  // if newest index timestamp is smaller than oldest
  if (flowBuffer[newestIndex].timeSecs < oldestTimestamp) {
    // buffer is invalid
    return 0.0f;
  }

  for (size_t i = 0; i < bufCount; i++) {
    size_t idx = (oldestIndex + i) % FLOW_WINDOW_SIZE;

    float x = flowBuffer[idx].timeSecs - oldestTimestamp;
    float y = flowBuffer[idx].weight;

    // if time delta < 0 something is wrong...
    if (x < 0) {
      return 0.0f;
    }

    sumX += x;
    sumY += y;
    sumXY += (x * y);
    sumXX += (x * x);
  }

  float denom = (bufCount * sumXX) - (sumX * sumX);

  if (denom == 0)
    return 0.0f;

  float slope = ((bufCount * sumXY) - (sumX * sumY)) / denom;

  // clamp to reasonable flow rate just in case
  if (slope < 0.0f)
    slope = 0.0f;
  // my bdb max flow is approx. 8g/s so 10g/s as a max is reasonable
  if (slope > 10.0f)
    slope = 10.0f;

  return slope;
}

void ScaleManager::resetFlowBuffer() {
  bufHead = 0;
  bufCount = 0;

  const FlowPoint ZERO_POINT = {};

  std::fill(flowBuffer, flowBuffer + FLOW_WINDOW_SIZE, ZERO_POINT);
}

void ScaleManager::begin() {
  BLEDevice::init("autobru-client");

  pScan = BLEDevice::getScan();

  scanCallbacks = new ScanCallbacks(this);

  pScan->setScanCallbacks(scanCallbacks);
  pScan->setInterval(500);
  pScan->setWindow(100);
  pScan->setActiveScan(false);

  bManager = BrewManager::getInstance();
}

void ScaleManager::connectScale() { shouldScan = true; }

void ScaleManager::disconnectScale() {
  shouldConnect = false;
  shouldScan = false;

  if (pClient && pClient->isConnected()) {
    pClient->disconnect();
  } else {
    cleanUpConnectionState();
  }
}

void ScaleManager::update() {
  if (shouldScan) {
    if (pScan && !pScan->isScanning()) {
      if (!shouldConnect && !connected) {
        DEBUG_PRINTF("Restarting Scan...\n");
        pScan->start(SCAN_TIME_MS, false);
      }
    }
  }

  if (shouldConnect) {
    static unsigned long lastConnectAttempt = 0;
    if (millis() - lastConnectAttempt > 2000) {
      lastConnectAttempt = millis();

      if (connectToServer()) {
        shouldConnect = false;
      }
    }
  }

  if (connected) {
    uint32_t lastTime = lastPacketTime.load();
    if (lastTime > 0 && (millis() - lastTime > CONNECTION_TIMEOUT_MS)) {
      DEBUG_PRINTF("Watchdog: Connection lost.\n");

      if (pClient)
        pClient->disconnect();

      cleanUpConnectionState();
    }
  }
}

byte TARE[6] = {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};
byte START_TIMER[6] = {0x03, 0x0a, 0x04, 0x00, 0x00, 0x0a};
byte STOP_TIMER[6] = {0x03, 0x0a, 0x05, 0x00, 0x00, 0x0d};
byte RESET_TIMER[6] = {0x03, 0x0a, 0x06, 0x00, 0x00, 0x0c};
byte START_AND_TARE[6] = {0x03, 0x0a, 0x07, 0x00, 0x00, 0x00};
byte BEEP[6] = {0x03, 0x0a, 0x02, 0x00, 0x03, 0x08};

bool ScaleManager::tare() {
  if (commandChar == nullptr)
    return false;
  if (commandChar->writeValue(TARE)) {
    setUpPendingTare();
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::startTimer() {
  if (commandChar == nullptr)
    return false;
  if (commandChar->writeValue(START_TIMER)) {
    resetFlowBuffer();
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::stopTimer() {
  if (commandChar == nullptr)
    return false;
  if (commandChar->writeValue(STOP_TIMER)) {
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::resetTimer() {
  if (commandChar == nullptr)
    return false;
  if (commandChar->writeValue(RESET_TIMER)) {
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::startAndTare() {
  if (commandChar == nullptr)
    return false;
  if (commandChar->writeValue(START_AND_TARE)) {
    setUpPendingTare();
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::beep() {
  if (commandChar == nullptr)
    return false;
  if (commandChar->writeValue(BEEP)) {
    return true;
  } else {
    return false;
  }
};

void ScaleManager::cleanUpConnectionState() {
  connected = false;
  shouldConnect = false;

  resetFlowBuffer();
  latestFlowRate = 0.0f;
  latestWeight = 0.0f;

  weightChar = nullptr;
  commandChar = nullptr;

  if (bManager && bManager->isActive()) {
    shouldScan = true;
  }
}

void ScaleManager::setUpPendingTare() {
  std::lock_guard<std::mutex> lock(scaleMutex);

  resetFlowBuffer();

  latestWeight.store(0.0f);

  tarePending = true;
  tareRequestTime = millis();
}