#include "motors.h"
#include <Arduino.h>
#include <Preferences.h>
#include "../config/pins.h"
#include "../config/settings.h"
#include "../core/eventLog.h"

Motors motors;

// Arduino-ESP32 changed the LEDC API in core 3.0: channels went away and PWM
// is now bound to the pin itself. Supporting both is one #if and saves the
// single most common "it will not compile" report.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define PWM_ATTACH(pin)        ledcAttach((pin), MOTOR_PWM_FREQ_HZ, MOTOR_PWM_BITS)
  #define PWM_WRITE(pin, ch, d)  ledcWrite((pin), (d))
#else
  #define PWM_ATTACH(pin)        /* handled below with explicit channels */
  #define PWM_WRITE(pin, ch, d)  ledcWrite((ch), (d))
#endif

static const uint8_t PWM_CH_LEFT  = 0;
static const uint8_t PWM_CH_RIGHT = 1;
static const uint8_t JOG_DUTY     = 150;   // enough to turn a loaded motor, gentle enough to watch

static Preferences prefs;

static inline int16_t clampI16(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return (int16_t)lo;
    if (v > hi) return (int16_t)hi;
    return (int16_t)v;
}

void Motors::begin() {
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    pinMode(PIN_MOTOR_IN3, OUTPUT);
    pinMode(PIN_MOTOR_IN4, OUTPUT);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    PWM_ATTACH(PIN_MOTOR_ENA);
    PWM_ATTACH(PIN_MOTOR_ENB);
#else
    ledcSetup(PWM_CH_LEFT,  MOTOR_PWM_FREQ_HZ, MOTOR_PWM_BITS);
    ledcSetup(PWM_CH_RIGHT, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_BITS);
    ledcAttachPin(PIN_MOTOR_ENA, PWM_CH_LEFT);
    ledcAttachPin(PIN_MOTOR_ENB, PWM_CH_RIGHT);
#endif

    loadCal();
    hardStop();
}

void Motors::loadCal() {
    prefs.begin("trashbot", true);
    cal_.leftInvert  = prefs.getBool("mInvL", false);
    cal_.rightInvert = prefs.getBool("mInvR", false);
    cal_.maxDuty     = prefs.getUChar("mMax", MOTOR_DEFAULT_MAX_DUTY);
    cal_.minDuty     = prefs.getUChar("mMin", MOTOR_DEFAULT_MIN_DUTY);
    cal_.accel       = prefs.getUChar("mAcc", MOTOR_DEFAULT_ACCEL);
    cal_.trim        = (int8_t)prefs.getChar("mTrim", 0);
    prefs.end();

    // A corrupt or hand-edited NVS entry must not be able to raise the
    // ceiling or stall the ramp.
    if (cal_.maxDuty == 0 || cal_.maxDuty > MOTOR_DUTY_CEILING) cal_.maxDuty = MOTOR_DEFAULT_MAX_DUTY;
    if (cal_.minDuty >= cal_.maxDuty)                           cal_.minDuty = (uint8_t)(cal_.maxDuty / 3);
    if (cal_.accel == 0)                                        cal_.accel   = MOTOR_DEFAULT_ACCEL;
    if (cal_.trim < -20) cal_.trim = -20;
    if (cal_.trim >  20) cal_.trim =  20;
}

void Motors::saveCal() {
    prefs.begin("trashbot", false);
    prefs.putBool ("mInvL", cal_.leftInvert);
    prefs.putBool ("mInvR", cal_.rightInvert);
    prefs.putUChar("mMax",  cal_.maxDuty);
    prefs.putUChar("mMin",  cal_.minDuty);
    prefs.putUChar("mAcc",  cal_.accel);
    prefs.putChar ("mTrim", cal_.trim);
    prefs.end();
    eventLog.push("CAL_SAVED", "invL=%d invR=%d max=%u min=%u accel=%u trim=%d",
                  cal_.leftInvert, cal_.rightInvert, cal_.maxDuty,
                  cal_.minDuty, cal_.accel, cal_.trim);
}

int16_t Motors::liftToMinDuty(int32_t duty) const {
    if (duty == 0) return 0;
    int32_t mag = duty < 0 ? -duty : duty;
    if (mag < cal_.minDuty) mag = cal_.minDuty;
    if (mag > cal_.maxDuty) mag = cal_.maxDuty;
    return (int16_t)(duty < 0 ? -mag : mag);
}

