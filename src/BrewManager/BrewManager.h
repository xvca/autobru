#ifndef BREW_MANAGER_H
#define BREW_MANAGER_H

#include "ScaleManager.h"
#include <Arduino.h>

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
  BrewManager();
  static BrewManager *instance;

  // Pin definitions
  static const uint8_t MANUAL_PIN = 1;
  static const uint8_t TWO_CUP_PIN = 2;
  static const uint8_t ONE_CUP_PIN = 3;
  static const uint8_t BREW_SWITCH_PIN = 4;

  static float targetWeight;

  BrewState state = IDLE;
  PreinfusionMode pMode = SIMPLE;

  static const unsigned int MAX_SHOT_DURATION = 50 * 1000;

  static const unsigned long DEBOUNCE_DELAY = 250;
  unsigned long lastDebounceTime = 0;

  static const unsigned long DRIP_SETTLE_TIME = 10 * 1000;

  unsigned long brewStartTime = 0;
  unsigned long dripStartTime = 0;

  void handleSimplePreinfusion();
  void handleWeightTriggeredPreinfusion();

  void triggerBrewSwitch(int duration);
  bool isPressed(uint8_t button);

  ScaleManager *sManager;

  void setState(BrewState newState) { state = newState; }

public:
  static BrewManager *getInstance() {
    if (!instance) {
      instance = new BrewManager();
    }
    return instance;
  }

  void begin();
  void update();

  BrewState getState() const { return state; }
  PreinfusionMode getPreinfusionMode() const { return pMode; }

  bool startBrew();
  bool stopBrew();
  bool isBrewing() const { return state != IDLE; }

  unsigned long getBrewTime();
};

#endif