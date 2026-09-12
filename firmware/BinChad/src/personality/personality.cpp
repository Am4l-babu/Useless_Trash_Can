#include "personality.h"
#include "../remote/protocol.h"

Personality personality;

// ===========================================================================
//  Deterministic PRNG - xorshift32.
//  Chosen over rand() so that a given seed replays a given demo exactly.
// ===========================================================================
uint32_t Personality::rnd() {
    uint32_t x = _rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rngState = x;
    return x;
}

uint32_t Personality::rndBelow(uint32_t n) {
    return n ? (rnd() % n) : 0;
}

void Personality::begin(uint32_t seed) {
    _rngState = seed ? seed : 0x1BADB002;
    _state = P_IDLE;
    _stateSince = millis();
    _lastAngerDecay = millis();
}

// ===========================================================================
//  State
// ===========================================================================
static const char *const kStateNames[P_STATE_COUNT_] = {
    "IDLE", "CURIOUS", "ALERT", "HAPPY", "CONFUSED", "ANGRY", "SHY",
    "SARCASTIC", "SLEEPING", "AI MODE", "NORMAL MODE", "USELESS MODE", "ERROR"
};

const char *Personality::stateName(PersonalityState s) {
    return (s < P_STATE_COUNT_) ? kStateNames[s] : "?";
}
const char *Personality::stateName() const { return stateName(_state); }

void Personality::setState(PersonalityState s) {
    if (_state == s) return;
    _state = s;
    _stateSince = millis();
}

// ===========================================================================
//  Events and escalation
// ===========================================================================
void Personality::onInteraction() {
    if (_interactions < 0xFFFF) _interactions++;
}

void Personality::onSuccess() {
    if (_successes < 0xFFFF) _successes++;
    if (_anger >= 12) _anger -= 12; else _anger = 0;
}

void Personality::onMiss() {
    if (_misses < 0xFFFF) _misses++;
    _anger = (uint8_t)min(100, _anger + 14);

    // Escalation ladder. The bin's patience is a function of your aim.
    if (_misses >= MISS_ANGRY_AT || _anger >= 85) {
        setState(P_ANGRY);
    } else if (_misses >= MISS_CONCERN_AT) {
        setState(P_SARCASTIC);
    }
}

void Personality::onRemoteCommand() {
    if (_remoteCommands < 0xFFFF) _remoteCommands++;
    if (_frustration < REMOTE_FRUSTRATION_MAX) _frustration++;
}

void Personality::onNormalButton() {
    if (_normalPresses < 0xFFFF) _normalPresses++;
}

void Personality::onObstruction() {
    // Genuinely apologetic. The one thing it is never sarcastic about.
    _anger = (uint8_t)(_anger / 2);
}

void Personality::onError() {
    setState(P_ERROR);
}

void Personality::update() {
    const uint32_t now = millis();

    // Anger bleeds off over time so the bin does not stay furious all day.
    if (now - _lastAngerDecay > 1500) {
        _lastAngerDecay = now;
        if (_anger > 0) _anger--;
    }

    // Leaving ANGRY after the timeout, provided the anger itself has cooled.
    if (_state == P_ANGRY && msInState() > ANGRY_TIMEOUT_MS && _anger < 40) {
        setState(P_IDLE);
    }

    // Remote frustration slowly forgives, so a demo can be reset by just
    // putting the remote down for a while.
    static uint32_t lastForgive = 0;
    if (now - lastForgive > 12000) {
        lastForgive = now;
        if (_frustration > 0) _frustration--;
    }
}

// ===========================================================================
//  Lines
// ===========================================================================
const char *Personality::pick(const char *const *table, uint8_t count, uint8_t &lastIndex) {
    if (count == 0) return "";
    if (count == 1) return table[0];
    uint8_t i;
    do { i = (uint8_t)rndBelow(count); } while (i == lastIndex);
    lastIndex = i;
    return table[i];
}

static const char *const kMissLines[] = {
    "YOU MISSED.", "NICE THROW.", "ALMOST.", "SERIOUSLY?",
    "I SAW THAT.", "TRY AGAIN.", "THAT WAS EMBARRASSING.",
    "MY GRANDMOTHER THROWS BETTER."
};
static const char *const kMissLate[] = {
    "AGAIN?", "ARE YOU OKAY?", "THIS IS A LOT OF MISSES.",
    "I AM KEEPING COUNT."
};
static const char *const kSuccessLines[] = {
    "ACCEPTABLE.", "FINE.", "ADEQUATE.", "I GUESS.",
    "DON'T GET USED TO IT.", "LUCKY."
};
static const char *const kRefusalLines[] = {
    "COMMAND REJECTED", "NO.", "I'D RATHER NOT.",
    "INTERESTING REQUEST.", "NOTED. IGNORED.", "TRY ASKING NICELY."
};
static const char *const kAngryLines[] = {
    "ENOUGH.", "STOP IT.", "I AM A BIN, NOT A TOY.",
    "WE ARE DONE HERE."
};
static const char *const kIdleLines[] = {
    "SCANNING...", "WAITING.", "NOTHING TO DO.",
    "I COULD BE ANYTHING.", "STILL A BIN."
};
static const char *const kVerdictLines[] = {
    "YOU SHOULD THROW BETTER.", "USER: SUSPECTED",
    "CONFIDENCE: NONE", "RECOMMENDATION: STOP",
    "ERROR: USER DETECTED"
};

