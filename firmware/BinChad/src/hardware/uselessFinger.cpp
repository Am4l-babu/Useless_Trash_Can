#include "uselessFinger.h"
#include "sensors.h"
#include "../config/pins.h"
#include <ESP32Servo.h>
#include <math.h>

UselessFinger finger;

static Servo doorServo;
static Servo fingerServo;

static const uint16_t MICRO_PULSE_MIN_US = 500;
static const uint16_t MICRO_PULSE_MAX_US = 2400;

static inline float ease(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 0.5f * (1.0f - cosf((float)M_PI * t));
}

bool UselessFinger::begin() {
    doorServo.setPeriodHertz(50);
    fingerServo.setPeriodHertz(50);
    attachAll();

    _doorAngle   = _doorFrom   = _doorTo   = DOOR_ANGLE_SHUT;
    _fingerAngle = _fingerFrom = _fingerTo = FINGER_ANGLE_HOME;
    doorServo.write(_doorAngle);
    fingerServo.write(_fingerAngle);
    delay(300);
    detachAll();

    _state = FINGER_IDLE;
    _enteredAt = millis();
    return true;
}

void UselessFinger::attachAll() {
    if (_attached) return;
    doorServo.attach(PIN_SERVO_FINGER_DOOR, MICRO_PULSE_MIN_US, MICRO_PULSE_MAX_US);
    fingerServo.attach(PIN_SERVO_FINGER,    MICRO_PULSE_MIN_US, MICRO_PULSE_MAX_US);
    _attached = true;
}

void UselessFinger::detachAll() {
    if (!_attached) return;
    doorServo.detach();
    fingerServo.detach();
    _attached = false;
}

void UselessFinger::driveDoor(uint8_t angle)   { if (_attached) doorServo.write(angle); }
void UselessFinger::driveFinger(uint8_t angle) { if (_attached) fingerServo.write(angle); }

void UselessFinger::enter(FingerState s) {
    _state     = s;
    _enteredAt = millis();
    _lastStep  = 0;

    _doorFrom   = _doorAngle;
    _fingerFrom = _fingerAngle;

    switch (s) {
        case FINGER_DOOR_OPENING:
            _doorTo    = DOOR_ANGLE_OPEN;
            _segmentMs = FINGER_DOOR_MS;
            break;
        case FINGER_PAUSE:
            _segmentMs = FINGER_PAUSE_MS;      // nothing moves; that is the joke
            break;
        case FINGER_EXTENDING:
            _fingerTo  = FINGER_ANGLE_PRESS;
            _segmentMs = FINGER_EXTEND_MS;
            break;
        case FINGER_DWELL:
            _segmentMs = FINGER_DWELL_MS;
            break;
        case FINGER_RETRACTING:
            _fingerTo  = FINGER_ANGLE_HOME;
            _segmentMs = FINGER_RETRACT_MS;
            break;
        case FINGER_DOOR_CLOSING:
            _doorTo    = DOOR_ANGLE_SHUT;
            _segmentMs = FINGER_DOOR_MS;
            break;
        default:
            _segmentMs = 1;
            break;
    }
}

void UselessFinger::deploy() {
    if (isBusy()) return;
    _failEvent  = false;
    _attempt    = 0;
    _wiggleMode = false;
    attachAll();
    enter(FINGER_DOOR_OPENING);
}

void UselessFinger::wiggle() {
    if (isBusy()) return;
    _attempt    = 0;
    _wiggleMode = true;
    attachAll();
    enter(FINGER_DOOR_OPENING);
}

void UselessFinger::abort() {
    attachAll();
    _fingerAngle = FINGER_ANGLE_HOME;
    _doorAngle   = DOOR_ANGLE_SHUT;
    driveFinger(_fingerAngle);
    driveDoor(_doorAngle);
    _state = FINGER_IDLE;
    _enteredAt = millis();
}

bool UselessFinger::consumePressEvent() {
    const bool e = _pressEvent;
    _pressEvent = false;
    return e;
}

bool UselessFinger::consumeFailureEvent() {
    const bool e = _failEvent;
    _failEvent = false;
    return e;
}

void UselessFinger::stepServos() {
    const uint32_t elapsed = millis() - _enteredAt;
    const float t = ease(_segmentMs ? (float)elapsed / (float)_segmentMs : 1.0f);

    const uint8_t d = (uint8_t)((int16_t)_doorFrom +
        (int16_t)lroundf(t * ((float)_doorTo - (float)_doorFrom)));
    const uint8_t f = (uint8_t)((int16_t)_fingerFrom +
        (int16_t)lroundf(t * ((float)_fingerTo - (float)_fingerFrom)));

    if (d != _doorAngle)   { _doorAngle = d;   driveDoor(d); }
    if (f != _fingerAngle) { _fingerAngle = f; driveFinger(f); }
}

void UselessFinger::update() {
    if (_state == FINGER_IDLE || _state == FINGER_FAILED) {
        if (_attached && millis() - _enteredAt > 600) detachAll();
        return;
    }

    const uint32_t now = millis();
    if (now - _lastStep >= FINGER_STEP_MS) {
        _lastStep = now;
        if (_state != FINGER_PAUSE && _state != FINGER_DWELL) stepServos();
    }

    if (now - _enteredAt < _segmentMs) return;

    switch (_state) {
    case FINGER_DOOR_OPENING:
        enter(FINGER_PAUSE);
        break;

    case FINGER_PAUSE:
        enter(FINGER_EXTENDING);
        break;

    case FINGER_EXTENDING:
        _fingerAngle = _fingerTo;
        driveFinger(_fingerAngle);
        enter(FINGER_DWELL);
        break;

    case FINGER_DWELL:
        // Did it actually flip the switch? In wiggle mode we never wanted to.
        if (!_wiggleMode && sensors.normalSwitchOn() && _attempt == 0) {
            // Missed - the user is probably holding the switch down. One more
            // go, from a slightly deeper angle.
            _attempt = 1;
            _fingerFrom = _fingerAngle;
            _fingerTo   = (uint8_t)min(180, FINGER_ANGLE_PRESS + 10);
            _segmentMs  = FINGER_EXTEND_MS / 2;
            _enteredAt  = now;
            _state      = FINGER_EXTENDING;
            return;
        }
        _pressEvent = true;
        enter(FINGER_RETRACTING);
        break;

    case FINGER_RETRACTING:
        _fingerAngle = _fingerTo;
        driveFinger(_fingerAngle);
        enter(FINGER_DOOR_CLOSING);
        break;

    case FINGER_DOOR_CLOSING:
        _doorAngle = _doorTo;
        driveDoor(_doorAngle);
        _cycles++;
        // If the switch is somehow still ON, the finger has been defeated.
        if (!_wiggleMode && sensors.normalSwitchOn()) {
            _state = FINGER_FAILED;
            _failEvent = true;
        } else {
            _state = FINGER_IDLE;
        }
        _enteredAt = millis();
        break;

    default:
        _state = FINGER_IDLE;
        _enteredAt = millis();
        break;
    }
}
