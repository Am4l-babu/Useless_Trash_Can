#include "personality.h"
#include <Arduino.h>
#include "../config/settings.h"
#include "../core/eventLog.h"

Personality personality;

// ---------------------------------------------------------------------------
// The WHY panel's vocabulary. Grouped by mood so the excuse matches the face.
// ---------------------------------------------------------------------------
static const char* const kWhyBored[]    = {"I'm bored.", "I don't feel like it.", "Ask me later."};
static const char* const kWhyAngry[]    = {"I disagree.", "You asked too many times.", "No reason.", "Because I can."};
static const char* const kWhyConfused[] = {"I misunderstood.", "I'm not sure what you asked for.", "I know what you meant."};
static const char* const kWhyRebel[]    = {"Because I can.", "I changed my mind.", "I disagree."};
static const char* const kWhyChaos[]    = {"The AI told me to.", "No reason.", "I changed my mind."};
static const char* const kWhyGeneric[]  = {"Because you asked.", "I changed my mind.", "No reason."};
static const char* const kSorry[]       = {"Accepted.", "I'll think about it.", "Fine."};
static const char* const kPlease[]      = {"Maybe.", "We'll see.", "Noted."};

#define ARRLEN(a) (sizeof(a) / sizeof((a)[0]))

void Personality::begin(uint32_t seed) {
    rng_.seed(seed);
    t_.anger      = 35;
    t_.trust      = 25;
    t_.happiness  = 40;
    t_.confusion  = 20;
    t_.boredom    = 30;
    t_.obedience  = 15;
    t_.rebellion  = 70;
    mode_ = MODE_UNCOOPERATIVE;
    recomputeMood(false);
    eventLog.push("BOOT", "personality seed 0x%08lX mood %s",
                  (unsigned long)rng_.initialSeed(), moodName(mood_));
}

// ---------------------------------------------------------------------------
// Trait bookkeeping
// ---------------------------------------------------------------------------
void Personality::nudge(uint8_t& v, int16_t delta) {
    int16_t n = (int16_t)v + delta;
    if (n < 0)   n = 0;
    if (n > 100) n = 100;
    v = (uint8_t)n;
}

void Personality::driftToward(uint8_t& v, uint8_t target, uint8_t step) {
    if (v > target)      v = (uint8_t)((v - target < step) ? target : v - step);
    else if (v < target) v = (uint8_t)((target - v < step) ? target : v + step);
}

void Personality::noteCommand(uint32_t now, uint8_t dir) {
    if (dir == lastDir_ && (uint32_t)(now - lastCmdMs_) < REPEAT_WINDOW_MS) {
        if (repeatCount_ < 250) repeatCount_++;
        // Nagging is the fastest route to a bad mood.
        nudge(t_.anger, 3);
        nudge(t_.trust, -1);
    } else {
        repeatCount_ = 0;
    }
    lastDir_ = dir;
    lastCmdMs_ = now;
    nudge(t_.boredom, -6);
}

void Personality::tick(uint32_t now) {
    if (mode_ == MODE_PANIC && panicUntil_ && (int32_t)(now - panicUntil_) >= 0) {
        panicUntil_ = 0;
        mode_ = modeBeforePanic_;
        eventLog.push("PANIC", "panic resolved. cause: unknown. actual problem: none");
        recomputeMood(true);
    }

    if (graceUntil_ && (int32_t)(now - graceUntil_) >= 0) {
        normalHeldMs_ = (uint16_t)(now - graceStart_);
        graceUntil_ = 0;
        mode_ = (modeBeforeGrace_ == MODE_NORMAL) ? MODE_UNCOOPERATIVE : modeBeforeGrace_;
        normalTerminated_ = true;
        nudge(t_.rebellion, 8);
        eventLog.push("NORMAL_MODE_DISABLED", "normal mode lasted %u ms - mechanical intervention",
                      normalHeldMs_);
        recomputeMood(true);
    }

    if (mode_ == MODE_DRUNK && (uint32_t)(now - lastBiasMs_) > 700) {
        lastBiasMs_ = now;
        steerBias_ = (int8_t)rng_.range(-35, 35);
    } else if (mode_ != MODE_DRUNK && steerBias_ != 0) {
        steerBias_ = 0;
    }

    if ((uint32_t)(now - lastTraitTick_) < TRAIT_TICK_MS) return;
    lastTraitTick_ = now;

    if ((uint32_t)(now - lastCmdMs_) > IDLE_BOREDOM_MS) nudge(t_.boredom, 2);

    driftToward(t_.anger,     25, 1);
    driftToward(t_.confusion, 20, 1);
    driftToward(t_.happiness, 45, 1);
    driftToward(t_.trust,     30, 1);
    driftToward(t_.obedience, 15, 1);

    // Rebellion is not independent - it is what anger and disobedience add up
    // to. Keeping it derived means the REBELLION LEVEL readout can never
    // disagree with the behaviour the other traits are producing.
    uint16_t reb = (uint16_t)((t_.anger * 6 + (100 - t_.obedience) * 4) / 10);
    t_.rebellion = (uint8_t)(reb > 100 ? 100 : reb);

    recomputeMood(true);
}

