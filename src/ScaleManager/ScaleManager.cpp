#include "ScaleManager.h"

BLEUUID ScaleManager::serviceUUID("0FFE");
BLEUUID ScaleManager::commandUUID("FF12");
BLEUUID ScaleManager::weightUUID("FF11");

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
  Serial.printf("%s Failed to connect, reason = %d - Starting scan\n",
                pClient->getPeerAddress().toString().c_str(), reason);

  doConnect = false;
  advDevice = nullptr;
  pScan->start(SCAN_TIME_MS, false, true);
}

void ScaleManager::onClientDisconnect(int reason) {
  Serial.printf("%s Disconnected, reason = %d - Starting scan\n",
                pClient->getPeerAddress().toString().c_str(), reason);

  connected = false;
  doConnect = false;
  advDevice = nullptr;
  weightChar->unsubscribe();
  commandChar = nullptr;
  weightChar = nullptr;
  pScan->start(SCAN_TIME_MS, false, true);
}

void ScaleManager::onScanResult(const BLEAdvertisedDevice *advertisedDevice) {
  if (advertisedDevice->getName().length() &&
      advertisedDevice->getName().rfind("BOOKOO", 0) == 0) {
    Serial.printf("Found Our Scale\n");
    pScan->stop();
    advDevice = advertisedDevice;
    doConnect = true;
  }
}

void ScaleManager::onScanEnd(const BLEScanResults &results, int reason) {
  Serial.printf("Scan Ended, reason: %d, device count: %d; Restarting scan\n",
                reason, results.getCount());
  pScan->start(SCAN_TIME_MS, false, true);
}

void ScaleManager::notifyCallback(
    BLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
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
    pClient = BLEDevice::getClientByPeerAddress(advDevice->getAddress());
    if (pClient) {
      if (!pClient->connect(advDevice, false)) {
        Serial.printf("Reconnect failed\n");
        pScan->start(SCAN_TIME_MS, false, true);
        doConnect = false;
        return false;
      }
      Serial.printf("Reconnected client\n");
    } else {
      pClient = BLEDevice::getDisconnectedClient();
    }
  }

  if (!pClient) {
    if (BLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
      Serial.printf("Max clients reached - no more connections available\n");
      return false;
    }

    pClient = BLEDevice::createClient();

    Serial.printf("New client created\n");

    clientCallbacks = new ClientCallbacks(this);

    pClient->setClientCallbacks(clientCallbacks);

    pClient->setConnectionParams(12, 12, 0, 150);

    pClient->setConnectTimeout(5 * 1000);

    if (!pClient->connect(advDevice)) {
      BLEDevice::deleteClient(pClient);
      Serial.printf("Failed to connect, deleted client\n");

      return false;
    }
  }

  if (!pClient->isConnected()) {
    if (!pClient->connect(advDevice)) {
      Serial.printf("Failed to connect\n");
      return false;
    }
  }

  Serial.printf("Connected to: %s RSSI: %d\n",
                pClient->getPeerAddress().toString().c_str(),
                pClient->getRssi());

  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.printf("Failed to find service with UUID: %s\n",
                  serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println("Found service");

  commandChar = pRemoteService->getCharacteristic(commandUUID);
  if (commandChar == nullptr) {
    Serial.printf("Failed to find Command Characteristic with UUID: %s\n",
                  commandUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println("Found Command Characteristic");

  weightChar = pRemoteService->getCharacteristic(weightUUID);
  if (!weightChar->canRead()) {
    Serial.printf("Failed to read from Weight Characteristic with UUID: %s\n",
                  weightUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println("Found Weight Characteristic");

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
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);
  pScan->start(SCAN_TIME_MS);
}

void ScaleManager::update() {
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Successfully connected to scale");
    }
  }
}

byte TARE[6] = {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};
byte START_TIMER[6] = {0x03, 0x0a, 0x04, 0x00, 0x00, 0x0a};
byte STOP_TIMER[6] = {0x03, 0x0a, 0x05, 0x00, 0x00, 0x0d};
byte RESET_TIMER[6] = {0x03, 0x0a, 0x06, 0x00, 0x00, 0x0c};
byte START_AND_TARE[6] = {0x03, 0x0a, 0x07, 0x00, 0x00, 0x00};

bool ScaleManager::tare() {
  if (commandChar->writeValue(TARE)) {
    Serial.println("Tared Successfully");
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::startTimer() {
  if (commandChar->writeValue(START_TIMER)) {
    Serial.println("Tared Successfully");
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::stopTimer() {
  if (commandChar->writeValue(STOP_TIMER)) {
    Serial.println("Stopped Timer Successfully");
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::resetTimer() {
  if (commandChar->writeValue(RESET_TIMER)) {
    Serial.println("Reset Timer Successfully");
    return true;
  } else {
    return false;
  }
};

bool ScaleManager::startAndTare() {
  if (commandChar->writeValue(START_AND_TARE)) {
    Serial.println("Started Timer and Tared Successfully");
    return true;
  } else {
    return false;
  }
};