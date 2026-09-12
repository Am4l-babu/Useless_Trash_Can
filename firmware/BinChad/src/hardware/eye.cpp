#include "eye.h"
#include "../config/pins.h"
#include <ESP32Servo.h>
#include <math.h>

Eye eye;

static Servo panServo;
#if USE_EYE_TILT
static Servo tiltServo;
#endif

static const uint16_t MICRO_PULSE_MIN_US = 500;
static const uint16_t MICRO_PULSE_MAX_US = 2400;

static inline float ease(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 0.5f * (1.0f - cosf((float)M_PI * t));
}

bool Eye::begin() {
    panServo.setPeriodHertz(50);
#if USE_EYE_TILT
    tiltServo.setPeriodHertz(50);
#endif
    attachAll();
    _pan = _panFrom = _panTo = EYE_PAN_CENTER;
    _tilt = _tiltFrom = _tiltTo = EYE_TILT_CENTER;
    apply(_pan, _tilt);
    delay(250);
    _restingSince = millis();
    _nextWander = millis() + 3000;
    return true;
}

void Eye::attachAll() {
    if (_attached) return;
    panServo.attach(PIN_SERVO_EYE_PAN, MICRO_PULSE_MIN_US, MICRO_PULSE_MAX_US);
#if USE_EYE_TILT
    tiltServo.attach(PIN_SERVO_EYE_TILT, MICRO_PULSE_MIN_US, MICRO_PULSE_MAX_US);
#endif
    _attached = true;
}

void Eye::detachAll() {
    if (!_attached) return;
    panServo.detach();
#if USE_EYE_TILT
    tiltServo.detach();
#endif
    _attached = false;
}

void Eye::apply(uint8_t pan, uint8_t tilt) {
    _pan  = constrain(pan,  EYE_PAN_MIN,  EYE_PAN_MAX);
    _tilt = constrain(tilt, EYE_TILT_MIN, EYE_TILT_MAX);
    if (!_attached) return;
    panServo.write(_pan);
#if USE_EYE_TILT
    tiltServo.write(_tilt);
#else
    (void)_tilt;
#endif
}

void Eye::startMove(uint8_t pan, uint8_t tilt, uint16_t durationMs) {
    attachAll();
    _panFrom  = _pan;   _panTo  = constrain(pan,  EYE_PAN_MIN,  EYE_PAN_MAX);
    _tiltFrom = _tilt;  _tiltTo = constrain(tilt, EYE_TILT_MIN, EYE_TILT_MAX);
    _moveStart = millis();
    _moveMs    = durationMs ? durationMs : 1;
    _lastStep  = 0;
    _moving    = true;
}

void Eye::center() { look(EYE_CENTER); }

void Eye::look(EyeTarget t, uint16_t durationMs) {
    _asleep = false;
    switch (t) {
        case EYE_CENTER:
            startMove(EYE_PAN_CENTER, EYE_TILT_CENTER, durationMs);
            break;
        case EYE_USER:
            startMove(EYE_PAN_CENTER, EYE_TILT_MAX - 6, durationMs);
            break;
        case EYE_INTO_BIN:
            startMove(EYE_PAN_CENTER, EYE_TILT_MIN + 4, durationMs);
            break;
        case EYE_REMOTE:
            startMove(EYE_PAN_CENTER - 22, EYE_TILT_CENTER - 8, durationMs);
            break;
        case EYE_BUTTON:
            // Deliberately slow. The bin looking at the button before the
            // finger appears is the setup for the punchline.
            startMove(EYE_PAN_MAX - 12, EYE_TILT_CENTER - 10, durationMs * 2);
            break;
        case EYE_AWAY:
            startMove(EYE_PAN_MIN + 8, EYE_TILT_MAX - 10, durationMs);
            break;
    }
}