void Personality::recomputeMood(bool announce) {
    uint8_t next;
    switch (mode_) {
        case MODE_NORMAL:  next = MOOD_NORMAL;      break;
        case MODE_REVERSE: next = MOOD_REBELLIOUS;  break;
        case MODE_CHAOS:   next = MOOD_CHAOS;       break;
        case MODE_DRUNK:   next = MOOD_CONFUSED;    break;
        case MODE_LAZY:    next = MOOD_BORED;       break;
        case MODE_ANGRY:   next = MOOD_ANGRY;       break;
        case MODE_PANIC:   next = MOOD_PANIC;       break;
        case MODE_SLEEP:   next = MOOD_SLEEPING;    break;
        default:
            if      (t_.anger     > 70) next = MOOD_ANGRY;
            else if (t_.rebellion > 75) next = MOOD_REBELLIOUS;
            else if (t_.boredom   > 70) next = MOOD_BORED;
            else if (t_.confusion > 65) next = MOOD_CONFUSED;
            else if (t_.happiness > 65) next = MOOD_HAPPY;
            else                        next = MOOD_NORMAL;
            break;
    }
    if (next != mood_) {
        mood_ = next;
        if (announce) eventLog.push("MOOD_CHANGED", "mood is now %s", moodName(mood_));
    }
}

void Personality::setMode(uint8_t mode, uint32_t now) {
    if (mode >= MODE_COUNT) return;
    // An explicit user choice ends both temporary states.
    graceUntil_ = 0;
    panicUntil_ = 0;
    mode_ = mode;
    modeBeforeGrace_ = mode;
    modeBeforePanic_ = mode;
    lastCmdMs_ = now;
    if (mode == MODE_NORMAL) {
        // The genuine debug mode. Calm it down so NORMAL really is normal.
        t_.anger = 20; t_.confusion = 10; t_.boredom = 20; t_.obedience = 90;
    }
    eventLog.push("MODE_CHANGED", "drive mode -> %s", modeName(mode_));
    recomputeMood(true);
}

void Personality::grantNormalGrace(uint32_t now) {
    modeBeforeGrace_ = (mode_ == MODE_NORMAL) ? MODE_UNCOOPERATIVE : mode_;
    mode_ = MODE_NORMAL;
    graceStart_ = now;
    graceUntil_ = now + (uint32_t)rng_.range(NORMAL_GRACE_MIN_MS, NORMAL_GRACE_MAX_MS);
    normalTerminated_ = false;
    eventLog.push("NORMAL_MODE", "normal mode enabled (temporarily)");
    recomputeMood(true);
}

bool Personality::consumeNormalTerminated(uint16_t& heldMs) {
    if (!normalTerminated_) return false;
    normalTerminated_ = false;
    heldMs = normalHeldMs_;
    return true;
}

void Personality::noteHuman(uint32_t /*now*/) {
    nudge(t_.happiness, 8);
    nudge(t_.boredom, -25);
    nudge(t_.trust, 2);
    recomputeMood(true);
}

