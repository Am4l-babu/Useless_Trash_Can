// =============================================================================
//  sensors.h - all of BIN-CHAD's perception, such as it is.
//
//  Two VL53L0X time-of-flight sensors:
//    THROAT   - points straight down the bin opening. Detects the object
//               arriving, and doubles as the hand-safety sensor for the lid.
//    APPROACH - points out into the room. Detects a victim.
//
//  Plus: two lid limit switches, the NORMAL MODE switch, the hidden demo
//  trigger, and an optional IR break-beam that backs up the throat ToF.
//
//  Everything here is non-blocking and degrades instead of failing. A dead
//  sensor sets a flag; the behaviour layer keeps running without it.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

// ---------------------------------------------------------------------------
// Small debounced digital input. Active LOW (INPUT_PULLUP, switch to GND).
// ---------------------------------------------------------------------------
class DebouncedInput {
public:
    void begin(uint8_t pin, uint16_t debounceMs = SWITCH_DEBOUNCE_MS);
    void update();
    bool isActive()   const { return _stable; }          // held down / closed
    bool justPressed()      { bool r = _rose;  _rose  = false; return r; }
    bool justReleased()     { bool r = _fell;  _fell  = false; return r; }
    uint32_t stableForMs() const { return millis() - _stableSince; }
private:
    uint8_t  _pin        = 255;
    uint16_t _debounceMs = SWITCH_DEBOUNCE_MS;
    bool     _stable     = false;
    bool     _raw        = false;
    bool     _rose       = false;
    bool     _fell       = false;
    uint32_t _changedAt  = 0;
    uint32_t _stableSince= 0;
};

// ---------------------------------------------------------------------------
// Health of one ToF channel.
// ---------------------------------------------------------------------------
enum SensorHealth : uint8_t {
    SENSOR_ABSENT = 0,   // never came up at boot
    SENSOR_OK     = 1,
    SENSOR_DEGRADED = 2  // was working, now failing reads
};

class Sensors {
public:
    // Brings up I2C and both ToF sensors. Returns true if at least the
    // throat sensor is alive - that is the minimum for a usable demo.
    bool begin();

    // Call every loop(). Rate-limited internally to SENSOR_POLL_MS.
    void update();

    // ---- ranges (millimetres; TOF_INVALID_MM when unavailable) -----------
    uint16_t throatMm()   const { return _throatMm; }
    uint16_t approachMm() const { return _approachMm; }

    // ---- derived, debounced booleans -------------------------------------
    bool objectInThroat() const { return _objectInThroat; }
    bool objectJustEntered();              // one-shot edge
    bool personNear()     const { return _personNear; }
    bool personJustArrived();              // one-shot edge

    // Hard safety: something is inside the closing zone right now.
    // Returns true if EITHER the ToF or the IR beam says so - the safety
    // path is deliberately OR-ed, never AND-ed.
    bool handInDangerZone() const;

    // ---- switches ---------------------------------------------------------
    bool lidAtClosedSwitch() const { return _limClosed.isActive(); }
    bool lidAtOpenSwitch()   const { return _limOpen.isActive(); }
    bool normalSwitchOn()    const { return _normalSw.isActive(); }
    bool normalSwitchJustTurnedOn()  { return _normalSw.justPressed(); }
    bool normalSwitchJustTurnedOff() { return _normalSw.justReleased(); }
    bool hiddenTriggerPressed()      { return _hidden.justPressed(); }

    // ---- health -----------------------------------------------------------
    SensorHealth throatHealth()   const { return _throatHealth; }
    SensorHealth approachHealth() const { return _approachHealth; }
    bool anySensorDegraded() const;
    const char *healthSummary() const;   // for the self-test screen

private:
    void     pollThroat();
    void     pollApproach();
    uint16_t readOne(void *lox, SensorHealth &health, uint8_t &failCount);

    uint32_t _lastPoll = 0;

    uint16_t _throatMm   = TOF_INVALID_MM;
    uint16_t _approachMm = TOF_INVALID_MM;

    uint8_t  _throatFails = 0, _approachFails = 0;
    SensorHealth _throatHealth   = SENSOR_ABSENT;
    SensorHealth _approachHealth = SENSOR_ABSENT;

    uint8_t _throatHits = 0, _throatMisses = 0;
    uint8_t _nearHits = 0, _farHits = 0;

    bool _objectInThroat = false, _objectEdge = false;
    bool _personNear     = false, _personEdge = false;

    DebouncedInput _limClosed, _limOpen, _normalSw, _hidden, _irThroat;
};

extern Sensors sensors;
