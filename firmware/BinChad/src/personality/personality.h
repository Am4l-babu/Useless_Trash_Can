// =============================================================================
//  personality.h - what BIN-CHAD is currently feeling, and how that changes
//  what the rest of the machine does.
//
//  This module owns:
//    * the mood state machine
//    * the interaction counters and the escalation rules built on them
//    * the line/quip selection (never the same line twice in a row)
//    * the remote mistranslation engine
//
//  It owns no hardware. It decides; behaviorManager acts.
//
//  On determinism: the mistranslation engine is a seeded xorshift, not
//  rand(). Given the same seed and the same sequence of button presses it
//  produces the same sequence of wrong answers, which means a demo can be
//  rehearsed and a bug can be reproduced. See docs/REMOTE_PROTOCOL.md.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum PersonalityState : uint8_t {
    P_IDLE,
    P_CURIOUS,
    P_ALERT,
    P_HAPPY,
    P_CONFUSED,
    P_ANGRY,
    P_SHY,
    P_SARCASTIC,
    P_SLEEPING,
    P_AI_MODE,
    P_NORMAL_MODE,
    P_USELESS_MODE,
    P_ERROR,
    P_STATE_COUNT_
};

class Personality {
public:
    void begin(uint32_t seed);
    void update();

    // ---- state ------------------------------------------------------------
    PersonalityState state() const { return _state; }
    void setState(PersonalityState s);
    const char *stateName() const;
    static const char *stateName(PersonalityState s);
    uint32_t msInState() const { return millis() - _stateSince; }

    // ---- events (called by behaviorManager) --------------------------------
    void onInteraction();
    void onSuccess();
    void onMiss();
    void onRemoteCommand();
    void onNormalButton();
    void onObstruction();
    void onError();

    // ---- counters ----------------------------------------------------------
    uint16_t interactions()  const { return _interactions; }
    uint16_t successes()     const { return _successes; }
    uint16_t misses()        const { return _misses; }
    uint16_t remoteCommands()const { return _remoteCommands; }
    uint16_t normalPresses() const { return _normalPresses; }
    uint8_t  frustration()   const { return _frustration; }
    uint8_t  anger()         const { return _anger; }      // 0..100

    // ---- lines -------------------------------------------------------------
    // All return static strings; safe to hold. Never repeat consecutively.
    const char *missLine();
    const char *successLine();
    const char *remoteRefusalLine();
    const char *angryLine();
    const char *idleLine();
    const char *aiVerdictLine();

    // ---- the remote mistranslation engine ----------------------------------
    // Maps a requested command to what the bin will actually do.
    // obeyed is set true on the rare occasions the bin complies.
    uint8_t mistranslate(uint8_t requested, bool &obeyed);

    // Deterministic PRNG - also used for line choice so the whole
    // personality is reproducible from one seed.
    uint32_t rnd();
    uint32_t rndBelow(uint32_t n);

private:
    const char *pick(const char *const *table, uint8_t count, uint8_t &lastIndex);

    PersonalityState _state = P_IDLE;
    uint32_t _stateSince = 0;

    uint16_t _interactions = 0, _successes = 0, _misses = 0;
    uint16_t _remoteCommands = 0, _normalPresses = 0;
    uint8_t  _frustration = 0;
    uint8_t  _anger = 0;
    uint32_t _lastAngerDecay = 0;

    uint32_t _rngState = 0x1BADB002;

    uint8_t _lastMiss = 0xFF, _lastSuccess = 0xFF, _lastRefusal = 0xFF;
    uint8_t _lastAngry = 0xFF, _lastIdle = 0xFF, _lastVerdict = 0xFF;
};

extern Personality personality;
