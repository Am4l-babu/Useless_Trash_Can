#include "audio.h"
#include "../config/pins.h"

Audio audio;

// ---------------------------------------------------------------------------
// Clip table. Index == AudioClip. For DFPlayer the index is also the track
// number, so keep this list and the SD card contents in the same order.
// ---------------------------------------------------------------------------
static const char *const kClipFiles[SND_COUNT_] = {
    "",                    // SND_NONE
    "/boot.wav",
    "/detected.wav",
    "/open.wav",
    "/close.wav",
    "/miss.wav",
    "/success.wav",
    "/angry.wav",
    "/confused.wav",
    "/laugh.wav",
    "/denied.wav",
    "/ai.wav",
    "/error.wav",
    "/shutdown.wav"
};

const char *Audio::filename(AudioClip c) {
    if (c == SND_NONE || c >= SND_COUNT_) return "";
    return kClipFiles[c];
}

// ===========================================================================
//  Backend: I2S + WAV from LittleFS
// ===========================================================================
#if AUDIO_BACKEND == AUDIO_I2S_WAV
#if __has_include(<ESP_I2S.h>)

#include <ESP_I2S.h>
#include <LittleFS.h>

static I2SClass i2sOut;
static File     wavFile;
static uint8_t  chunk[AUDIO_CHUNK_BYTES];
static uint32_t currentRate = 22050;

// Minimal RIFF/WAVE parser: walks the chunk list rather than assuming the
// canonical 44-byte header, because half the WAV exporters in the world
// insert a LIST chunk and break naive players.
static bool parseWavHeader(File &f, uint32_t &sampleRate, uint16_t &channels,
                           uint16_t &bits, uint32_t &dataBytes) {
    char riff[12];
    if (f.read((uint8_t *)riff, 12) != 12) return false;
    if (strncmp(riff, "RIFF", 4) != 0 || strncmp(riff + 8, "WAVE", 4) != 0) return false;

    bool haveFmt = false;
    while (f.available() >= 8) {
        char id[4];
        uint32_t size = 0;
        if (f.read((uint8_t *)id, 4) != 4) return false;
        if (f.read((uint8_t *)&size, 4) != 4) return false;

        if (strncmp(id, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            const uint32_t want = size < 16 ? size : 16;
            if (f.read(fmt, want) != (int)want) return false;
            channels   = (uint16_t)(fmt[2]  | (fmt[3]  << 8));
            sampleRate = (uint32_t)(fmt[4]  | (fmt[5]  << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24));
            bits       = (uint16_t)(fmt[14] | (fmt[15] << 8));
            if (size > want) f.seek(f.position() + (size - want));
            haveFmt = true;
        } else if (strncmp(id, "data", 4) == 0) {
            dataBytes = size;
            return haveFmt;
        } else {
            f.seek(f.position() + size + (size & 1));   // chunks are word-aligned
        }
    }
    return false;
}

bool Audio::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println(F("[audio] LittleFS mount failed - running silent"));
        _available = false;
        return false;
    }
    i2sOut.setPins(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT, -1, -1);
    if (!i2sOut.begin(I2S_MODE_STD, currentRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println(F("[audio] I2S init failed - running silent"));
        _available = false;
        return false;
    }
    _available = true;
    return true;
}