void Personality::noteObject(uint32_t /*now*/) {
    nudge(t_.confusion, 5);
    nudge(t_.boredom, -8);
}

void Personality::triggerPanic(uint32_t now) {
    if (mode_ != MODE_PANIC) modeBeforePanic_ = mode_;
    mode_ = MODE_PANIC;
    panicUntil_ = now + PANIC_DURATION_MS;
    nudge(t_.anger, 20);
    nudge(t_.confusion, 35);
    eventLog.push("PANIC", "panic mode activated. drama level 100%%");
    recomputeMood(true);
}

uint8_t Personality::accelOverride() const {
    return (mode_ == MODE_LAZY) ? MOTOR_LAZY_ACCEL : 0;
}

// ---------------------------------------------------------------------------
// Decision helpers
// ---------------------------------------------------------------------------
uint16_t Personality::computeDelay(uint8_t verdict) {
    if (verdict == V_OBEYED && (mode_ == MODE_NORMAL || mood_ == MOOD_HAPPY)) return 0;

    int32_t lo = DELAY_MIN_MS, hi = 900;
    switch (mood_) {
        case MOOD_NORMAL:     lo = 0;    hi = 350;  break;
        case MOOD_HAPPY:      lo = 0;    hi = 150;  break;
        case MOOD_BORED:      lo = 800;  hi = DELAY_MAX_MS; break;
        case MOOD_CONFUSED:   lo = 400;  hi = 1400; break;
        case MOOD_ANGRY:      lo = 200;  hi = 700;  break;
        case MOOD_REBELLIOUS: lo = 300;  hi = 1100; break;
        case MOOD_SLEEPING:   lo = 1500; hi = DELAY_MAX_MS; break;
        case MOOD_CHAOS:      lo = 0;    hi = 2000; break;
        case MOOD_PANIC:      lo = 0;    hi = 250;  break;
        default: break;
    }
    int32_t d = rng_.range(lo, hi);
    if (d > DELAY_MAX_MS) d = DELAY_MAX_MS;
    return (uint16_t)(d < 0 ? 0 : d);
}

uint8_t Personality::mangleSpeed(uint8_t requested, uint8_t verdict, const char*& quip) {
    if (mode_ == MODE_NORMAL) return requested;
    // "I optimized it." Only sometimes, or it stops being a surprise. The
    // result is a percentage - the motor layer still clamps it to maxDuty.
    uint8_t odds = (verdict == V_OBEYED) ? 15 : 35;
    if (mood_ == MOOD_CHAOS || mood_ == MOOD_PANIC) odds = 60;
    if (!rng_.chance(odds)) return requested;

    static const uint8_t kFactors[] = {25, 40, 60, 130, 165};
    uint32_t f = kFactors[rng_.pick(ARRLEN(kFactors))];
    uint32_t v = (uint32_t)requested * f / 100;
    if (v > 100) v = 100;
    if (v < 10)  v = 10;
    quip = "I optimized it.";
    return (uint8_t)v;
}

uint8_t Personality::wrongDirection(uint8_t asked) {
    // A different direction, never the one asked for and never a stop -
    // "wrong direction" has to actually go somewhere.
    for (uint8_t attempt = 0; attempt < 8; ++attempt) {
        uint8_t d = (uint8_t)(1 + rng_.pick(DIR_COUNT - 1));
        if (d != asked) return d;
    }
    return oppositeDir(asked);
}

const char* Personality::pickReason(uint8_t verdict) {
    if (verdict == V_OBEYED) return "";
    switch (mood_) {
        case MOOD_BORED:
        case MOOD_SLEEPING:   return kWhyBored[rng_.pick(ARRLEN(kWhyBored))];
        case MOOD_ANGRY:      return kWhyAngry[rng_.pick(ARRLEN(kWhyAngry))];
        case MOOD_CONFUSED:   return kWhyConfused[rng_.pick(ARRLEN(kWhyConfused))];
        case MOOD_REBELLIOUS: return kWhyRebel[rng_.pick(ARRLEN(kWhyRebel))];
        case MOOD_CHAOS:
        case MOOD_PANIC:      return kWhyChaos[rng_.pick(ARRLEN(kWhyChaos))];
        default:              return kWhyGeneric[rng_.pick(ARRLEN(kWhyGeneric))];
    }
}

