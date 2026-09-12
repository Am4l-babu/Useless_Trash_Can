#include "lid.h"
#include "sensors.h"
#include "../config/pins.h"
#include <ESP32Servo.h>
#include <math.h>

Lid lid;

static Servo lidServo;

// MG996R / DS3218 class servos want a wider pulse window than the Arduino
// default of 544..2400 us. These values match the reference build; trim them
// if your servo buzzes or strains at the endpoints.
static const uint16_t LID_PULSE_MIN_US = 500;
static const uint16_t LID_PULSE_MAX_US = 2500;

// ---------------------------------------------------------------------------
// Cosine ease-in-out. t in 0..1 maps to 0..1 with zero velocity at both ends.
// This is what stops the lid slamming. Do not replace it with a linear ramp.
// ---------------------------------------------------------------------------
static inline float ease(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 0.5f * (1.0f - cosf((float)M_PI * t));
}

bool Lid::begin() {
    pinMode(PIN_SERVO_POWER_EN, OUTPUT);
    digitalWrite(PIN_SERVO_POWER_EN, HIGH);        // servo rail live

    lidServo.setPeriodHertz(50);
    attachServo();
    _angle = _startAngle = _targetAngle = LID_ANGLE_CLOSED;
    applyAngle(_angle);
    delay(300);                                    // one-time settle, boot only

    // Home against the closed limit switch. If it never asserts, the lid is
    // mis-assembled or the switch is dead. Say so instead of pretending.
    sensors.update();
    if (!sensors.lidAtClosedSwitch()) {
        Serial.println(F("[lid] closed limit switch not made at boot"));
        _state = LID_ERROR;
        _restingSince = millis();
        return false;
    }
    _state = LID_CLOSED;
    _restingSince = millis();
    return true;
}

// ---------------------------------------------------------------------------
// Servo attach/detach. Parking the lid with the servo detached removes the
// idle hunting and buzz that makes cheap analogue servos sound broken, and
// saves several hundred mA on the rail between interactions.
// ---------------------------------------------------------------------------
void Lid::attachServo() {
    if (_attached) return;
    lidServo.attach(PIN_SERVO_LID, LID_PULSE_MIN_US, LID_PULSE_MAX_US);
    _attached = true;
}

void Lid::detachServo() {
    if (!_attached) return;
    lidServo.detach();
    _attached = false;
}

void Lid::applyAngle(uint8_t a) {
    _angle = a;
    const uint8_t out = LID_INVERT ? (uint8_t)(180 - a) : a;
    if (_attached) lidServo.write(out);
}

uint16_t Lid::durationFor(LidSpeed s, bool opening) const {
    switch (s) {
        case LID_SPEED_ANGRY:
            return LID_MS_SLAM;
        case LID_SPEED_SOFT:
            return opening ? (uint16_t)(LID_MS_OPEN * 2)
                           : (uint16_t)(LID_MS_CLOSE * 1.6f);
        default:
            return opening ? LID_MS_OPEN : LID_MS_CLOSE;
    }
}

void Lid::startMove(uint8_t target, uint16_t durationMs, LidState movingState) {
    if (_state == LID_ERROR) return;
    attachServo();

    const uint8_t lo = LID_ANGLE_CLOSED < LID_ANGLE_OPEN ? LID_ANGLE_CLOSED : LID_ANGLE_OPEN;
    const uint8_t hi = LID_ANGLE_CLOSED < LID_ANGLE_OPEN ? LID_ANGLE_OPEN : LID_ANGLE_CLOSED;

    _startAngle  = _angle;
    _targetAngle = constrain(target, lo, hi);

    // Scale the duration by the distance actually travelled, so a short hop
    // is not stretched across a full-travel time.
    const int16_t span = abs((int16_t)_targetAngle - (int16_t)_startAngle);
    const int16_t full = abs((int16_t)LID_ANGLE_OPEN - (int16_t)LID_ANGLE_CLOSED);
    _moveMs = (full > 0) ? (uint16_t)max(60L, (long)durationMs * span / full)
                         : durationMs;

    _moveStart = millis();
    _lastStep  = 0;
    _state     = movingState;
}

void Lid::open(LidSpeed s)  { startMove(LID_ANGLE_OPEN,   durationFor(s, true),  LID_OPENING); }
void Lid::close(LidSpeed s) { startMove(LID_ANGLE_CLOSED, durationFor(s, false), LID_CLOSING); }
void Lid::peek()            { startMove(LID_ANGLE_PEEK,   LID_MS_OPEN,           LID_OPENING); }

void Lid::moveTo(uint8_t a, uint16_t durationMs) {
    startMove(a, durationMs, (a > _angle) ? LID_OPENING : LID_CLOSING);
}

