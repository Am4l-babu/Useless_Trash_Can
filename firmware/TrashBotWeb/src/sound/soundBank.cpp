#include "soundBank.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>
#include <ctype.h>
#include "../core/eventLog.h"

SoundBank soundBank;

static portMUX_TYPE mapMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// The catalogue. Index == SoundEvent.
// ---------------------------------------------------------------------------
static const SoundEventInfo kEvents[SND_COUNT] = {
    {"boot",             "Boot",              "The bin has just started (USB and BLE only - no browser is connected yet)."},
    {"connected",        "Phone connected",   "A browser or a BLE phone joins."},
    {"disconnected",     "Phone left",        "A browser or a BLE phone leaves."},
    {"human_detected",   "Human detected",    "A person walks into the camera's view."},
    {"human_lost",       "Human gone",        "The person has been out of view for a while."},
    {"object_detected",  "Object spotted",    "The camera sees something it has not seen recently (cup, bottle, phone...)."},
    {"command_obeyed",   "Obeyed",            "A drive command was carried out as asked. Rare."},
    {"command_modified", "Modified",          "A drive command was changed into something else."},
    {"command_ignored",  "Ignored",           "A drive command was heard and skipped."},
    {"command_rejected", "Rejected",          "The safety layer refused a command. Not a joke."},
    {"command_delayed",  "Thinking about it", "A drive command is being sat on for a moment."},
    {"stop",             "Stop",              "The controls were released."},
    {"estop",            "EMERGENCY STOP",    "The red button. Bypasses every cooldown."},
    {"safety_reset",     "Safety reset",      "The emergency stop was cleared."},
    {"lid_open",         "Lid open",          "OPEN went through (simulated until a servo is fitted)."},
    {"lid_close",        "Lid close",         "CLOSE went through (simulated until a servo is fitted)."},
    {"please",           "Please",            "Someone said please."},
    {"sorry",            "Sorry",             "Someone apologised."},
    {"panic",            "Panic",             "PANIC was pressed."},
    {"do_nothing",       "Do nothing",        "DO NOTHING was pressed and nothing was done."},
    {"normal_mode_on",   "Normal mode on",    "The green button worked. For now."},
    {"normal_mode_off",  "Normal mode off",   "Mechanical intervention. Normal mode is over."},
    {"mood_normal",      "Mood: normal",      "Mood changed to NORMAL."},
    {"mood_happy",       "Mood: happy",       "Mood changed to HAPPY."},
    {"mood_bored",       "Mood: bored",       "Mood changed to BORED."},
    {"mood_confused",    "Mood: confused",    "Mood changed to CONFUSED."},
    {"mood_angry",       "Mood: angry",       "Mood changed to ANGRY."},
    {"mood_rebellious",  "Mood: rebellious",  "Mood changed to REBELLIOUS."},
    {"mood_sleeping",    "Mood: sleeping",    "Mood changed to SLEEPING."},
    {"mood_chaos",       "Mood: chaos",       "Mood changed to CHAOS."},
    {"mood_panic",       "Mood: panic",       "Mood changed to PANIC."},
};

const SoundEventInfo& soundEventInfo(uint8_t e) {
    static const SoundEventInfo unknown = {"?", "?", ""};
    return e < SND_COUNT ? kEvents[e] : unknown;
}

uint8_t parseSoundEvent(const char* id) {
    if (!id) return SND_COUNT;
    for (uint8_t i = 0; i < SND_COUNT; ++i)
        if (strcmp(id, kEvents[i].id) == 0) return i;
    return SND_COUNT;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
static const char* const kExt[] = {".wav", ".mp3", ".aac", ".m4a", ".ogg", ".opus", ".webm", ".flac"};

static bool hasAudioExt(const char* name) {
    size_t n = strlen(name);
    for (size_t i = 0; i < sizeof(kExt) / sizeof(kExt[0]); ++i) {
        size_t e = strlen(kExt[i]);
        if (n > e && strcmp(name + n - e, kExt[i]) == 0) return true;
    }
    return false;
}

bool SoundBank::validName(const char* name) {
    if (!name) return false;
    size_t n = strlen(name);
    if (n == 0 || n >= SND_NAME_MAX) return false;
    if (name[0] == '.') return false;                   // no dotfiles, no "..", no map.json tricks
    for (size_t i = 0; i < n; ++i) {
        char c = name[i];
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) ||
              c == '.' || c == '_' || c == '-')) return false;
    }
    if (strcmp(name, "map.json") == 0) return false;
    return hasAudioExt(name);
}

