// =============================================================================
//  eye.h - the servo-mounted eyeball.
//
//  This is the single cheapest trick in the whole project and the one that
//  does the most work. A bin that turns to look at you reads as "aware"
//  long before anyone notices the sensors.
//
//  Pan is mandatory, tilt is optional (USE_EYE_TILT). Both are eased, both
//  detach when parked to kill micro-servo buzz.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum EyeTarget : uint8_t {
    EYE_CENTER,
    EYE_USER,        // straight out, slightly up
    EYE_INTO_BIN,    // down the throat, checking the damage
    EYE_REMOTE,      // toward where the remote usually is (user's hands)
    EYE_BUTTON,      // toward the NORMAL MODE switch - used to telegraph the joke
    EYE_AWAY         // pointedly not at you
};

class Eye {
public:
    bool begin();
    void update();

    void center();
    void look(EyeTarget t, uint16_t durationMs = EYE_MOVE_MS);
    void lookNormalized(float x, float y = 0.0f, uint16_t durationMs = EYE_MOVE_MS);
    void blink();                 // quick flick, reads as a blink
    void jitter(uint8_t amount);  // angry vibration
    void sleep();                 // eye rolls down and parks
    void wake();

    void setIdleWander(bool on) { _wander = on; }
    bool isMoving() const { return _moving; }
    bool isAsleep() const { return _asleep; }

private:
    void startMove(uint8_t pan, uint8_t tilt, uint16_t durationMs);
    void apply(uint8_t pan, uint8_t tilt);
    void attachAll();
    void detachAll();

    uint8_t  _pan = EYE_PAN_CENTER,  _panFrom = EYE_PAN_CENTER,  _panTo = EYE_PAN_CENTER;
    uint8_t  _tilt = EYE_TILT_CENTER, _tiltFrom = EYE_TILT_CENTER, _tiltTo = EYE_TILT_CENTER;

    uint32_t _moveStart = 0;
    uint16_t _moveMs    = 1;
    uint32_t _lastStep  = 0;
    uint32_t _restingSince = 0;
    uint32_t _nextWander   = 0;

    bool _moving   = false;
    bool _attached = false;
    bool _wander   = true;
    bool _asleep   = false;

    uint8_t  _jitterAmount = 0;
    uint32_t _jitterUntil  = 0;
    uint32_t _blinkPhase   = 0;
    uint8_t  _blinkStage   = 0;
};

extern Eye eye;
