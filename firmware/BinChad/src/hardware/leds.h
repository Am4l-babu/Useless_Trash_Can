// =============================================================================
//  leds.h - WS2812B ring/strip around the bin opening.
//
//  One pattern is active at a time. Patterns are advanced by update() at
//  LED_FRAME_MS and are pure functions of (frame, pattern) - no blocking,
//  no delays, no per-pattern state to get stuck in.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum LedPattern : uint8_t {
    LED_IDLE,        // slow warm breathe
    LED_ALERT,       // fast white/amber pulse - something incoming
    LED_SUCCESS,     // green sweep
    LED_MISS,        // red flash
    LED_ANGRY,       // hard red/orange strobe
    LED_AI,          // blue/purple scanner
    LED_CHAOS,       // uniformly terrible
    LED_SLEEP,       // barely-there blue breathe
    LED_NORMAL,      // clean, boring white - "normal mode"
    LED_ERROR,       // amber double-blink
    LED_OFF
};

class Leds {
public:
    bool begin();
    void update();

    void set(LedPattern p);
    LedPattern pattern() const { return _pattern; }

    // Temporary override that reverts to the previous pattern afterwards.
    void flash(LedPattern p, uint16_t ms);

    void setBrightness(uint8_t b);
    uint8_t brightness() const { return _brightness; }

    // The LIGHT / DARK remote jokes.
    void jokeLightsOff();     // "LIGHT" -> everything goes dark
    void jokeBlinding();      // "DARK"  -> full brightness white

private:
    void render(uint32_t frame);
    void fill(uint8_t r, uint8_t g, uint8_t b);
    static uint8_t breathe(uint32_t frame, uint16_t periodFrames);

    LedPattern _pattern     = LED_IDLE;
    LedPattern _restore     = LED_IDLE;
    uint32_t   _flashUntil  = 0;
    uint32_t   _lastFrame   = 0;
    uint32_t   _frame       = 0;
    uint8_t    _brightness  = LED_BRIGHTNESS;
    bool       _ready       = false;
};

extern Leds leds;
