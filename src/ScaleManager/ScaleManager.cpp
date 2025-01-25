#include "ScaleManager.h"

ScaleManager *ScaleManager::instance = nullptr;
NimBLEUUID ScaleManager::serviceUUID("0FFE");
NimBLEUUID ScaleManager::commandUUID("FF12");
NimBLEUUID ScaleManager::weightUUID("FF11");

ScaleManager::ScaleManager()
    : pClient(nullptr), pScan(nullptr), advDevice(nullptr),
      commandChar(nullptr), weightChar(nullptr), clientCallbacks(nullptr),
      scanCallbacks(nullptr) {
  instance = this;
}

void ScaleManager::onClientConnect() {
  doConnect = false;
  connected = true;
}

void ScaleManager::onClientConnectFail(int reason) {
  doConnect = false;
  advDevice = nullptr;

  if (bManager->isActive())
    doScan = true;
}

void ScaleManager::onClientDisconnect(int reason) {
  connected = false;
  doConnect = false;
  advDevice = nullptr;
  weightChar->unsubscribe();
  commandChar = nullptr;
  weightChar = nullptr;

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
    pScan->stop();
    doScan = false;
    advDevice = advertisedDevice;
    doConnect = true;
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

  uint32_t currentTime = millis();

  if (currentTime - instance->lastNotification < NOTIFICATION_INTERVAL) {
    return;
  }

  instance->lastNotification = currentTime;

  std::string str = (isNotify == true) ? "Notification" : "Indication";
  str += " from ";
  str += pRemoteCharacteristic->getClient()->getPeerAddress().toString();
  str += ": Service = " +
         pRemoteCharacteristic->getRemoteService()->getUUID().toString();
  str += ", Characteristic = " + pRemoteCharacteristic->getUUID().toString();

  ScaleData sData = instance->parseScaleData(pData, length);

  instance->latestWeight = sData.weightGrams;
  instance->latestTime = sData.milliseconds;
  instance->latestFlowRate = sData.flowRate;
}

bool ScaleManager::connectToServer() {
  if (pClient != nullptr) {
    pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
    if (pClient) {
      if (!pClient->connect(advDevice, false)) {
        pScan->start(SCAN_TIME_MS, false, true);
        doConnect = false;
        return false;
      }
    } else {
      pClient = NimBLEDevice::getDisconnectedClient();
    }
  }

  if (!pClient) {
    if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
      return false;
    }

    pClient = NimBLEDevice::createClient();

    clientCallbacks = new ClientCallbacks(this);

    pClient->setClientCallbacks(clientCallbacks);

    pClient->setConnectionParams(12, 12, 0, 150);

    pClient->setConnectTimeout(5 * 1000);

    if (!pClient->connect(advDevice)) {
      NimBLEDevice::deleteClient(pClient);

      return false;
    }
  }

  if (!pClient->isConnected()) {
    if (!pClient->connect(advDevice)) {
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
  if (!weightChar->canRead()) {
    pClient->disconnect();
    return false;
  }

  if (weightChar->canNotify())
    weightChar->subscribe(true, notifyCallback);

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
    pScan->start(SCAN_TIME_MS);
  }

  if (doConnect)
    connectToServer();
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