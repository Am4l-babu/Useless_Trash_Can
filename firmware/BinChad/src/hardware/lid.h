// =============================================================================
//  lid.h - the only genuinely dangerous part of this machine.
//
//  Rules this module enforces, regardless of what the personality layer wants:
//    1. The lid never moves faster than its configured profile.
//    2. Closing motion is eased (soft-close) - it cannot slam.
//    3. If the throat sensor sees a hand while closing, the lid stops and
//       reopens. The behaviour layer is informed; it is not consulted.
//    4. Servo angle is never trusted on its own - limit switches confirm
//       the endpoints, and a disagreement raises LID_ERROR instead of
//       grinding the servo into a stop.
//
//  Motion profile: cosine ease-in-out, recomputed every LID_STEP_MS.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum LidState : uint8_t {
    LID_CLOSED,
    LID_OPENING,
    LID_OPEN,
    LID_CLOSING,
    LID_BLOCKED,    // obstruction seen; lid has retreated to open
    LID_ERROR       // limit switches disagree with the servo, or never arrived
};

enum LidSpeed : uint8_t {
    LID_SPEED_NORMAL,
    LID_SPEED_SOFT,     // extra slow, used for closing near a person
    LID_SPEED_ANGRY     // fast, still eased, still obstruction-checked
};

class Lid {
public:
    bool begin();
    void update();

    void open(LidSpeed s = LID_SPEED_NORMAL);
    void close(LidSpeed s = LID_SPEED_NORMAL);
    void peek();                       // half-open "curious" pose
    void moveTo(uint8_t angle, uint16_t durationMs);
    void holdOpenFor(uint16_t ms);     // open, then auto-close after ms
    void stop();                       // freeze where it is (remote STOP joke)
    void emergencyStop();              // freeze AND drop servo power

    LidState state()    const { return _state; }
    uint8_t  angle()    const { return _angle; }
    bool     isMoving() const { return _state == LID_OPENING || _state == LID_CLOSING; }
    bool     isOpen()   const { return _state == LID_OPEN || _state == LID_BLOCKED; }
    bool     hasError() const { return _state == LID_ERROR; }
    uint32_t cycles()   const { return _cycles; }

    // One-shot: an obstruction was detected since the last call. The
    // behaviour layer uses this to trigger the apology.
    bool consumeObstructionEvent();

    void clearError();                 // operator override, retries homing
    void setAutoClose(bool on) { _autoClose = on; }

private:
    void  startMove(uint8_t target, uint16_t durationMs, LidState movingState);
    void  applyAngle(uint8_t a);
    void  attachServo();
    void  detachServo();
    void  finishMove();
    bool  endpointConfirmed(uint8_t target) const;
    uint16_t durationFor(LidSpeed s, bool opening) const;

    LidState _state       = LID_CLOSED;
    uint8_t  _angle       = LID_ANGLE_CLOSED;
    uint8_t  _startAngle  = LID_ANGLE_CLOSED;
    uint8_t  _targetAngle = LID_ANGLE_CLOSED;

    uint32_t _moveStart   = 0;
    uint16_t _moveMs      = 0;
    uint32_t _lastStep    = 0;
    uint32_t _restingSince= 0;
    uint32_t _openedAt    = 0;
    uint16_t _holdMs      = LID_HOLD_OPEN_MS;

    bool     _attached    = false;
    bool     _autoClose   = true;
    bool     _obstruction = false;
    uint8_t  _retries     = 0;
    uint32_t _cycles      = 0;
    uint32_t _blockedAt   = 0;
};

extern Lid lid;
