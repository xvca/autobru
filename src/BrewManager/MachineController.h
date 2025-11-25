#ifndef MACHINE_CONTROLLER_H
#define MACHINE_CONTROLLER_H

#include <Arduino.h>

struct DebouncedButton {
  uint8_t pin;
  bool stableState = true;
  bool lastRawState = true;
  bool fellEdge = false;
  bool roseEdge = false;
  uint32_t lastChangeMs = 0;
};

class MachineController {
public:
  void begin();
  void update();

  // input queries
  bool isManualStart() const { return manualBtn.fellEdge; }
  bool isOneCupStart() const { return oneCupBtn.fellEdge; }
  bool isTwoCupStart() const { return twoCupBtn.fellEdge; }

  bool isStopPressed() const {
    return manualBtn.fellEdge || oneCupBtn.fellEdge || twoCupBtn.fellEdge;
  }

  bool isManualReleased() const { return manualBtn.roseEdge; }

  // output commands
  void clickRelay();
  void holdRelay();
  void releaseRelay();

  // macros
  void startPreinfusionMacro();
  bool isMacroComplete();

  void stopFromPreinfusion();

private:
  void updateButton(DebouncedButton &btn);

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

  static constexpr ulong BUTTON_DEBOUNCE_TIME = 50;
  static constexpr ulong RELAY_PULSE_TIME = 100;

  DebouncedButton manualBtn;
  DebouncedButton oneCupBtn;
  DebouncedButton twoCupBtn;

  // relay state
  bool relayActive = false;
  bool relayLatching = false;
  uint32_t relayReleaseTime = 0;

  // start macro state
  bool macroRunning = false;
  bool macroFinished = false;
  uint8_t macroStep = 0;
  uint32_t macroNextActionTime = 0;

  // stop macro state
  bool stopSequenceRunning = false;
  uint32_t stopSequenceStepTime = 0;
};

#endif