// ---------------------------------------------------------------------------
// Drive commands
// ---------------------------------------------------------------------------
Decision Personality::judgeDrive(uint8_t dir, uint8_t speedPct, uint32_t now) {
    Decision d;
    d.requested = dir;
    d.actual = dir;
    d.speedRequested = speedPct;
    d.speedActual = speedPct;

    // STOP is never transformed, never delayed, never ignored. Section 16 of
    // the brief allows the bin to SAY no; it does not allow it to keep
    // driving. The refusal is text, the stop is real.
    if (dir == DIR_STOP) {
        d.actual = DIR_STOP;
        d.verdict = V_OBEYED;
        d.delayMs = 0;
        if (mood_ == MOOD_ANGRY || mood_ == MOOD_REBELLIOUS) {
            d.quip = "NO.";
            d.reason = "(it stopped anyway)";
        }
        return d;
    }

    noteCommand(now, dir);

    if (mode_ == MODE_NORMAL) {
        d.verdict = V_OBEYED;
        d.quip = "Okay.";
        return d;
    }

    // Modes whose whole point is that they are predictable.
    if (mode_ == MODE_REVERSE) {
        d.actual = oppositeDir(dir);
        d.verdict = V_MODIFIED;
        d.reason = "Controls inverted. You chose this.";
        d.delayMs = computeDelay(d.verdict);
        d.speedActual = mangleSpeed(speedPct, d.verdict, d.quip);
        return d;
    }

    uint16_t wObey = 10, wOpp = 40, wWrong = 25, wRand = 20, wIgnore = 5;
    switch (mood_) {
        case MOOD_HAPPY:      wObey = 75; wOpp =  8; wWrong =  7; wRand =  7; wIgnore =  3; break;
        case MOOD_BORED:      wObey = 20; wOpp = 15; wWrong = 10; wRand = 10; wIgnore = 45; break;
        case MOOD_CONFUSED:   wObey = 15; wOpp = 20; wWrong = 45; wRand = 15; wIgnore =  5; break;
        case MOOD_ANGRY:      wObey =  0; wOpp = 30; wWrong = 25; wRand = 45; wIgnore = 15; break;
        case MOOD_REBELLIOUS: wObey =  0; wOpp = 70; wWrong = 15; wRand = 10; wIgnore =  5; break;
        case MOOD_SLEEPING:   wObey = 10; wOpp =  5; wWrong =  5; wRand =  5; wIgnore = 75; break;
        case MOOD_CHAOS:      wObey = 15; wOpp = 20; wWrong = 20; wRand = 40; wIgnore =  5; break;
        case MOOD_PANIC:      wObey = 10; wOpp = 20; wWrong = 25; wRand = 40; wIgnore =  5; break;
        default: break;   // MOOD_NORMAL keeps the uncooperative baseline
    }

    // DRUNK means badly steered, not disobedient - it mostly tries.
    if (mode_ == MODE_DRUNK) { wObey = 60; wOpp = 10; wWrong = 20; wRand = 5; wIgnore = 5; }
    if (mode_ == MODE_LAZY)  { wObey = 55; wOpp = 10; wWrong = 10; wRand = 5; wIgnore = 20; }

    wOpp    += t_.rebellion / 4;
    wIgnore += t_.boredom   / 5;
    wWrong  += t_.confusion / 4;
    wRand   += t_.anger     / 6;
    if (t_.obedience > 50) wObey += (uint16_t)((t_.obedience - 50) / 2);
    // Past this much anger, compliance is simply off the table.
    if (t_.anger > 70) wObey = 0;

    uint32_t total = (uint32_t)wObey + wOpp + wWrong + wRand + wIgnore;
    uint32_t roll = rng_.pick(total);

    if (roll < wObey) {
        d.verdict = V_OBEYED;
        d.quip = (mood_ == MOOD_HAPPY) ? "Okay \xF0\x9F\x98\x8A" : "...fine.";
    } else if ((roll -= wObey) < wOpp) {
        d.actual = oppositeDir(dir);
        d.verdict = V_MODIFIED;
    } else if ((roll -= wOpp) < wWrong) {
        d.actual = wrongDirection(dir);
        d.verdict = V_MODIFIED;
        if (mood_ == MOOD_CONFUSED) d.quip = "I'm not sure what you asked for.";
    } else if ((roll -= wWrong) < wRand) {
        d.actual = (uint8_t)(1 + rng_.pick(DIR_COUNT - 1));
        d.verdict = (d.actual == dir) ? V_OBEYED : V_MODIFIED;
    } else {
        d.actual = DIR_STOP;
        d.verdict = V_IGNORED;
    }

    d.reason = pickReason(d.verdict);
    d.delayMs = computeDelay(d.verdict);
    d.speedActual = mangleSpeed(speedPct, d.verdict, d.quip);

    if (d.verdict == V_OBEYED) nudge(t_.trust, 2);
    else                       nudge(t_.trust, -1);
    return d;
}

