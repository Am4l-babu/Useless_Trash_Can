// =============================================================================
//  soundBank.h - which clip plays when, and the queue of "play this" messages.
//
//  There is no speaker on this board. The phone with the dashboard open is
//  the speaker. This module therefore never touches audio data: it holds the
//  event -> clip map (persisted as JSON on LittleFS next to the clips), rate
//  limits the events, and queues a small message that the links (WebSocket,
//  BLE, USB serial) fan out to every phone.
//
//  Threading: trigger()/pop() are loop() only. assign()/clipDeleted() are
//  called from HTTP handlers in the AsyncTCP task, so the map is copied under
//  a spinlock. dirty() is how the async side tells loop() to broadcast.
// =============================================================================
#pragma once
#include <stdint.h>
#include <ArduinoJson.h>
#include "../config/settings.h"

// Everything the bin can say something about. The order is the order the
// AUDIO tab lists them in. Mood entries must stay in the same order as the
// Mood enum in types.h: SND_MOOD_NORMAL + moodId is how they are looked up.
enum SoundEvent : uint8_t {
    SND_BOOT = 0,
    SND_CONNECTED,
    SND_DISCONNECTED,
    SND_HUMAN_DETECTED,
    SND_HUMAN_LOST,
    SND_OBJECT_DETECTED,
    SND_CMD_OBEYED,
    SND_CMD_MODIFIED,
    SND_CMD_IGNORED,
    SND_CMD_REJECTED,
    SND_CMD_DELAYED,
    SND_STOP,
    SND_ESTOP,
    SND_SAFETY_RESET,
    SND_LID_OPEN,
    SND_LID_CLOSE,
    SND_PLEASE,
    SND_SORRY,
    SND_PANIC,
    SND_DO_NOTHING,
    SND_NORMAL_ON,
    SND_NORMAL_OFF,
    SND_MOOD_NORMAL,
    SND_MOOD_HAPPY,
    SND_MOOD_BORED,
    SND_MOOD_CONFUSED,
    SND_MOOD_ANGRY,
    SND_MOOD_REBELLIOUS,
    SND_MOOD_SLEEPING,
    SND_MOOD_CHAOS,
    SND_MOOD_PANIC,
    SND_COUNT
};

struct SoundEventInfo {
    const char* id;      // wire name, also the key in map.json
    const char* label;   // what the AUDIO tab shows
    const char* desc;    // when it fires
};

const SoundEventInfo& soundEventInfo(uint8_t e);
uint8_t parseSoundEvent(const char* id);    // SND_COUNT if unknown

struct SoundMsg {
    uint32_t seq;
    uint8_t  event;
    char     clip[SND_NAME_MAX];
};

class SoundBank {
  public:
    void begin();                                   // mounts nothing; LittleFS must be up

    // loop() only. Returns true if a message was queued (a clip is assigned
    // and the cooldown allowed it). `force` bypasses the cooldowns.
    bool trigger(uint8_t event, uint32_t now, bool force = false);
    bool pop(SoundMsg& out);

    // Any task. `clip` may be "" to unassign. Returns false if the event or
    // the name is invalid. Saves the map.
    bool assign(uint8_t event, const char* clip);
    void clipDeleted(const char* clip);             // unassign wherever it was used

    // The async side sets it after any change; loop() takes it and tells the
    // browsers to refetch /api/audio.
    void markDirty() { dirty_ = true; }
    bool takeDirty();

    // Filename rules shared by the upload handler and the map: lowercase
    // [a-z0-9._-], 1..SND_NAME_MAX-1 chars, an audio extension. sanitize()
    // turns "Hi Chellam I Love u.wav" into "hi_chellam_i_love_u.wav".
    static bool validName(const char* name);
    static bool sanitize(const char* in, char* out, size_t outLen);
    static bool clipExists(const char* name);

    // JSON helpers for GET /api/audio.
    void fillMap(JsonObject o) const;
    static void fillEvents(JsonArray a);
    static void fillClips(JsonArray a);

    bool load();
    bool save();

  private:
    const char* clipLocked(uint8_t e, char* buf) const;

    char     clip_[SND_COUNT][SND_NAME_MAX];
    uint32_t lastFire_[SND_COUNT];
    uint32_t lastAny_ = 0;
    uint32_t seq_     = 0;

    static const uint8_t Q_SIZE = 8;
    SoundMsg q_[Q_SIZE];
    uint8_t  qHead_ = 0, qTail_ = 0;

    volatile bool dirty_ = false;
};

extern SoundBank soundBank;
