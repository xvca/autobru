#ifndef BREW_MANAGER_H
#define BREW_MANAGER_H

#include "MachineController.h"
#include "ScaleManager.h"
#include <Arduino.h>
#include <Preferences.h>

class ScaleManager;

static constexpr int MAX_STORED_SHOTS = 12;
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
 * WEIGHT_TRIGGERED -> Preinfuses until weight detected on scale
 */
enum PreinfusionMode { SIMPLE, WEIGHT_TRIGGERED };

struct BrewPrefs {
  bool isEnabled;
  float regularPreset;
  float decafPreset;
  PreinfusionMode pMode;
  String timezone;
  int decafStartHour;
};

struct Shot {
  uint32_t id;
  float targetWeight;
  float finalWeight;
  float lastFlowRate;
  float stopWeight;
};

class BrewManager {
private:
  BrewManager() { loadSettings(); };
  BrewManager(const BrewManager &) = delete;
  BrewManager &operator=(const BrewManager &) = delete;

  static BrewManager *instance;

  // deps
  MachineController machine;
  ScaleManager *sManager;
  Preferences preferences;

  // state
  bool enabled = true;
  bool active = false;
  bool waitingForMacro = false;

  uint32_t globalShotCounter = 0;

  BrewState state = IDLE;
  PreinfusionMode pMode;

  // brew data
  float targetWeight;
  float currentWeight;
  float lastFlowRate;
  float stopWeight;

  ulong brewStartTime = 0;
  ulong brewEndTime = 0;
  ulong lastActiveTime = 0;

  // beep stuff
  int pendingBeeps = 0;
  ulong lastBeepTime = 0;

  // constants
  static const uint ACTIVITY_TIMEOUT = 10 * 60 * 1000;
  static const uint MAX_SHOT_DURATION = 60 * 1000;
  static constexpr float LEARNING_RATE = 0.2;
  static constexpr float MIN_FLOW_COMP = 0.2;
  static constexpr float MAX_FLOW_COMP = 2.5;
  static constexpr ulong DRIP_SETTLE_TIME = 10 * 1000;

  // threshold to decide between profile 0 (split shots) and profile 1 (full)
  static constexpr float PROFILE_THRESHOLD_WEIGHT = 28.0f;

  // settings
  float regularPreset;
  float decafPreset;

  String timezone = "GMT0";
  int decafStartHour = -1;

  // seperate history for each profile to prevent learning pollution
  Shot recentShotsProfile0[MAX_STORED_SHOTS];
  Shot recentShotsProfile1[MAX_STORED_SHOTS];

  float flowCompFactors[2];
  int currentProfileIndex = 1;

  // helpers
  void computeCompFactor();
  void computeCompFactorFromScratch(int profileIdx);
  void loadSettings();
  void saveSettings();
  void finalizeBrew();

  bool isDecafTime();

  // internal state handlers
  void handleIdleState();
  void handleActiveState();

public:
  static BrewManager *getInstance() {
    if (instance == nullptr) {
      instance = new BrewManager();
    }
    return instance;
  }

  void begin();
  void update();

  // api

  // startBrew called by API or logic
  // if shouldTriggerRelay = false, assume the button is already being pressed
  // by the user/macro
  bool startBrew(float target, bool shouldTriggerRelay = false);

  // user pressed a button, cancelling brew, doesn't trigger relay
  bool abortBrew(bool shouldTriggerRelay = false);

  // target weight or other finishing condition reached. stop tracking and
  // trigger relay  to stop brewing
  bool finishBrew();

  void wake();
  bool isActive() { return active; }
  bool isBrewing() const { return state != IDLE; }
  bool isEnabled() const { return enabled; }
  BrewState getState() const { return state; }
  float getTargetWeight() const { return targetWeight; }
  ulong getBrewTime();

  void clearShotData();
  bool deleteShotById(uint32_t id);
  void recalculateCompFactor();

  BrewPrefs getPrefs();
  void setPrefs(BrewPrefs prefs);

  Shot *getRecentShots(int profileIndex);
  float getFlowCompFactor(int profileIndex);

  void syncTimezone();
};

#endif