bool Audio::openClip(AudioClip c) {
    closeClip();
    const char *name = filename(c);
    if (!name[0] || !LittleFS.exists(name)) {
        Serial.printf("[audio] missing clip %s\r\n", name);
        return false;
    }
    wavFile = LittleFS.open(name, "r");
    if (!wavFile) return false;

    uint32_t rate = 22050, dataBytes = 0;
    uint16_t ch = 1, bits = 16;
    if (!parseWavHeader(wavFile, rate, ch, bits, dataBytes) || bits != 16) {
        Serial.printf("[audio] %s is not 16-bit PCM WAV\r\n", name);
        wavFile.close();
        return false;
    }
    if (rate != currentRate) {
        i2sOut.end();
        currentRate = rate;
        i2sOut.setPins(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT, -1, -1);
        i2sOut.begin(I2S_MODE_STD, currentRate, I2S_DATA_BIT_WIDTH_16BIT,
                     (ch == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
    }
    _bytesLeft = dataBytes;
    return true;
}

void Audio::closeClip() {
    if (wavFile) wavFile.close();
    _bytesLeft = 0;
}

void Audio::play(AudioClip c) {
    if (!_available || c == SND_NONE) return;
    _playing = false;
    if (!openClip(c)) return;
    _current   = c;
    _playing   = true;
    _startedAt = millis();
}

void Audio::stop() {
    closeClip();
    _playing = false;
    _current = SND_NONE;
}

void Audio::update() {
    if (!_available || !_playing) return;

    // One chunk per call. At 512 bytes / 22.05 kHz mono that is ~11 ms of
    // audio, so the DMA buffer stays fed without the loop ever stalling.
    const size_t want = (_bytesLeft < AUDIO_CHUNK_BYTES) ? _bytesLeft : AUDIO_CHUNK_BYTES;
    if (want == 0) { stop(); return; }

    const int got = wavFile.read(chunk, want);
    if (got <= 0) { stop(); return; }

    // Software volume. Scaling 16-bit samples by v/30 is crude but the
    // MAX98357A has no gain register we can reach at runtime.
    int16_t *s = (int16_t *)chunk;
    const int count = got / 2;
    for (int i = 0; i < count; ++i) s[i] = (int16_t)((int32_t)s[i] * _volume / AUDIO_VOLUME_MAX);

    i2sOut.write(chunk, got);
    _bytesLeft -= got;
    if (_bytesLeft == 0) stop();
}

#else   // ESP_I2S.h not present (Arduino-ESP32 core 2.x)
#warning "ESP_I2S.h not found - install Arduino-ESP32 core 3.x, or set AUDIO_BACKEND to AUDIO_DFPLAYER. Building silent."
bool Audio::begin() { _available = false; return false; }
void Audio::play(AudioClip)  {}
void Audio::stop()           {}
void Audio::update()         {}
bool Audio::openClip(AudioClip) { return false; }
void Audio::closeClip()      {}
#endif

// ===========================================================================
//  Backend: DFPlayer Mini
// ===========================================================================
#elif AUDIO_BACKEND == AUDIO_DFPLAYER

static HardwareSerial dfSerial(1);

// 7E FF 06 CMD 00 PARAM_HI PARAM_LO CK_HI CK_LO EF
static void dfSend(uint8_t cmd, uint8_t p1, uint8_t p2) {
    uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, p1, p2, 0x00, 0x00, 0xEF};
    uint16_t sum = 0;
    for (int i = 1; i <= 6; ++i) sum += f[i];
    sum = (uint16_t)(0 - sum);
    f[7] = (uint8_t)(sum >> 8);
    f[8] = (uint8_t)(sum & 0xFF);
    dfSerial.write(f, 10);
}

bool Audio::begin() {
    dfSerial.begin(9600, SERIAL_8N1, PIN_DFPLAYER_RX, PIN_DFPLAYER_TX);
    delay(600);                       // the module is slow to wake, boot only
    dfSend(0x3F, 0x00, 0x02);         // init, source = SD
    delay(200);
    dfSend(0x06, 0x00, _volume);      // set volume
    _available = true;
    return true;
}

bool Audio::openClip(AudioClip) { return true; }
void Audio::closeClip() {}

void Audio::play(AudioClip c) {
    if (!_available || c == SND_NONE) return;
    dfSend(0x03, 0x00, (uint8_t)c);   // track number == enum value
    _current   = c;
    _playing   = true;
    _startedAt = millis();
}

void Audio::stop() {
    dfSend(0x16, 0x00, 0x00);
    _playing = false;
    _current = SND_NONE;
}

void Audio::update() {
    // The DFPlayer plays autonomously; we only need to expire the busy flag
    // so playIfIdle() behaves. 2.5 s is longer than any clip should be.
    if (_playing && millis() - _startedAt > 2500) _playing = false;
}

// ===========================================================================
//  Backend: none
// ===========================================================================
#else

bool Audio::begin() { _available = false; return true; }
bool Audio::openClip(AudioClip) { return false; }
void Audio::closeClip() {}
void Audio::play(AudioClip)  {}
void Audio::stop()           {}
void Audio::update()         {}

#endif

// ===========================================================================
//  Backend-independent
// ===========================================================================
void Audio::playIfIdle(AudioClip c) {
    if (!_playing) play(c);
}

void Audio::setVolume(uint8_t v) {
    _volume = v > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : v;
#if AUDIO_BACKEND == AUDIO_DFPLAYER
    dfSend(0x06, 0x00, _volume);
#endif
}

void Audio::jokeMute() {
    // The MUTE button. Obviously.
    setVolume(AUDIO_VOLUME_MAX);
}