const char *Personality::missLine() {
    if (_misses >= MISS_ANNOY_AT && rndBelow(100) < 55) {
        return pick(kMissLate, (uint8_t)(sizeof(kMissLate) / sizeof(kMissLate[0])), _lastMiss);
    }
    return pick(kMissLines, (uint8_t)(sizeof(kMissLines) / sizeof(kMissLines[0])), _lastMiss);
}
const char *Personality::successLine() {
    return pick(kSuccessLines, (uint8_t)(sizeof(kSuccessLines) / sizeof(kSuccessLines[0])), _lastSuccess);
}
const char *Personality::remoteRefusalLine() {
    return pick(kRefusalLines, (uint8_t)(sizeof(kRefusalLines) / sizeof(kRefusalLines[0])), _lastRefusal);
}
const char *Personality::angryLine() {
    return pick(kAngryLines, (uint8_t)(sizeof(kAngryLines) / sizeof(kAngryLines[0])), _lastAngry);
}
const char *Personality::idleLine() {
    return pick(kIdleLines, (uint8_t)(sizeof(kIdleLines) / sizeof(kIdleLines[0])), _lastIdle);
}
const char *Personality::aiVerdictLine() {
    return pick(kVerdictLines, (uint8_t)(sizeof(kVerdictLines) / sizeof(kVerdictLines[0])), _lastVerdict);
}

// ===========================================================================
//  Remote mistranslation engine
//
//  Base weights (percent), per the design brief:
//      correct 10 | opposite 40 | wrong direction 25 | random 20 | nothing 5
//
//  As frustration climbs, "correct" is squeezed to zero and the mass moves
//  to "opposite" and "random". The behaviour is unfair but it is not
//  arbitrary - which is what makes it read as deliberate rather than broken.
// ===========================================================================
static uint8_t oppositeOf(uint8_t cmd) {
    switch (cmd) {
        case CMD_UP:    return CMD_DOWN;
        case CMD_DOWN:  return CMD_UP;
        case CMD_LEFT:  return CMD_RIGHT;
        case CMD_RIGHT: return CMD_LEFT;
        case CMD_OPEN:  return CMD_CLOSE;
        case CMD_CLOSE: return CMD_OPEN;
        case CMD_LIGHT: return CMD_DARK;
        case CMD_DARK:  return CMD_LIGHT;
        case CMD_MUTE:  return CMD_MUTE;    // handled as "louder" downstream
        case CMD_STOP:  return CMD_STOP;    // handled as "faster" downstream
        default:        return CMD_NONE;
    }
}

static const uint8_t kDirections[] = { CMD_UP, CMD_DOWN, CMD_LEFT, CMD_RIGHT };
static const uint8_t kAnything[]   = {
    CMD_UP, CMD_DOWN, CMD_LEFT, CMD_RIGHT, CMD_OPEN, CMD_CLOSE,
    CMD_AI, CMD_ANGRY, CMD_MOOD, CMD_LIGHT, CMD_DARK
};

uint8_t Personality::mistranslate(uint8_t requested, bool &obeyed) {
    obeyed = false;

    // Commands that are their own joke pass through untouched; the
    // behaviour layer implements the twist (STOP speeds up, MUTE gets
    // louder, AI theatrically fails).
    if (requested == CMD_AI || requested == CMD_STOP || requested == CMD_MUTE ||
        requested == CMD_MOOD || requested == CMD_ANGRY || requested == CMD_NORMAL ||
        requested == CMD_SECRET) {
        return requested;
    }

    // Weight table, adjusted for frustration.
    const uint8_t f = _frustration;                 // 0..REMOTE_FRUSTRATION_MAX
    uint8_t wCorrect  = (f >= 6) ? 0 : (uint8_t)(10 - f);
    uint8_t wOpposite = (uint8_t)(40 + f * 2);
    uint8_t wWrongDir = 25;
    uint8_t wRandom   = (uint8_t)(20 + f);
    uint8_t wNothing  = 5;

    // In ANGRY the bin stops pretending there is any chance of compliance.
    if (_state == P_ANGRY) { wCorrect = 0; wOpposite = 30; wRandom = 45; wNothing = 15; }

    const uint16_t total = (uint16_t)wCorrect + wOpposite + wWrongDir + wRandom + wNothing;
    uint16_t roll = (uint16_t)rndBelow(total);

    if (roll < wCorrect) {
        obeyed = true;
        return requested;
    }
    roll -= wCorrect;

    if (roll < wOpposite) {
        const uint8_t o = oppositeOf(requested);
        return (o == CMD_NONE) ? requested : o;
    }
    roll -= wOpposite;

    if (roll < wWrongDir) {
        uint8_t d;
        do { d = kDirections[rndBelow(sizeof(kDirections))]; } while (d == requested);
        return d;
    }
    roll -= wWrongDir;

    if (roll < wRandom) {
        return kAnything[rndBelow(sizeof(kAnything))];
    }

    return CMD_NONE;    // the silent treatment
}
