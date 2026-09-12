// =============================================================================
//  audio.h - short, sharp sound effects.
//
//  Two backends, selected at compile time in settings.h:
//
//    AUDIO_I2S_WAV  MAX98357A + 16-bit mono WAV files in LittleFS.
//                   Streamed AUDIO_CHUNK_BYTES per loop tick so playback
//                   never blocks the lid or the safety checks.
//
//    AUDIO_DFPLAYER DFPlayer Mini over UART, raw 10-byte frames. No external
//                   library, no dependency to go stale. Files are 0001.mp3,
//                   0002.mp3 ... in track order matching AudioClip below.
//
//  Clips must be SHORT. A one-second line lands; a three-second line kills
//  the pace of the interaction and the audience stops watching the bin.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum AudioClip : uint8_t {
    SND_NONE = 0,
    SND_BOOT,        // 0001.mp3 / boot.wav
    SND_DETECTED,
    SND_OPEN,
    SND_CLOSE,
    SND_MISS,
    SND_SUCCESS,
    SND_ANGRY,
    SND_CONFUSED,
    SND_LAUGH,
    SND_DENIED,
    SND_AI,
    SND_ERROR,
    SND_SHUTDOWN,
    SND_COUNT_
};

class Audio {
public:
    bool begin();
    void update();

    void play(AudioClip c);            // interrupts whatever is playing
    void playIfIdle(AudioClip c);      // politely does nothing if busy
    void stop();

    bool isPlaying() const { return _playing; }
    bool isAvailable() const { return _available; }

    void setVolume(uint8_t v);         // 0..30
    uint8_t volume() const { return _volume; }
    void jokeMute();                   // "MUTE" -> volume goes up

    static const char *filename(AudioClip c);

private:
    bool openClip(AudioClip c);
    void closeClip();

    bool     _available = false;
    bool     _playing   = false;
    uint8_t  _volume    = AUDIO_VOLUME_DEFAULT;
    AudioClip _current  = SND_NONE;
    uint32_t _bytesLeft = 0;
    uint32_t _startedAt = 0;
};

extern Audio audio;
