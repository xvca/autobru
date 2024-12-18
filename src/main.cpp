#include <Arduino.h>
#include <NimBLEDevice.h>

const static int scanTimeMs = 5000;
const static int NOTIFICATION_INTERVAL = 100;

static NimBLEClient *pClient;
static const NimBLEAdvertisedDevice *advDevice;
static NimBLEScan *pScan;

static BLEUUID serviceUUID("0FFE");
static BLEUUID commandUUID("FF12");
static BLEUUID weightUUID("FF11");

static bool doConnect = false;
static bool connected = false;
static BLERemoteCharacteristic *commandChar;
static BLERemoteCharacteristic *weightChar;

unsigned long lastHeartBeat = 0;
unsigned long lastConnected = 0;

unsigned long lastNotification = 0;

struct ScaleData {
  uint32_t milliseconds;
  uint8_t weightUnit;
  float weightGrams;
  float flowRate;
  uint8_t batteryPercent;
  uint16_t standbyMinutes;
  uint8_t buzzerGear;
  uint8_t flowRateSmoothing;
};

ScaleData parseScaleData(const uint8_t *data, size_t length) {
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

void printScaleData(const ScaleData &data) {
  Serial.println(F("------ Data ------"));
  Serial.print(F("Time (ms): "));
  Serial.println(data.milliseconds);

  Serial.print(F("Weight (g): "));
  Serial.println(data.weightGrams, 2); // 2 decimal places

  Serial.print(F("Flow rate (g/s): "));
  Serial.println(data.flowRate, 2); // 2 decimal places

  Serial.print(F("Battery (%): "));
  Serial.println(data.batteryPercent);
  Serial.println(F("----------------------"));
}

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *pClient) override {
    doConnect = false;
    connected = true;
  }

  void onConnectFail(NimBLEClient *pClient, int reason) override {
    Serial.printf("%s Failed to connect, reason = %d - Starting scan\n",
                  pClient->getPeerAddress().toString().c_str(), reason);

    doConnect = false;
    advDevice = nullptr;

    // start scanning again
    pScan->start(scanTimeMs, false, true);
  }

  void onDisconnect(NimBLEClient *pClient, int reason) override {
    Serial.printf("%s Disconnected, reason = %d - Starting scan\n",
                  pClient->getPeerAddress().toString().c_str(), reason);

    connected = false;
    doConnect = false;

    advDevice = nullptr;

    weightChar->unsubscribe();

    commandChar = nullptr;
    weightChar = nullptr;

    // start scanning again
    pScan->start(scanTimeMs, false, true);
  }
} clientCallbacks;

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    Serial.printf("Advertised Device found: %s\n",
                  advertisedDevice->toString().c_str());
    if (advertisedDevice->getName().length() &&
        advertisedDevice->getName().rfind("BOOKOO", 0) == 0) {
      Serial.printf("Found Our Scale\n");

      /** stop scan before connecting */
      pScan->stop();

      /** Save the device reference in a global for the client to use*/
      advDevice = advertisedDevice;

      /** Ready to connect now */
      doConnect = true;
    }
  }

  /** Callback to process the results of the completed scan or restart it */
  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    Serial.printf("Scan Ended, reason: %d, device count: %d; Restarting scan\n",
                  reason, results.getCount());
    pScan->start(scanTimeMs, false, true);
  }
} scanCallbacks;

/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
              size_t length, bool isNotify) {

  uint32_t currentTime = millis();

  if (currentTime - lastNotification < NOTIFICATION_INTERVAL) {
    return;
  }

  lastNotification = currentTime;

  std::string str = (isNotify == true) ? "Notification" : "Indication";
  str += " from ";
  str += pRemoteCharacteristic->getClient()->getPeerAddress().toString();
  str += ": Service = " +
         pRemoteCharacteristic->getRemoteService()->getUUID().toString();
  str += ", Characteristic = " + pRemoteCharacteristic->getUUID().toString();

  ScaleData sData = parseScaleData(pData, length);
  printScaleData(sData);
}

/** Handles the provisioning of clients and connects / interfaces with the
 * server */
bool connectToServer() {

  /** Check if we have a client we should reuse first **/
  if (pClient != nullptr) {
    /**
     *  Special case when we already know this device, we send false as the
     *  second argument in connect() to prevent refreshing the service
     * database. This saves considerable time and power.
     */
    pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
    if (pClient) {
      if (!pClient->connect(advDevice, false)) {
        Serial.printf("Reconnect failed\n");
        doConnect = false;
        return false;
      }
      Serial.printf("Reconnected client\n");
    } else {
      /**
       *  We don't already have a client that knows this device,
       *  check for a client that is disconnected that we can use.
       */
      pClient = NimBLEDevice::getDisconnectedClient();
    }
  }

  /** No client to reuse? Create a new one. */
  if (!pClient) {
    if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
      Serial.printf("Max clients reached - no more connections available\n");
      return false;
    }

    pClient = NimBLEDevice::createClient();

    Serial.printf("New client created\n");

    pClient->setClientCallbacks(&clientCallbacks, false);

    /**
     *  Set initial connection parameters:
     *  These settings are safe for 3 clients to connect reliably, can go faster
     * if you have less connections. Timeout should be a multiple of the
     * interval, minimum is 100ms. Min interval: 12 * 1.25ms = 15, Max interval:
     * 12 * 1.25ms = 15, 0 latency, 150 * 10ms = 1500ms timeout
     */
    pClient->setConnectionParams(12, 12, 0, 150);

    /** Set how long we are willing to wait for the connection to complete
     * (milliseconds), default is 30000. */
    pClient->setConnectTimeout(5 * 1000);

    if (!pClient->connect(advDevice)) {
      /** Created a client but failed to connect, don't need to keep it as it
       * has no data */
      NimBLEDevice::deleteClient(pClient);
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
    weightChar->subscribe(true, notifyCB);

  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("Starting NimBLE Client\n");

  NimBLEDevice::init("autobru-client");

  pScan = NimBLEDevice::getScan();

  pScan->setScanCallbacks(&scanCallbacks, false);

  pScan->setInterval(1349);
  pScan->setWindow(449);

  pScan->setActiveScan(true);

  pScan->start(scanTimeMs);
  Serial.printf("Scanning for peripherals\n");
}

void loop() {
  delay(1000);

  if (doConnect) {
    if (connectToServer()) {
      Serial.printf("Success! we should now be getting notifications, scanning "
                    "for more!\n");
    }
  }
}
