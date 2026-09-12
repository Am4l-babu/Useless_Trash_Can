// =============================================================================
//  types.h - the vocabulary every module agrees on.
//
//  Direction and ActionId are what the browser asks for AND what the bot
//  decides to do instead, which is why the two live in one enum each: a
//  transformation is just a different value of the same type, and the event
//  log can print "FORWARD -> BACKWARD" without a special case.
// =============================================================================
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------
enum Direction : uint8_t {
    DIR_STOP = 0,
    DIR_FORWARD,
    DIR_BACKWARD,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_FWD_LEFT,
    DIR_FWD_RIGHT,
    DIR_BACK_LEFT,
    DIR_BACK_RIGHT,
    DIR_COUNT
};

// ---------------------------------------------------------------------------
// Non-movement buttons
// ---------------------------------------------------------------------------
enum ActionId : uint8_t {
    ACT_NONE = 0,
    ACT_OPEN,
    ACT_CLOSE,
    ACT_PLEASE,
    ACT_SORRY,
    ACT_PANIC,
    ACT_DO_NOTHING,
    ACT_NORMAL_MODE,
    ACT_COUNT
};

// ---------------------------------------------------------------------------
// Drive modes - selected by the user, a policy rather than a feeling.
// MODE_NORMAL is the genuine debug mode: identity transform, zero delay.
// ---------------------------------------------------------------------------
enum DriveMode : uint8_t {
    MODE_NORMAL = 0,
    MODE_UNCOOPERATIVE,
    MODE_REVERSE,
    MODE_CHAOS,
    MODE_DRUNK,
    MODE_LAZY,
    MODE_ANGRY,
    MODE_PANIC,
    MODE_SLEEP,
    MODE_COUNT
};

// ---------------------------------------------------------------------------
// Moods - the bot's own state, derived from traits and the current mode.
// ---------------------------------------------------------------------------
enum Mood : uint8_t {
    MOOD_NORMAL = 0,
    MOOD_HAPPY,
    MOOD_BORED,
    MOOD_CONFUSED,
    MOOD_ANGRY,
    MOOD_REBELLIOUS,
    MOOD_SLEEPING,
    MOOD_CHAOS,
    MOOD_PANIC,
    MOOD_COUNT
};

// ---------------------------------------------------------------------------
// What happened to a command.
//
//   V_REJECTED is not a joke - it means validation or the safety layer said
//   no. Keeping it distinct from V_IGNORED is what lets the log tell
//   "deliberately unhelpful" apart from "actually broken".
// ---------------------------------------------------------------------------
enum Verdict : uint8_t {
    V_OBEYED = 0,
    V_MODIFIED,
    V_IGNORED,
    V_REJECTED,
    V_COUNT
};

// ---------------------------------------------------------------------------
// Inbound message kinds, after parsing. The web layer never acts on these
// itself - it validates, converts to this struct, and hands it to the queue.
// ---------------------------------------------------------------------------
enum MsgKind : uint8_t {
    MSG_NONE = 0,
    MSG_DRIVE,        // a: Direction, b: speed %, (v unused)
    MSG_ACTION,       // a: ActionId
    MSG_MODE,         // a: DriveMode
    MSG_SPEED,        // b: speed %
    MSG_ESTOP,
    MSG_RESET_SAFETY,
    MSG_CAL_JOG,      // a: 0 left / 1 right, b: 0 rev / 1 stop / 2 fwd
    MSG_CAL_SET,      // a: CalField, v: value
    MSG_PING,         // b: 1 if the sender has unlocked audio playback
    MSG_VISION,       // a: persons, b: confidence %, v: VisionSource, text: objects csv
    MSG_SOUND_TEST    // a: SoundEvent - fire it on every link, cooldowns bypassed
};

enum CalField : uint8_t {
    CAL_LEFT_INVERT = 0,
    CAL_RIGHT_INVERT,
    CAL_MAX_DUTY,
    CAL_MIN_DUTY,
    CAL_ACCEL,
    CAL_TRIM,
    CAL_SAVE,
    CAL_COUNT
};

// Client ids: WebSocket clients are small integers assigned by the server.
// The other links use these constants so the log can say where a command
// came from.
static const uint32_t CLIENT_ID_USB  = 0x55534200;   // "USB"
static const uint32_t CLIENT_ID_BLE  = 0x424C4500;   // "BLE" + connection handle
static const uint32_t CLIENT_ID_HTTP = 0x48545450;   // "HTTP" (the /api/vision POST)

static const uint8_t  INBOUND_TEXT_MAX = 48;

struct InboundMsg {
    uint8_t  kind;
    uint8_t  a;
    uint8_t  b;
    int16_t  v;
    uint32_t clientId;
    char     text[INBOUND_TEXT_MAX];   // MSG_VISION only: objects, comma separated
};