void Eye::lookNormalized(float x, float y, uint16_t durationMs) {
    _asleep = false;
    x = constrain(x, -1.0f, 1.0f);
    y = constrain(y, -1.0f, 1.0f);
    const uint8_t pan = (uint8_t)lroundf(
        EYE_PAN_CENTER + x * ((x < 0) ? (EYE_PAN_CENTER - EYE_PAN_MIN)
                                      : (EYE_PAN_MAX - EYE_PAN_CENTER)));
    const uint8_t tilt = (uint8_t)lroundf(
        EYE_TILT_CENTER + y * ((y < 0) ? (EYE_TILT_CENTER - EYE_TILT_MIN)
                                       : (EYE_TILT_MAX - EYE_TILT_CENTER)));
    startMove(pan, tilt, durationMs);
}

void Eye::blink() {
    _blinkStage = 1;
    _blinkPhase = millis();
    attachAll();
}

void Eye::jitter(uint8_t amount) {
    _jitterAmount = amount;
    _jitterUntil  = millis() + 900;
    attachAll();
}

void Eye::sleep() {
    _asleep = true;
    _wander = false;
    startMove(EYE_PAN_CENTER, EYE_TILT_MIN, 1200);   // rolls down, "closed"
}

void Eye::wake() {
    _asleep = false;
    _wander = true;
    startMove(EYE_PAN_CENTER, EYE_TILT_CENTER, 1400);  // opens slowly, suspiciously
}

void Eye::update() {
    const uint32_t now = millis();

    // ---- blink: a fast down-up flick on the tilt axis ---------------------
#if USE_EYE_TILT
    if (_blinkStage) {
        const uint32_t dt = now - _blinkPhase;
        if (_blinkStage == 1 && dt > 40) {
            apply(_pan, EYE_TILT_MIN);
            _blinkStage = 2;
            _blinkPhase = now;
        } else if (_blinkStage == 2 && dt > 90) {
            apply(_pan, _tiltTo);
            _blinkStage = 0;
        }
        return;
    }
#else
    _blinkStage = 0;
#endif

    // ---- angry jitter -----------------------------------------------------
    if (_jitterAmount && now < _jitterUntil) {
        if (now - _lastStep >= EYE_STEP_MS) {
            _lastStep = now;
            const int8_t d = (int8_t)random(-(int)_jitterAmount, (int)_jitterAmount + 1);
            apply((uint8_t)constrain((int)_panTo + d, EYE_PAN_MIN, EYE_PAN_MAX), _tilt);
        }
        return;
    }
    if (_jitterAmount && now >= _jitterUntil) {
        _jitterAmount = 0;
        apply(_panTo, _tiltTo);
    }

    // ---- eased motion -----------------------------------------------------
    if (_moving) {
        if (now - _lastStep < EYE_STEP_MS) return;
        _lastStep = now;

        const uint32_t elapsed = now - _moveStart;
        if (elapsed >= _moveMs) {
            apply(_panTo, _tiltTo);
            _moving = false;
            _restingSince = now;
        } else {
            const float t = ease((float)elapsed / (float)_moveMs);
            apply((uint8_t)((int16_t)_panFrom  + lroundf(t * ((float)_panTo  - (float)_panFrom))),
                  (uint8_t)((int16_t)_tiltFrom + lroundf(t * ((float)_tiltTo - (float)_tiltFrom))));
        }
        return;
    }

    // ---- idle micro-movement ---------------------------------------------
    // Small, infrequent saccades. Without these the eye looks switched off;
    // with them it looks like it is thinking about something.
    if (_wander && !_asleep && now > _nextWander) {
        const int8_t dp = (int8_t)random(-14, 15);
        startMove((uint8_t)constrain((int)EYE_PAN_CENTER + dp, EYE_PAN_MIN, EYE_PAN_MAX),
                  _tilt, (uint16_t)random(300, 700));
        _nextWander = now + random(2200, 6000);
        if (random(100) < 30) blink();
        return;
    }

    if (_attached && (now - _restingSince) > EYE_IDLE_DETACH_MS) detachAll();
}
