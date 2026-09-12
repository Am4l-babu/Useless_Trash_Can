// =============================================================================
//  behaviorManager.h - the top-level state machine.
//
//  This is the only module that is allowed to command hardware. Everything
//  else either senses, or decides, or renders. Keeping actuation in one
//  place is what makes the safety argument tractable: there is exactly one
//  file to audit for "what can make the lid move".
//
//      BOOT -> SELF_TEST -> IDLE
//                            |-- object/person  -> THROW_RESPONSE -> (SUCCESS|MISS)
//                            |-- remote command -> REMOTE_RESPONSE
//                            |-- NORMAL switch  -> NORMAL_MODE -> SELF_DISABLE
//                            |-- AI command     -> AI_THEATRE
//                            |-- timeout        -> SLEEP
//                            '-- fault          -> FAULT
//
//  Full diagram: docs/state-machine.png
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum MainState : uint8_t {
    ST_BOOT,
    ST_SELF_TEST,
    ST_IDLE,
    ST_CURIOUS,
    ST_THROW_WAIT,      // lid open, waiting to see whether you can aim
    ST_SUCCESS,
    ST_MISS_SILENCE,    // the judgemental pause
    ST_MISS_REACT,
    ST_REMOTE_RESPONSE,
    ST_AI_THEATRE,
    ST_NORMAL_MODE,
    ST_SELF_DISABLE,    // the finger comes out
    ST_APOLOGY,         // obstruction detected; the one sincere state
    ST_SLEEP,
    ST_FAULT
};

class BehaviorManager {
public:
    void begin();
    void update();

    MainState state() const { return _state; }
    const char *stateName() const;

    // Forces a successful-throw reaction. Wired to the hidden trigger so a
    // missed throw during the demo never becomes a dead moment on stage.
    void forceSuccess();

private:
    void enter(MainState s);
    uint32_t msInState() const { return millis() - _stateSince; }

    void runSelfTest();
    void handleSensors();
    void handleRemote();
    void handleNormalSwitch();
    void handleFaults();

    void executeCommand(uint8_t cmd, bool obeyed);
    void startThrowResponse();
    void reactSuccess();
    void reactMiss();
    void startAiTheatre();
    void startNormalMode();
    void startSelfDisable();

    MainState _state      = ST_BOOT;
    uint32_t  _stateSince = 0;
    uint8_t   _step       = 0;
    uint32_t  _stepAt     = 0;

    uint32_t  _lastInteraction = 0;

    // Sensor edges are latched ONCE per update() pass and read from here.
    // Calling sensors.objectJustEntered() directly from inside a state case
    // consumes the edge only in that state, so an edge raised while the bin
    // was busy elsewhere would survive and fire late, in the wrong state.
    bool      _objectEdge = false;
    bool      _personEdge = false;

    bool      _throatWasBlocked = false;
    bool      _forcedSuccess    = false;

    // AI theatre progress
    uint8_t   _aiPercent = 0;
};

extern BehaviorManager behavior;