void Lid::holdOpenFor(uint16_t ms) {
    _holdMs = ms;
    _autoClose = true;
    open();
}

void Lid::stop() {
    if (!isMoving()) return;
    _targetAngle = _angle;
    _state = (_angle > (uint8_t)((LID_ANGLE_CLOSED + LID_ANGLE_OPEN) / 2)) ? LID_OPEN : LID_CLOSED;
    _openedAt = millis();
    _restingSince = millis();
}

void Lid::emergencyStop() {
    stop();
    detachServo();
    digitalWrite(PIN_SERVO_POWER_EN, LOW);   // kill the rail; nothing can move
}

bool Lid::consumeObstructionEvent() {
    const bool e = _obstruction;
    _obstruction = false;
    return e;
}

bool Lid::endpointConfirmed(uint8_t target) const {
    if (target == LID_ANGLE_CLOSED) return sensors.lidAtClosedSwitch();
    if (target == LID_ANGLE_OPEN)   return sensors.lidAtOpenSwitch();
    return true;    // intermediate poses have no switch to check against
}

void Lid::finishMove() {
    applyAngle(_targetAngle);

    // Trust, but verify. The servo claims it arrived; the switch decides.
    if (!endpointConfirmed(_targetAngle)) {
        if (millis() - _moveStart < LID_LIMIT_TIMEOUT_MS) return;   // give it a moment
        Serial.printf("[lid] endpoint %u not confirmed by limit switch\r\n", _targetAngle);
        _state = LID_ERROR;
        detachServo();
        return;
    }

    if (_targetAngle == LID_ANGLE_CLOSED) {
        _state = LID_CLOSED;
        _cycles++;
    } else {
        _state = LID_OPEN;
        _openedAt = millis();
    }
    _retries = 0;
    _restingSince = millis();
}

void Lid::clearError() {
    _retries = 0;
    _state = LID_CLOSING;
    attachServo();
    startMove(LID_ANGLE_CLOSED, (uint16_t)(LID_MS_CLOSE * 2), LID_CLOSING);   // slow and careful
}

// ===========================================================================
//  update() - called every loop; performs at most one eased step
// ===========================================================================
void Lid::update() {
    const uint32_t now = millis();

    // ---- SAFETY FIRST -----------------------------------------------------
    // Evaluated before anything else, on every call, unconditionally.
    // The personality layer does not get a vote here.
    if (_state == LID_CLOSING && sensors.handInDangerZone()) {
        Serial.println(F("[lid] obstruction detected - aborting close"));
        _obstruction = true;
        _blockedAt   = now;
        startMove(LID_ANGLE_OPEN, LID_MS_OPEN, LID_OPENING);
        _state = LID_BLOCKED;      // startMove set OPENING; BLOCKED is what we mean
        return;
    }

    switch (_state) {

    case LID_OPENING:
    case LID_CLOSING:
    case LID_BLOCKED: {
        if (now - _lastStep < LID_STEP_MS) break;
        _lastStep = now;

        const uint32_t elapsed = now - _moveStart;
        if (elapsed >= _moveMs) {
            if (_state == LID_BLOCKED) {
                applyAngle(_targetAngle);   // finished retreating; wait it out
                break;
            }
            finishMove();
        } else {
            const float t = ease((float)elapsed / (float)_moveMs);
            const int16_t a = (int16_t)_startAngle +
                (int16_t)lroundf(t * ((float)_targetAngle - (float)_startAngle));
            applyAngle((uint8_t)a);
        }
        break;
    }

    case LID_OPEN:
        // Auto-close after the hold window, but only once the throat is
        // clear. A hand parked over the opening just extends the hold.
        if (_autoClose && (now - _openedAt) >= _holdMs) {
            if (sensors.handInDangerZone()) {
                _openedAt = now - _holdMs + 400;   // re-check shortly
            } else {
                close(LID_SPEED_NORMAL);
            }
        } else if (!_autoClose && _attached &&
                   (now - _restingSince) > LID_IDLE_DETACH_MS) {
            detachServo();
        }
        break;

    case LID_CLOSED:
        if (_attached && (now - _restingSince) > LID_IDLE_DETACH_MS) detachServo();
        break;

    case LID_ERROR:
        detachServo();
        break;
    }

    // Recovery from BLOCKED: once the obstruction clears, retry the close a
    // bounded number of times, then give up and stay open. Failing open is
    // the safe direction for a lid.
    if (_state == LID_BLOCKED &&
        (now - _blockedAt) > LID_RETRY_PAUSE_MS &&
        !sensors.handInDangerZone()) {
        if (_retries < LID_MAX_RETRIES) {
            _retries++;
            close(LID_SPEED_SOFT);
        } else {
            _state     = LID_OPEN;
            _openedAt  = now;
            _autoClose = false;
        }
    }
}
