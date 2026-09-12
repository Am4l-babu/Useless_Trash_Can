#include "sensors.h"
#include "../config/pins.h"
#include <Wire.h>
#include <Adafruit_VL53L0X.h>

Sensors sensors;

// Both sensors power up at 0x29. We hold each in reset via XSHUT and bring
// them up one at a time, re-addressing the first before releasing the second.
static const uint8_t TOF_ADDR_THROAT   = 0x30;
static const uint8_t TOF_ADDR_APPROACH = 0x31;

static Adafruit_VL53L0X loxThroat;
static Adafruit_VL53L0X loxApproach;

// ===========================================================================
//  DebouncedInput
// ===========================================================================
void DebouncedInput::begin(uint8_t pin, uint16_t debounceMs) {
    _pin        = pin;
    _debounceMs = debounceMs;
    pinMode(_pin, INPUT_PULLUP);
    _raw = _stable = (digitalRead(_pin) == LOW);
    _changedAt = _stableSince = millis();
    _rose = _fell = false;
}

void DebouncedInput::update() {
    if (_pin == 255) return;
    const bool now = (digitalRead(_pin) == LOW);
    if (now != _raw) {
        _raw = now;
        _changedAt = millis();
        return;
    }
    if (now != _stable && (millis() - _changedAt) >= _debounceMs) {
        _stable = now;
        _stableSince = millis();
        if (_stable) _rose = true; else _fell = true;
    }
}

// ===========================================================================
//  Bring-up
// ===========================================================================
bool Sensors::begin() {
    _limClosed.begin(PIN_LIMIT_LID_CLOSED);
    _limOpen.begin(PIN_LIMIT_LID_OPEN);
    _normalSw.begin(PIN_SWITCH_NORMAL_MODE, 60);   // big switch, slower bounce
    _hidden.begin(PIN_HIDDEN_TRIGGER);
#if USE_IR_THROAT
    _irThroat.begin(PIN_IR_THROAT, 12);            // beam break must be quick
#endif

    pinMode(PIN_TOF_THROAT_XSHUT, OUTPUT);
    pinMode(PIN_TOF_APPROACH_XSHUT, OUTPUT);
    digitalWrite(PIN_TOF_THROAT_XSHUT, LOW);       // hold both in reset
    digitalWrite(PIN_TOF_APPROACH_XSHUT, LOW);
    delay(12);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);

    // --- throat sensor first ---------------------------------------------
    digitalWrite(PIN_TOF_THROAT_XSHUT, HIGH);
    delay(12);
    if (loxThroat.begin(TOF_ADDR_THROAT, false, &Wire)) {
        loxThroat.startRangeContinuous(SENSOR_POLL_MS);
        _throatHealth = SENSOR_OK;
    } else {
        Serial.println(F("[sensors] THROAT ToF did not answer"));
        _throatHealth = SENSOR_ABSENT;
    }

    // --- approach sensor --------------------------------------------------
#if USE_TOF_APPROACH
    digitalWrite(PIN_TOF_APPROACH_XSHUT, HIGH);
    delay(12);
    if (loxApproach.begin(TOF_ADDR_APPROACH, false, &Wire)) {
        loxApproach.startRangeContinuous(SENSOR_POLL_MS);
        _approachHealth = SENSOR_OK;
    } else {
        Serial.println(F("[sensors] APPROACH ToF did not answer"));
        _approachHealth = SENSOR_ABSENT;
    }
#endif

    // The throat sensor is the only one the demo genuinely needs. Without
    // the approach sensor the bin simply stops noticing people early.
    return _throatHealth == SENSOR_OK;
}