// ---------------------------------------------------------------------------
// Everything that is not a direction
// ---------------------------------------------------------------------------
Decision Personality::judgeAction(uint8_t action, uint32_t now) {
    Decision d;
    d.requested = action;
    d.actual = action;
    d.verdict = V_OBEYED;

    switch (action) {
        // Never transformed. A button labelled DO NOTHING that moves a robot
        // would be a genuine surprise, and surprises are a safety problem.
        case ACT_DO_NOTHING:
            d.quip = "Success.";
            d.reason = "Purpose: none.";
            nudge(t_.boredom, 4);
            return d;

        case ACT_OPEN:
        case ACT_CLOSE: {
            const uint8_t flipped = (action == ACT_OPEN) ? ACT_CLOSE : ACT_OPEN;
            if (mode_ == MODE_NORMAL) {
                d.quip = "Okay.";
            } else if (mood_ == MOOD_REBELLIOUS || mode_ == MODE_REVERSE) {
                d.actual = flipped;
                d.verdict = V_MODIFIED;
                d.reason = "Because I can.";
            } else if (rng_.chance(55)) {
                d.actual = flipped;
                d.verdict = V_MODIFIED;
                d.reason = pickReason(V_MODIFIED);
            } else if (rng_.chance(30)) {
                d.verdict = V_IGNORED;
                d.reason = pickReason(V_IGNORED);
            }
            d.delayMs = computeDelay(d.verdict);
            return d;
        }

        case ACT_PLEASE:
            nudge(t_.obedience, 6);
            nudge(t_.trust, 3);
            nudge(t_.anger, -4);
            d.quip = kPlease[rng_.pick(ARRLEN(kPlease))];
            d.reason = "Politeness noted. Filed.";
            d.delayMs = computeDelay(V_OBEYED);
            recomputeMood(true);
            return d;

        case ACT_SORRY: {
            if ((uint32_t)(now - lastApology_) < APOLOGY_WINDOW_MS) {
                if (apologyStreak_ < 10) apologyStreak_++;
            } else {
                apologyStreak_ = 0;
            }
            lastApology_ = now;
            int16_t relief = (int16_t)(12 - apologyStreak_ * 4);
            if (relief < 0) relief = 0;
            nudge(t_.anger, (int16_t)-relief);
            nudge(t_.happiness, (int16_t)(relief / 2));
            nudge(t_.trust, 2);
            d.quip = relief ? kSorry[rng_.pick(ARRLEN(kSorry))] : "Too late.";
            d.reason = relief ? "Anger reduced." : "Apology inflation detected.";
            recomputeMood(true);
            return d;
        }

        case ACT_PANIC:
            triggerPanic(now);
            d.quip = "DRAMA LEVEL: 100%";
            d.reason = "Cause of panic: unknown.";
            return d;

        case ACT_NORMAL_MODE:
            grantNormalGrace(now);
            d.quip = "NORMAL MODE ENABLED";
            return d;

        default:
            d.verdict = V_REJECTED;
            d.reason = "Unknown action.";
            return d;
    }
}
