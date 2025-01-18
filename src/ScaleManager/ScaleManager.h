#ifndef SCALE_MANAGER_H
#define SCALE_MANAGER_H

#include <NimBLEDevice.h>
#include <atomic>

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

class ScaleManager;
class ScanCallbacks;
class ClientCallbacks;

class ScaleManager : NimBLEScanCallbacks, NimBLEClientCallbacks {
public:
  static ScaleManager *getInstance() {
    if (instance == nullptr) {
      instance = new ScaleManager();
    }
    return instance;
  }

  void begin();
  void update();

  bool isConnected() const { return connected; }

  float getWeight() const { return latestWeight.load(); }
  float getTime() const { return latestTime.load(); }
  float getFlowRate() const { return latestFlowRate.load(); }

  void onClientConnect();
  void onClientConnectFail(int reason);
  void onClientDisconnect(int reason);

  void onScanResult(const NimBLEAdvertisedDevice *advertisedDevice);
  void onScanEnd(const NimBLEScanResults &scanResults, int reason);

  bool tare();
  bool startTimer();
  bool stopTimer();
  bool resetTimer();
  bool startAndTare();

private:
  ScaleManager();
  ScaleManager(const ScaleManager &) = delete;
  ScaleManager &operator=(const ScaleManager &) = delete;

  static constexpr int SCAN_TIME_MS = 5000;
  static constexpr int NOTIFICATION_INTERVAL = 20;

  static ScaleManager *instance;

  std::atomic<float> latestWeight{0.0f};
  std::atomic<uint32_t> latestTime{0};
  std::atomic<float> latestFlowRate{0.0f};

  bool connectToServer();

  static void notifyCallback(NimBLERemoteCharacteristic *pRemoteCharacteristic,
                             uint8_t *pData, size_t length, bool isNotify);

  ScaleData parseScaleData(const uint8_t *data, size_t length);
  void printScaleData(const ScaleData &data);

  NimBLEClient *pClient;
  NimBLEScan *pScan;

  const NimBLEAdvertisedDevice *advDevice;

  NimBLERemoteCharacteristic *commandChar;
  NimBLERemoteCharacteristic *weightChar;

  ClientCallbacks *clientCallbacks;
  ScanCallbacks *scanCallbacks;

  bool doConnect = false;
  bool connected = false;
  ulong lastNotification = 0;

  static NimBLEUUID serviceUUID;
  static NimBLEUUID commandUUID;
  static NimBLEUUID weightUUID;

  friend class ScanCallbacks;
  friend class ClientCallbacks;
};

class ScanCallbacks : public NimBLEScanCallbacks {
public:
  ScanCallbacks(ScaleManager *manager) : sManager(manager) {}

  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) {
    if (sManager) {
      sManager->onScanResult(advertisedDevice);
    }
  }

  void onScanEnd(const NimBLEScanResults &scanResults, int reason) {
    if (sManager) {
      sManager->onScanEnd(scanResults, reason);
    }
  }

private:
  ScaleManager *sManager;
};

class ClientCallbacks : public NimBLEClientCallbacks {
public:
  ClientCallbacks(ScaleManager *manager) : sManager(manager) {}

  void onConnect(NimBLEClient *pClient) {
    if (sManager) {
      sManager->onClientConnect();
    }
  }

  void onConnectFail(NimBLEClient *pClient, int reason) {
    if (sManager) {
      sManager->onClientConnectFail(reason);
    }
  }

  void onDisconnect(NimBLEClient *pClient, int reason) {
    if (sManager) {
      sManager->onClientDisconnect(reason);
    }
  }

private:
  ScaleManager *sManager;
};

#endif