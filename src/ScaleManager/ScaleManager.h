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

class ScaleManager {
public:
  ScaleManager();
  ~ScaleManager();

  void init();
  bool isConnected() const { return connected; }
  void onLoop();

  float getWeight() const { return latestWeight.load(); }

  void onClientConnect();
  void onClientConnectFail(int reason);
  void onClientDisconnect(int reason);

  void onScanResult(const BLEAdvertisedDevice *advertisedDevice);
  void onScanEnd(const BLEScanResults &scanResults, int reason);

  bool tare();
  bool startTimer();
  bool stopTimer();
  bool resetTimer();
  bool startAndTare();

private:
  static constexpr int SCAN_TIME_MS = 5000;
  static constexpr int NOTIFICATION_INTERVAL = 20;

  static ScaleManager *instance;

  std::atomic<float> latestWeight{0.0f};

  bool connectToServer();

  static void notifyCallback(BLERemoteCharacteristic *pRemoteCharacteristic,
                             uint8_t *pData, size_t length, bool isNotify);

  ScaleData parseScaleData(const uint8_t *data, size_t length);
  void printScaleData(const ScaleData &data);

  BLEClient *pClient;
  BLEScan *pScan;

  const BLEAdvertisedDevice *advDevice;

  BLERemoteCharacteristic *commandChar;
  BLERemoteCharacteristic *weightChar;

  ClientCallbacks *clientCallbacks;
  ScanCallbacks *scanCallbacks;

  bool doConnect = false;
  bool connected = false;
  ulong lastNotification = 0;

  static BLEUUID serviceUUID;
  static BLEUUID commandUUID;
  static BLEUUID weightUUID;

  friend class ScanCallbacks;
  friend class ClientCallbacks;
};

class ScanCallbacks : public NimBLEScanCallbacks {
public:
  ScanCallbacks(ScaleManager *manager) : sManager(manager) {}

  void onResult(const BLEAdvertisedDevice *advertisedDevice) {
    if (sManager) {
      sManager->onScanResult(advertisedDevice);
    }
  }

  void onScanEnd(const BLEScanResults &scanResults, int reason) {
    if (sManager) {
      sManager->onScanEnd(scanResults, reason);
    }
  }

private:
  ScaleManager *sManager;
};

class ClientCallbacks : public BLEClientCallbacks {
public:
  ClientCallbacks(ScaleManager *manager) : sManager(manager) {}

  void onConnect(BLEClient *pClient) {
    if (sManager) {
      sManager->onClientConnect();
    }
  }

  void onConnectFail(BLEClient *pClient, int reason) {
    if (sManager) {
      sManager->onClientConnectFail(reason);
    }
  }

  void onDisconnect(BLEClient *pClient, int reason) {
    if (sManager) {
      sManager->onClientDisconnect(reason);
    }
  }

private:
  ScaleManager *sManager;
};

#endif