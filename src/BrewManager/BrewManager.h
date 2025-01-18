#ifndef BREW_MANAGER_H
#define BREW_MANAGER_H

#include "ScaleManager.h"
#include <Arduino.h>
#include <Preferences.h>

/**
 * IDLE         -> Waiting for user input
 * PREINFUSION  -> Brew switch being held for manual-duration preinfusion
 * (only relevant for WEIGHT_TRIGGERED preinfusion mode)
 * BREWING      -> Main brewing stage
 * DRIPPING     -> Post-brew stage where weight is still being measured
 */
enum BrewState { IDLE, PREINFUSION, BREWING, DRIPPING };

/**
 * SIMPLE           -> Machine defined preinfusion duration
 * WEIGHT_TRIGGERED -> Preinfuses until 1g detected on scale
 */
enum PreinfusionMode { SIMPLE, WEIGHT_TRIGGERED };

class BrewManager {
private:
  BrewManager() { loadSettings(); };
  BrewManager(const BrewManager &) = delete;
  BrewManager &operator=(const BrewManager &) = delete;

  static BrewManager *instance;

  // Pin definitions
  static constexpr uint8_t MANUAL_PIN = 5;
  static constexpr uint8_t TWO_CUP_PIN = 6;
  static constexpr uint8_t ONE_CUP_PIN = 7;
  static constexpr uint8_t BREW_SWITCH_PIN = 8;

  float targetWeight;
  float currentWeight;
  float finalFlowRate;

  BrewState state = IDLE;
  PreinfusionMode pMode;

  static const unsigned int MAX_SHOT_DURATION = 50 * 1000;

  static const unsigned long DEBOUNCE_DELAY = 500;
  unsigned long lastDebounceTime = 0;

  unsigned long brewStartTime = 0;
  unsigned long brewEndTime = 0;

  void handleSimplePreinfusion();
  void handleWeightTriggeredPreinfusion();

  void triggerBrewSwitch(int duration);
  bool isPressed(uint8_t button);

  ScaleManager *sManager;

  void setState(BrewState newState) { state = newState; }

  Preferences preferences;

  static constexpr int MAX_STORED_SHOTS = 10;
  static constexpr float LEARNING_RATE = 0.2;
  static constexpr float MIN_FLOW_COMP = 0.2;
  static constexpr float MAX_FLOW_COMP = 2.0;
  static constexpr unsigned long DRIP_SETTLE_TIME = 10 * 1000;

  float preset1;
  float preset2;

  struct Shot {
    float targetWeight;
    float finalWeight;
    float lastFlowRate;
  };

  Shot recentShots[MAX_STORED_SHOTS];
  int currentShotIndex = 0;

  void loadSettings();
  void saveSettings();

  void finalizeBrew();

  /**
   * Auto-adjusting factor - when multiplied by flow rate determines when to
   * stop the brew
   */
  float flowCompFactor;

public:
  static BrewManager *getInstance() {
    if (instance == nullptr) {
      instance = new BrewManager();
    }
    return instance;
  }

  void begin();
  void update();

  BrewState getState() const { return state; }
  PreinfusionMode getPreinfusionMode() const { return pMode; }

  void setPreset1(float target);
  void setPreset2(float target);
  void setPreinfusionMode(PreinfusionMode pMode);
  void clearShotData();

  bool startBrew(float target, bool triggerBrew);
  bool stopBrew();
  bool isBrewing() const { return state != IDLE; }

  unsigned long getBrewTime();
};

#endif
