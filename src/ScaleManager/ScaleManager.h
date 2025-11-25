#ifndef SCALE_MANAGER_H
#define SCALE_MANAGER_H

#include "BrewManager.h"
#include "debug.h"
#include <NimBLEDevice.h>
#include <atomic>
#include <mutex>
#include <numeric>

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

struct FlowPoint {
  float timeSecs;
  float weight;
};

class BrewManager;
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

  void disconnectScale();
  void connectScale();

  void cleanUpConnectionState();

  bool preScanning() const { return shouldScan; }
  bool isScanning() const { return (pScan != nullptr) && pScan->isScanning(); }
  bool isConnecting() const { return shouldConnect; }
  bool isConnected() const { return connected; }

  float getWeight() const { return latestWeight.load(); }
  uint32_t getTime() const { return latestTime.load(); }
  float getFlowRate() const { return latestFlowRate.load(); }
  uint32_t getLastPacketTime() const { return lastPacketTime.load(); }

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
  bool beep();

private:
  ScaleManager();
  ScaleManager(const ScaleManager &) = delete;
  ScaleManager &operator=(const ScaleManager &) = delete;

  static constexpr int SCAN_TIME_MS = 5000;
  static constexpr int NOTIFICATION_INTERVAL = 20;
  static constexpr int CONNECTION_TIMEOUT_MS = 2000;

  static ScaleManager *instance;

  std::mutex scaleMutex;

  std::atomic<float> latestWeight{0.0f};
  std::atomic<uint32_t> latestTime{0};
  std::atomic<float> latestFlowRate{0.0f};
  std::atomic<uint32_t> lastPacketTime{0};

  // flow tracking
  // number of samples to hold in our flow history
  static const size_t FLOW_WINDOW_SIZE = 20;
  FlowPoint flowBuffer[FLOW_WINDOW_SIZE] = {};
  size_t bufHead = 0;
  size_t bufCount = 0;

  void resetFlowBuffer();
  float calculateLinearRegressionFlow();

  void setUpPendingTare();

  bool connectToServer();

  static void notifyCallback(NimBLERemoteCharacteristic *pRemoteCharacteristic,
                             uint8_t *pData, size_t length, bool isNotify);

  ScaleData parseScaleData(const uint8_t *data, size_t length);
  void printScaleData(const ScaleData &data);

  NimBLEClient *pClient;
  NimBLEScan *pScan;

  NimBLEAddress targetAddress = NimBLEAddress();

  NimBLERemoteCharacteristic *commandChar;
  NimBLERemoteCharacteristic *weightChar;

  ClientCallbacks *clientCallbacks;
  ScanCallbacks *scanCallbacks;

  std::atomic<bool> shouldScan{false};
  std::atomic<bool> shouldConnect{false};
  std::atomic<bool> connected{false};

  std::atomic<bool> tarePending{false};
  std::atomic<uint32_t> tareRequestTime{0};

  BrewManager *bManager;

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