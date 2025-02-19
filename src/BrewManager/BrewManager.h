#ifndef BREW_MANAGER_H
#define BREW_MANAGER_H

#include "ScaleManager.h"
#include <Arduino.h>
#include <Preferences.h>

class ScaleManager;

static constexpr int MAX_STORED_SHOTS = 10;
/**
 * From personal experience using a spouted portafilter, flow comp would reach
 * equilibrium at around 1.3
 */
static constexpr float DEFAULT_FLOW_COMP = 1.3;

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

struct BrewPrefs {
  bool isEnabled;
  float preset1;
  float preset2;
  PreinfusionMode pMode;
};

struct Shot {
  float targetWeight;
  float finalWeight;
  float lastFlowRate;
};

class BrewManager {
private:
  BrewManager() { loadSettings(); };
  BrewManager(const BrewManager &) = delete;
  BrewManager &operator=(const BrewManager &) = delete;

  static BrewManager *instance;

  bool enabled = true;
  bool active = false;

  ulong lastActiveTime = 0;

  // Pin definitions
#ifdef DEBUG_BUILD
  static constexpr uint8_t MANUAL_PIN = 25;
  static constexpr uint8_t TWO_CUP_PIN = 26;
  static constexpr uint8_t ONE_CUP_PIN = 32;
  static constexpr uint8_t BREW_SWITCH_PIN = 33;
#else
  static constexpr uint8_t MANUAL_PIN = 1;
  static constexpr uint8_t TWO_CUP_PIN = 2;
  static constexpr uint8_t ONE_CUP_PIN = 3;
  static constexpr uint8_t BREW_SWITCH_PIN = 4;
#endif

  float targetWeight;
  float currentWeight;
  float finalFlowRate;

  BrewState state = IDLE;
  PreinfusionMode pMode;

  static const unsigned int ACTIVITY_TIMEOUT = 10 * 60 * 1000;
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

  static constexpr float LEARNING_RATE = 0.2;
  static constexpr float MIN_FLOW_COMP = 0.5;
  static constexpr float MAX_FLOW_COMP = 5.0;
  static constexpr unsigned long DRIP_SETTLE_TIME = 10 * 1000;

  float preset1;
  float preset2;

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

  void clearShotData();

  void wake();

  bool isActive() { return active; }

  bool startBrew(float target = 40, bool triggerBrew = true);
  bool stopBrew();
  bool isBrewing() const { return state != IDLE; }

  bool isEnabled() const { return enabled; }

  BrewPrefs getPrefs();
  void setPrefs(BrewPrefs prefs);

  unsigned long getBrewTime();

  Shot *getRecentShots() { return recentShots; }
  float getFlowCompFactor() { return flowCompFactor; }
};

#endif
