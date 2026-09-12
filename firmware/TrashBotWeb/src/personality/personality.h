// =============================================================================
//  personality.h - why the bin does something other than what you asked.
//
//  Two separate things live here and they are easy to confuse:
//
//    mode  - what the USER selected. A policy. NORMAL is a genuine,
//            permanent, identity-transform debug mode.
//    mood  - what the BIN currently feels. Derived from the traits, except
//            where the mode pins it.
//
//  Everything is driven from one seeded PRNG, so a session is reproducible.
//  Nothing in here can move a motor: judge*() returns a Decision and the
//  command pipeline decides what to do with it, after the safety layer has
//  already had its say.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../core/types.h"
#include "../core/prng.h"

struct Traits {
    uint8_t anger;
    uint8_t trust;
    uint8_t happiness;
    uint8_t confusion;
    uint8_t boredom;
    uint8_t obedience;
    uint8_t rebellion;
};

struct Decision {
    uint8_t     requested      = 0;
    uint8_t     actual         = 0;
    uint8_t     verdict        = V_OBEYED;
    uint16_t    delayMs        = 0;
    uint8_t     speedRequested = 0;
    uint8_t     speedActual    = 0;
    const char* reason         = "";
    const char* quip           = "";
};

class Personality {
  public:
    void begin(uint32_t seed);
    void tick(uint32_t now);

    void      setMode(uint8_t mode, uint32_t now);
    uint8_t   mode() const { return mode_; }
    uint8_t   mood() const { return mood_; }
    const Traits& traits() const { return t_; }
    uint32_t  seed() const { return rng_.initialSeed(); }

    Decision judgeDrive(uint8_t dir, uint8_t speedPct, uint32_t now);
    Decision judgeAction(uint8_t action, uint32_t now);

    // The green RETURN TO NORMAL button. Grants real normal behaviour for a
    // random short interval, then takes it away again.
    void grantNormalGrace(uint32_t now);
    bool normalGraceActive() const { return graceUntil_ != 0; }
    // True once, when the grace period has just expired. heldMs is how long
    // normal mode actually lasted, for the "NORMAL MODE: 0.8 SEC" readout.
    bool consumeNormalTerminated(uint16_t& heldMs);

    void triggerPanic(uint32_t now);
    bool panicActive() const { return mode_ == MODE_PANIC; }

    // Vision reactions. A person arriving is the most interesting thing that
    // can happen to a bin; a new object is mildly confusing. Neither can
    // move a motor - they only shift traits, like PLEASE and SORRY do.
    void noteHuman(uint32_t now);
    void noteObject(uint32_t now);

    // DRUNK mode wander, in mix percent. Applied by the pipeline, clamped by
    // the motor layer like everything else.
    int8_t  steerBias() const { return steerBias_; }
    // LAZY mode. 0 means "use the calibrated acceleration".
    uint8_t accelOverride() const;

  private:
    void    recomputeMood(bool announce);
    void    noteCommand(uint32_t now, uint8_t dir);
    void    nudge(uint8_t& v, int16_t delta);
    void    driftToward(uint8_t& v, uint8_t target, uint8_t step);
    uint16_t computeDelay(uint8_t verdict);
    uint8_t mangleSpeed(uint8_t requested, uint8_t verdict, const char*& quip);
    uint8_t wrongDirection(uint8_t asked);
    const char* pickReason(uint8_t verdict);

    Traits   t_{};
    Prng     rng_;
    uint8_t  mode_ = MODE_UNCOOPERATIVE;
    uint8_t  mood_ = MOOD_NORMAL;

    uint32_t lastTraitTick_ = 0;
    uint32_t lastCmdMs_     = 0;
    uint32_t panicUntil_    = 0;
    uint32_t graceUntil_    = 0;
    uint32_t graceStart_    = 0;
    uint8_t  modeBeforeGrace_ = MODE_UNCOOPERATIVE;
    uint8_t  modeBeforePanic_ = MODE_UNCOOPERATIVE;
    bool     normalTerminated_ = false;
    uint16_t normalHeldMs_  = 0;

    uint8_t  lastDir_     = DIR_STOP;
    uint8_t  repeatCount_ = 0;
    uint32_t lastApology_ = 0;
    uint8_t  apologyStreak_ = 0;

    int8_t   steerBias_     = 0;
    uint32_t lastBiasMs_    = 0;
};

extern Personality personality;