// ===========================================================================
//  Polling
// ===========================================================================
// Returns TOF_NO_SAMPLE when the sensor simply has not finished a
// measurement yet - that is NOT the same as "nothing in range", and
// conflating the two makes the throat flicker at the poll rate.
uint16_t Sensors::readOne(void *loxv, SensorHealth &health, uint8_t &failCount) {
    Adafruit_VL53L0X *lox = (Adafruit_VL53L0X *)loxv;
    if (health == SENSOR_ABSENT) return TOF_NO_SAMPLE;
    if (!lox->isRangeComplete()) return TOF_NO_SAMPLE;

    const uint16_t mm = lox->readRangeResult();
    const uint8_t  st = lox->readRangeStatus();

    // Status 0 = good measurement.
    if (st == 0 && mm > 0 && mm < TOF_INVALID_MM) {
        failCount = 0;
        if (health == SENSOR_DEGRADED) health = SENSOR_OK;
        return mm;
    }
    // Status 4 = phase fail, which in practice means "nothing out there".
    // A valid answer, not a fault.
    if (st == 4) {
        failCount = 0;
        return TOF_INVALID_MM;
    }
    if (failCount < 255) failCount++;
    if (failCount >= SENSOR_FAIL_LIMIT) health = SENSOR_DEGRADED;
    return TOF_NO_SAMPLE;
}

void Sensors::pollThroat() {
    const uint16_t mm = readOne(&loxThroat, _throatHealth, _throatFails);
    if (mm == TOF_NO_SAMPLE) return;          // keep the previous reading
    _throatMm = mm;

    // Hysteresis so a wobbling crisp packet does not machine-gun the state
    // machine: N consecutive samples to assert, N to release.
    const bool close = (_throatMm < TOF_THROAT_OBJECT_MM);
    const bool clear = (_throatMm > TOF_THROAT_CLEAR_MM);

    if (close)      { _throatMisses = 0; if (_throatHits   < 255) _throatHits++; }
    else if (clear) { _throatHits   = 0; if (_throatMisses < 255) _throatMisses++; }

    if (!_objectInThroat && _throatHits >= TOF_CONFIRM_SAMPLES) {
        _objectInThroat = true;
        _objectEdge = true;
    } else if (_objectInThroat && _throatMisses >= TOF_CONFIRM_SAMPLES) {
        _objectInThroat = false;
    }
}

void Sensors::pollApproach() {
#if USE_TOF_APPROACH
    const uint16_t mm = readOne(&loxApproach, _approachHealth, _approachFails);
    if (mm == TOF_NO_SAMPLE) return;
    _approachMm = mm;

    const bool near = (_approachMm < TOF_APPROACH_NEAR_MM);
    const bool far  = (_approachMm > TOF_APPROACH_FAR_MM);   // TOF_INVALID_MM counts as far

    if (near)      { _farHits  = 0; if (_nearHits < 255) _nearHits++; }
    else if (far)  { _nearHits = 0; if (_farHits  < 255) _farHits++; }

    if (!_personNear && _nearHits >= TOF_CONFIRM_SAMPLES) {
        _personNear = true;
        _personEdge = true;
    } else if (_personNear && _farHits >= TOF_CONFIRM_SAMPLES) {
        _personNear = false;
    }
#endif
}

void Sensors::update() {
    // Switches are cheap and safety-relevant - poll them every single loop.
    _limClosed.update();
    _limOpen.update();
    _normalSw.update();
    _hidden.update();
#if USE_IR_THROAT
    _irThroat.update();
#endif

    if (millis() - _lastPoll < SENSOR_POLL_MS) return;
    _lastPoll = millis();

    pollThroat();
    pollApproach();
}

// ===========================================================================
//  Derived state
// ===========================================================================
bool Sensors::objectJustEntered() { bool e = _objectEdge; _objectEdge = false; return e; }
bool Sensors::personJustArrived() { bool e = _personEdge; _personEdge = false; return e; }

bool Sensors::handInDangerZone() const {
    // OR-ed on purpose. If either channel thinks there is a hand in there,
    // there is a hand in there. Comedy never gets a vote on this function.
#if USE_IR_THROAT
    if (_irThroat.isActive()) return true;
#endif
    if (_throatHealth == SENSOR_OK &&
        _throatMm != TOF_INVALID_MM &&
        _throatMm < SAFETY_HAND_MM) return true;
    return false;
}

bool Sensors::anySensorDegraded() const {
    return _throatHealth != SENSOR_OK || _approachHealth == SENSOR_DEGRADED;
}

const char *Sensors::healthSummary() const {
    if (_throatHealth == SENSOR_OK && _approachHealth == SENSOR_OK) return "OK";
    if (_throatHealth == SENSOR_OK) return "PARTIAL";
    return "BLIND";
}