bool SoundBank::sanitize(const char* in, char* out, size_t outLen) {
    if (!in || outLen < 2) return false;
    // Drop any path the browser may have attached.
    const char* base = in;
    for (const char* p = in; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;

    size_t o = 0;
    bool lastSep = true;                                 // collapse and trim separators
    for (const char* p = base; *p && o < outLen - 1; ++p) {
        char c = (char)tolower((unsigned char)*p);
        if (isalnum((unsigned char)c) || c == '.' || c == '-') {
            // "beep (1).wav" -> the ')' became '_' and would sit before the
            // dot; a separator directly before a dot is never wanted.
            if (c == '.' && o > 0 && (out[o - 1] == '_' || out[o - 1] == '-')) --o;
            out[o++] = c;
            lastSep = (c == '.' || c == '-');
        } else if (!lastSep) {
            out[o++] = '_';
            lastSep = true;
        }
    }
    // Trim a trailing separator left by "name .wav" style input.
    while (o > 0 && (out[o - 1] == '_' || out[o - 1] == '-')) --o;
    out[o] = 0;
    // "_.wav" -> a leading separator is not useful either.
    while (out[0] == '_' || out[0] == '-') memmove(out, out + 1, strlen(out));
    return validName(out);
}

bool SoundBank::clipExists(const char* name) {
    if (!validName(name)) return false;
    String path = String(SND_DIR "/") + name;
    return LittleFS.exists(path);
}

// ---------------------------------------------------------------------------
void SoundBank::begin() {
    memset(clip_, 0, sizeof(clip_));
    memset(lastFire_, 0, sizeof(lastFire_));
    if (!LittleFS.exists(SND_DIR)) LittleFS.mkdir(SND_DIR);
    if (!load()) eventLog.push("AUDIO", "no clip map yet - nothing is assigned");
}

bool SoundBank::load() {
    File f = LittleFS.open(SND_MAP_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        eventLog.push("AUDIO", "map.json unreadable (%s) - starting empty", err.c_str());
        return false;
    }
    uint8_t assigned = 0;
    portENTER_CRITICAL(&mapMux);
    for (uint8_t e = 0; e < SND_COUNT; ++e) {
        const char* c = doc[kEvents[e].id] | "";
        clip_[e][0] = 0;
        if (validName(c)) {
            strlcpy(clip_[e], c, SND_NAME_MAX);
            ++assigned;
        }
    }
    portEXIT_CRITICAL(&mapMux);
    eventLog.push("AUDIO", "clip map loaded, %u event(s) assigned", assigned);
    return true;
}

bool SoundBank::save() {
    JsonDocument doc;
    char buf[SND_NAME_MAX];
    for (uint8_t e = 0; e < SND_COUNT; ++e) {
        clipLocked(e, buf);
        if (buf[0]) doc[kEvents[e].id] = buf;
    }
    File f = LittleFS.open(SND_MAP_PATH, "w");
    if (!f) {
        eventLog.push("AUDIO", "could not write map.json");
        return false;
    }
    serializeJson(doc, f);
    f.close();
    return true;
}

const char* SoundBank::clipLocked(uint8_t e, char* buf) const {
    portENTER_CRITICAL(&mapMux);
    strlcpy(buf, clip_[e], SND_NAME_MAX);
    portEXIT_CRITICAL(&mapMux);
    return buf;
}

// ---------------------------------------------------------------------------
bool SoundBank::assign(uint8_t event, const char* clip) {
    if (event >= SND_COUNT) return false;
    if (!clip) clip = "";
    if (clip[0] && !clipExists(clip)) return false;
    portENTER_CRITICAL(&mapMux);
    strlcpy(clip_[event], clip, SND_NAME_MAX);
    portEXIT_CRITICAL(&mapMux);
    eventLog.push("AUDIO_MAPPED", "%s -> %s", kEvents[event].id, clip[0] ? clip : "(nothing)");
    return save();
}

void SoundBank::clipDeleted(const char* clip) {
    if (!clip || !clip[0]) return;
    bool changed = false;
    portENTER_CRITICAL(&mapMux);
    for (uint8_t e = 0; e < SND_COUNT; ++e) {
        if (strcmp(clip_[e], clip) == 0) { clip_[e][0] = 0; changed = true; }
    }
    portEXIT_CRITICAL(&mapMux);
    if (changed) save();
}

bool SoundBank::takeDirty() {
    if (!dirty_) return false;
    dirty_ = false;
    return true;
}

// ---------------------------------------------------------------------------
bool SoundBank::trigger(uint8_t event, uint32_t now, bool force) {
    if (event >= SND_COUNT) return false;

    char clip[SND_NAME_MAX];
    clipLocked(event, clip);
    if (!clip[0]) return false;                          // nothing assigned: silence

    if (!force && event != SND_ESTOP) {
        if (lastFire_[event] && (uint32_t)(now - lastFire_[event]) < SND_EVENT_COOLDOWN_MS) return false;
        if (lastAny_ && (uint32_t)(now - lastAny_) < SND_GLOBAL_GAP_MS) return false;
    }

    uint8_t next = (uint8_t)((qHead_ + 1) % Q_SIZE);
    if (next == qTail_) return false;                    // the phone is behind; drop, don't block

    SoundMsg& m = q_[qHead_];
    m.seq   = ++seq_;
    m.event = event;
    strlcpy(m.clip, clip, SND_NAME_MAX);
    qHead_ = next;

    lastFire_[event] = now ? now : 1;
    lastAny_ = now ? now : 1;
    return true;
}

bool SoundBank::pop(SoundMsg& out) {
    if (qTail_ == qHead_) return false;
    out = q_[qTail_];
    qTail_ = (uint8_t)((qTail_ + 1) % Q_SIZE);
    return true;
}

// ---------------------------------------------------------------------------
void SoundBank::fillMap(JsonObject o) const {
    char buf[SND_NAME_MAX];
    for (uint8_t e = 0; e < SND_COUNT; ++e) {
        clipLocked(e, buf);
        o[kEvents[e].id] = buf;                          // "" means unassigned; the UI needs every key
    }
}

void SoundBank::fillEvents(JsonArray a) {
    for (uint8_t e = 0; e < SND_COUNT; ++e) {
        JsonObject o = a.add<JsonObject>();
        o["n"]     = e;
        o["id"]    = kEvents[e].id;
        o["label"] = kEvents[e].label;
        o["desc"]  = kEvents[e].desc;
    }
}

void SoundBank::fillClips(JsonArray a) {
    File dir = LittleFS.open(SND_DIR);
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            const char* name = f.name();
            // Core 2.x returns the bare name; older cores returned the full
            // path. Take whatever follows the last slash either way.
            const char* slash = strrchr(name, '/');
            if (slash) name = slash + 1;
            if (validName(name)) {
                JsonObject o = a.add<JsonObject>();
                o["name"] = name;
                o["size"] = (uint32_t)f.size();
            }
        }
        f = dir.openNextFile();
    }
}