void Motors::setMix(int16_t leftPct, int16_t rightPct, uint8_t speedPct) {
    jogUntil_ = 0;                       // a real drive command ends a calibration jog
    if (speedPct > 100) speedPct = 100;
    leftPct  = clampI16(leftPct,  -100, 100);
    rightPct = clampI16(rightPct, -100, 100);

    int32_t scale = (int32_t)cal_.maxDuty * speedPct / 100;
    int32_t l = leftPct  * scale / 100;
    int32_t r = rightPct * scale / 100;

    // Straightness trim: only ever takes power away from the faster side, so
    // it cannot be used to exceed the ceiling.
    if (cal_.trim > 0)      l = l * (100 - cal_.trim) / 100;
    else if (cal_.trim < 0) r = r * (100 + cal_.trim) / 100;

    tgtL_ = liftToMinDuty(l);
    tgtR_ = liftToMinDuty(r);
}

void Motors::hardStop() {
    jogUntil_ = 0;
    tgtL_ = tgtR_ = 0;
    curL_ = curR_ = 0;
    writeSide(true, 0);
    writeSide(false, 0);
}

void Motors::jog(bool isLeft, int8_t dir, uint32_t now) {
    int16_t duty = (dir > 0) ? JOG_DUTY : (dir < 0 ? -JOG_DUTY : 0);
    if (isLeft) { tgtL_ = duty; tgtR_ = 0; }
    else        { tgtR_ = duty; tgtL_ = 0; }
    jogUntil_ = duty ? (now + MOTOR_TEST_MAX_MS) : 0;
}

int16_t Motors::rampAxis(int16_t cur, int16_t tgt) const {
    int16_t accel = accelOverride_ ? accelOverride_ : cal_.accel;
    if (accel < 1) accel = 1;
    int32_t decel = (int32_t)accel * MOTOR_DECEL_MULT;

    int32_t curMag = cur < 0 ? -cur : cur;
    int32_t tgtMag = tgt < 0 ? -tgt : tgt;
    // Anything that reduces the magnitude - including passing through zero on
    // a direction reversal - gets the faster of the two step sizes.
    int32_t step = (tgtMag > curMag) ? accel : decel;

    int32_t diff = (int32_t)tgt - (int32_t)cur;
    if (diff >= -step && diff <= step) return tgt;
    return (int16_t)(cur + (diff > 0 ? step : -step));
}

void Motors::tick(uint32_t now) {
    if (jogUntil_ && (int32_t)(now - jogUntil_) >= 0) {
        jogUntil_ = 0;
        tgtL_ = tgtR_ = 0;
        eventLog.push("CAL_JOG_END", "jog timed out, motor stopped");
    }

    if ((uint32_t)(now - lastTick_) < MOTOR_TICK_MS) return;
    lastTick_ = now;

    curL_ = rampAxis(curL_, tgtL_);
    curR_ = rampAxis(curR_, tgtR_);
    writeSide(true,  curL_);
    writeSide(false, curR_);
}

void Motors::writeSide(bool isLeft, int16_t duty) {
    if (isLeft ? cal_.leftInvert : cal_.rightInvert) duty = (int16_t)-duty;

    const uint8_t inA = isLeft ? PIN_MOTOR_IN1 : PIN_MOTOR_IN3;
    const uint8_t inB = isLeft ? PIN_MOTOR_IN2 : PIN_MOTOR_IN4;
    const uint8_t pwmPin = isLeft ? PIN_MOTOR_ENA : PIN_MOTOR_ENB;
    const uint8_t pwmCh  = isLeft ? PWM_CH_LEFT   : PWM_CH_RIGHT;

    if (duty > 0) {
        digitalWrite(inA, HIGH);
        digitalWrite(inB, LOW);
    } else if (duty < 0) {
        digitalWrite(inA, LOW);
        digitalWrite(inB, HIGH);
        duty = (int16_t)-duty;
    } else {
        // Both low = coast. Not a brake: braking a geared motor at speed is
        // harder on the gearbox than letting it run down.
        digitalWrite(inA, LOW);
        digitalWrite(inB, LOW);
    }

    if (duty > cal_.maxDuty) duty = cal_.maxDuty;
    (void)pwmPin;   // one of these two is unused depending on the core version
    (void)pwmCh;
    PWM_WRITE(pwmPin, pwmCh, (uint32_t)duty);
}
