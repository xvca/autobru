#include "MachineController.h"

void MachineController::begin() {
  pinMode(MANUAL_PIN, INPUT_PULLUP);
  pinMode(ONE_CUP_PIN, INPUT_PULLUP);
  pinMode(TWO_CUP_PIN, INPUT_PULLUP);

  pinMode(BREW_SWITCH_PIN, OUTPUT);
  digitalWrite(BREW_SWITCH_PIN, LOW);

  manualBtn.pin = MANUAL_PIN;
  oneCupBtn.pin = ONE_CUP_PIN;
  twoCupBtn.pin = TWO_CUP_PIN;
}

void MachineController::update() {
  updateButton(manualBtn);
  updateButton(oneCupBtn);
  updateButton(twoCupBtn);

  if (relayActive && !relayLatching && millis() >= relayReleaseTime) {
    digitalWrite(BREW_SWITCH_PIN, LOW);
    relayActive = false;
  }

  if (macroRunning) {
    macroFinished = false;
    if (millis() >= macroStepTime) {
      macroRunning = false;
      holdRelay();
      macroFinished = true;
    }
  }

  if (stopSequenceRunning) {
    if (millis() >= stopSequenceStepTime) {
      stopSequenceRunning = false;
      clickRelay();
    }
  }
}

void MachineController::clickRelay() {
  digitalWrite(BREW_SWITCH_PIN, HIGH);
  relayActive = true;
  relayLatching = false;
  relayReleaseTime = millis() + RELAY_PULSE_TIME;
}

void MachineController::holdRelay() {
  digitalWrite(BREW_SWITCH_PIN, HIGH);
  relayActive = true;
  relayLatching = true;
}

void MachineController::releaseRelay() {
  digitalWrite(BREW_SWITCH_PIN, LOW);
  relayActive = false;
  relayLatching = false;
}

void MachineController::startPreinfusionMacro() {
  clickRelay();
  macroRunning = true;
  macroFinished = false;
  macroStepTime = millis() + 250;
}

bool MachineController::isMacroComplete() {
  if (macroFinished) {
    macroFinished = false;
    return true;
  }
  return false;
}

void MachineController::stopFromPreinfusion() {
  // release currently latched relay, lets machine go to full flow
  releaseRelay();
  stopSequenceRunning = true;
  stopSequenceStepTime = millis() + 150;
}

void MachineController::updateButton(DebouncedButton &btn) {
  bool raw = digitalRead(btn.pin);
  uint32_t now = millis();

  btn.fellEdge = false;
  btn.roseEdge = false;

  if (raw != btn.lastRawState) {
    btn.lastRawState = raw;
    btn.lastChangeMs = now;
  } else if ((now - btn.lastChangeMs) >= BUTTON_DEBOUNCE_TIME &&
             raw != btn.stableState) {
    btn.stableState = raw;
    if (!raw)
      btn.fellEdge = true;
    else
      btn.roseEdge = true;
  }
}
