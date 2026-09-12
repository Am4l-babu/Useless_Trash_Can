// =============================================================================
//  uselessFinger.h - the signature mechanism.
//
//  Sequence, in order, all non-blocking:
//      IDLE -> DOOR_OPENING -> PAUSE (the dramatic beat) -> EXTENDING
//           -> DWELL (on the switch) -> RETRACTING -> DOOR_CLOSING -> IDLE
//
//  The finger's job is to physically flip the NORMAL MODE latching switch
//  back to OFF. It verifies that it actually did so by reading the switch,
//  and retries once if the user was leaning on it. If the switch is still
//  ON after the retry, the bin gives up and complains on the display -
//  which is arguably funnier than succeeding.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum FingerState : uint8_t {
    FINGER_IDLE,
    FINGER_DOOR_OPENING,
    FINGER_PAUSE,
    FINGER_EXTENDING,
    FINGER_DWELL,
    FINGER_RETRACTING,
    FINGER_DOOR_CLOSING,
    FINGER_FAILED        // switch still ON after the retry
};

class UselessFinger {
public:
    bool begin();
    void update();

    void deploy();          // run the full sequence
    void abort();           // retract immediately, close the hatch
    void wiggle();          // a short taunt: hatch opens, finger waves, closes

    FingerState state()  const { return _state; }
    bool isBusy()        const { return _state != FINGER_IDLE && _state != FINGER_FAILED; }
    bool hasFailed()     const { return _state == FINGER_FAILED; }
    uint32_t cycles()    const { return _cycles; }

    // One-shot: the finger completed a press since the last call.
    bool consumePressEvent();

    // One-shot: the finger failed to move the switch since the last call.
    // hasFailed() is a LEVEL and stays true; this is the EDGE. The behaviour
    // layer must use this one, or it will re-announce the failure and restart
    // the audio clip on every single loop pass.
    bool consumeFailureEvent();

private:
    void enter(FingerState s);
    void driveDoor(uint8_t angle);
    void driveFinger(uint8_t angle);
    void stepServos();
    void attachAll();
    void detachAll();

    FingerState _state     = FINGER_IDLE;
    uint32_t    _enteredAt = 0;
    uint32_t    _lastStep  = 0;

    uint8_t  _doorAngle    = DOOR_ANGLE_SHUT;
    uint8_t  _doorFrom     = DOOR_ANGLE_SHUT;
    uint8_t  _doorTo       = DOOR_ANGLE_SHUT;
    uint8_t  _fingerAngle  = FINGER_ANGLE_HOME;
    uint8_t  _fingerFrom   = FINGER_ANGLE_HOME;
    uint8_t  _fingerTo     = FINGER_ANGLE_HOME;
    uint16_t _segmentMs    = 1;

    bool     _attached     = false;
    bool     _pressEvent   = false;
    bool     _failEvent    = false;
    bool     _wiggleMode   = false;
    uint8_t  _attempt      = 0;
    uint32_t _cycles       = 0;
};

extern UselessFinger finger;
