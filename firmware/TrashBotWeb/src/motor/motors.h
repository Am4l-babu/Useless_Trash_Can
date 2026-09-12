// =============================================================================
//  motors.h - L298N driver, acceleration limiting and calibration.
//
//  This is the bottom of the stack. Nothing above it can exceed the limits
//  enforced here: maxDuty is applied after every transformation the
//  personality engine performs, so "CHAOS mode" can pick a silly direction
//  but cannot pick a silly speed.
//
//  Calibration lives in NVS so a reflash does not send the bin backwards.
// =============================================================================
#pragma once
#include <stdint.h>

struct MotorCal {
    bool    leftInvert;
    bool    rightInvert;
    uint8_t maxDuty;    // ceiling, 0..255
    uint8_t minDuty;    // below this a loaded motor buzzes instead of turning
    uint8_t accel;      // duty change allowed per tick while speeding up
    int8_t  trim;       // -20..+20 %, positive slows the left motor
};

class Motors {
  public:
    void begin();
    void loadCal();
    void saveCal();
    MotorCal& cal() { return cal_; }

    // leftPct/rightPct are the differential mix (-100..100), speedPct scales
    // both. The result is clamped to cal_.maxDuty, always.
    void setMix(int16_t leftPct, int16_t rightPct, uint8_t speedPct);

    // Immediate, ramp bypassed, targets zeroed. The only way to stop that the
    // safety layer is allowed to use.
    void hardStop();

    void tick(uint32_t now);

    // LAZY drive mode. 0 restores the calibrated value.
    void setAccelOverride(uint8_t step) { accelOverride_ = step; }

    // Calibration page only: drive one motor directly at a fixed modest duty.
    // dir is -1 reverse / 0 stop / +1 forward. Auto-expires so a browser that
    // walks away cannot leave a motor running.
    void jog(bool isLeft, int8_t dir, uint32_t now);
    bool jogActive() const { return jogUntil_ != 0; }

    int16_t leftDuty()    const { return curL_; }
    int16_t rightDuty()   const { return curR_; }
    int16_t leftTarget()  const { return tgtL_; }
    int16_t rightTarget() const { return tgtR_; }
    bool    moving()      const { return curL_ != 0 || curR_ != 0; }

  private:
    void    writeSide(bool isLeft, int16_t duty);
    int16_t rampAxis(int16_t cur, int16_t tgt) const;
    int16_t liftToMinDuty(int32_t duty) const;

    MotorCal cal_{};
    int16_t  tgtL_ = 0, tgtR_ = 0;
    int16_t  curL_ = 0, curR_ = 0;
    uint8_t  accelOverride_ = 0;
    uint32_t lastTick_ = 0;
    uint32_t jogUntil_ = 0;
};

extern Motors motors;
