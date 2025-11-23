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
  doConnect = false;
  connected = true;
}

void ScaleManager::onClientConnectFail(int reason) {
  doConnect = false;

  if (bManager->isActive())
    doScan = true;
}

void ScaleManager::onClientDisconnect(int reason) {
  connected = false;
  doConnect = false;

  if (advDevice) {
    delete advDevice;
    advDevice = nullptr;
  }

  if (weightChar) {
    weightChar->unsubscribe();
    weightChar = nullptr;
  }
  commandChar = nullptr;

  if (bManager->isActive())
    doScan = true;
}

void ScaleManager::onScanResult(
    const NimBLEAdvertisedDevice *advertisedDevice) {

  if (!advertisedDevice || !pScan) {
    return;
  }

  if (!advertisedDevice->haveName()) {
    return;
  }

  const std::string &name = advertisedDevice->getName();
  if (name.rfind("BOOKOO", 0) == 0) {
    if (instance->advDevice) {
      delete instance->advDevice;
      instance->advDevice = nullptr;
    }

    doConnect = true;
    instance->advDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
    pScan->stop();
    doScan = false;
  }
}

void ScaleManager::onScanEnd(const NimBLEScanResults &results, int reason) {
  if (!doConnect) {
    pScan->start(SCAN_TIME_MS);
  }
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

  instance->flowBuffer[instance->bufHead].timeSecs =
      (float)sData.milliseconds / 1000.0f;
  instance->flowBuffer[instance->bufHead].weight = sData.weightGrams;

  instance->bufHead = (instance->bufHead + 1) % FLOW_WINDOW_SIZE;

  if (instance->bufCount < FLOW_WINDOW_SIZE) {
    instance->bufCount++;
  }

  float smoothedFlowRate;

  if (instance->bufCount >= 3) {
    smoothedFlowRate = instance->calculateLinearRegressionFlow();
  } else {
    smoothedFlowRate = 0.0f;
  }

  instance->latestWeight.store(sData.weightGrams);
  instance->latestTime.store(sData.milliseconds);
  instance->latestFlowRate.store(smoothedFlowRate);
}

bool ScaleManager::connectToServer() {
  if (advDevice == nullptr) {
    doConnect = false;
    return false;
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
                 advDevice->getAddress().toString().c_str());
    if (!pClient->connect(advDevice, false, false, false)) {
      DEBUG_PRINTF("connection failed....\n");
      return false;
    }
  }

  NimBLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    pClient->disconnect();
    return false;
  }

  commandChar = pRemoteService->getCharacteristic(commandUUID);
  if (commandChar == nullptr) {
    pClient->disconnect();
    return false;
  }

  weightChar = pRemoteService->getCharacteristic(weightUUID);
  if (weightChar == nullptr || !weightChar->canRead()) {
    pClient->disconnect();
    return false;
  }

  if (weightChar->canNotify()) {
    if (!weightChar->subscribe(true, notifyCallback)) {
      pClient->disconnect();
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

  float timeOffset = flowBuffer[oldestIndex].timeSecs;

  for (size_t i = 0; i < bufCount; i++) {
    size_t idx = (oldestIndex + i) % FLOW_WINDOW_SIZE;

    float x = flowBuffer[idx].timeSecs - timeOffset;
    float y = flowBuffer[idx].weight;

    sumX += x;
    sumY += y;
    sumXY += (x * y);
    sumXX += (x * x);
  }

  float denom = (bufCount * sumXX) - (sumX * sumX);

  if (denom == 0)
    return 0.0f;

  return ((bufCount * sumXY) - (sumX * sumY)) / denom;
}

void ScaleManager::resetFlowBuffer() {
  bufHead = 0;
  bufCount = 0;
}

void ScaleManager::begin() {
  BLEDevice::init("autobru-client");

  pScan = BLEDevice::getScan();

  scanCallbacks = new ScanCallbacks(this);

  pScan->setScanCallbacks(scanCallbacks);
  pScan->setInterval(2000);
  pScan->setWindow(100);
  pScan->setActiveScan(true);

  bManager = BrewManager::getInstance();
}

void ScaleManager::connectScale() {
  if (!preScanning() && !isScanning() && !isConnecting())
    doScan = true;
}

void ScaleManager::disconnectScale() {
  if (pClient->isConnected())
    pClient->disconnect();
}

void ScaleManager::update() {
  if (doScan) {
    doScan = false;
    if (!pScan->isScanning()) {
      pScan->start(SCAN_TIME_MS);
    }
  }

  if (doConnect) {
    static unsigned long lastConnectAttempt = 0;
    if (millis() - lastConnectAttempt > 2000) {
      lastConnectAttempt = millis();

      if (connectToServer()) {
        doConnect = false;
      }
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
  if (commandChar->writeValue(TARE)) {
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::startTimer() {
  if (commandChar->writeValue(START_TIMER)) {
    resetFlowBuffer();
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::stopTimer() {
  if (commandChar->writeValue(STOP_TIMER)) {
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::resetTimer() {
  if (commandChar->writeValue(RESET_TIMER)) {
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::startAndTare() {
  if (commandChar->writeValue(START_AND_TARE)) {
    resetFlowBuffer();
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::beep() {
  if (commandChar->writeValue(BEEP)) {
    return true;
  } else {
    return false;
  }
};