// ---------------------------------------------------------------------------
// Names. Inline so there is no .cpp to keep in sync; the browser gets these
// strings verbatim, so editing one changes the UI too.
// ---------------------------------------------------------------------------
inline const char* dirName(uint8_t d) {
    switch (d) {
        case DIR_STOP:       return "STOP";
        case DIR_FORWARD:    return "FORWARD";
        case DIR_BACKWARD:   return "BACKWARD";
        case DIR_LEFT:       return "LEFT";
        case DIR_RIGHT:      return "RIGHT";
        case DIR_FWD_LEFT:   return "FWD-LEFT";
        case DIR_FWD_RIGHT:  return "FWD-RIGHT";
        case DIR_BACK_LEFT:  return "BACK-LEFT";
        case DIR_BACK_RIGHT: return "BACK-RIGHT";
        default:             return "?";
    }
}

inline const char* actionName(uint8_t a) {
    switch (a) {
        case ACT_OPEN:        return "OPEN";
        case ACT_CLOSE:       return "CLOSE";
        case ACT_PLEASE:      return "PLEASE";
        case ACT_SORRY:       return "SORRY";
        case ACT_PANIC:       return "PANIC";
        case ACT_DO_NOTHING:  return "DO NOTHING";
        case ACT_NORMAL_MODE: return "NORMAL MODE";
        default:              return "NONE";
    }
}

inline const char* modeName(uint8_t m) {
    switch (m) {
        case MODE_NORMAL:         return "NORMAL";
        case MODE_UNCOOPERATIVE:  return "UNCOOPERATIVE";
        case MODE_REVERSE:        return "REVERSE";
        case MODE_CHAOS:          return "CHAOS";
        case MODE_DRUNK:          return "DRUNK";
        case MODE_LAZY:           return "LAZY";
        case MODE_ANGRY:          return "ANGRY";
        case MODE_PANIC:          return "PANIC";
        case MODE_SLEEP:          return "SLEEP";
        default:                  return "?";
    }
}

inline const char* moodName(uint8_t m) {
    switch (m) {
        case MOOD_NORMAL:      return "NORMAL";
        case MOOD_HAPPY:       return "HAPPY";
        case MOOD_BORED:       return "BORED";
        case MOOD_CONFUSED:    return "CONFUSED";
        case MOOD_ANGRY:       return "ANGRY";
        case MOOD_REBELLIOUS:  return "REBELLIOUS";
        case MOOD_SLEEPING:    return "SLEEPING";
        case MOOD_CHAOS:       return "CHAOS";
        case MOOD_PANIC:       return "PANIC";
        default:               return "?";
    }
}

inline const char* verdictName(uint8_t v) {
    switch (v) {
        case V_OBEYED:   return "OBEYED";
        case V_MODIFIED: return "MODIFIED";
        case V_IGNORED:  return "IGNORED";
        case V_REJECTED: return "REJECTED";
        default:         return "?";
    }
}

// ---------------------------------------------------------------------------
// Direction algebra
// ---------------------------------------------------------------------------
inline uint8_t oppositeDir(uint8_t d) {
    switch (d) {
        case DIR_FORWARD:    return DIR_BACKWARD;
        case DIR_BACKWARD:   return DIR_FORWARD;
        case DIR_LEFT:       return DIR_RIGHT;
        case DIR_RIGHT:      return DIR_LEFT;
        case DIR_FWD_LEFT:   return DIR_BACK_RIGHT;
        case DIR_FWD_RIGHT:  return DIR_BACK_LEFT;
        case DIR_BACK_LEFT:  return DIR_FWD_RIGHT;
        case DIR_BACK_RIGHT: return DIR_FWD_LEFT;
        default:             return DIR_STOP;
    }
}

// Differential mix, as hundredths of full scale per side. The diagonals run
// the inside track at 40% so a turn is a curve rather than a pirouette.
inline void dirToMix(uint8_t d, int16_t& left, int16_t& right) {
    switch (d) {
        case DIR_FORWARD:    left =  100; right =  100; break;
        case DIR_BACKWARD:   left = -100; right = -100; break;
        case DIR_LEFT:       left = -100; right =  100; break;
        case DIR_RIGHT:      left =  100; right = -100; break;
        case DIR_FWD_LEFT:   left =   40; right =  100; break;
        case DIR_FWD_RIGHT:  left =  100; right =   40; break;
        case DIR_BACK_LEFT:  left =  -40; right = -100; break;
        case DIR_BACK_RIGHT: left = -100; right =  -40; break;
        default:             left =    0; right =    0; break;
    }
}
