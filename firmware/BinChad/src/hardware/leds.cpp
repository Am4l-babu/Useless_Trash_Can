#include "leds.h"
#include "../config/pins.h"
#include <Adafruit_NeoPixel.h>

Leds leds;

static Adafruit_NeoPixel strip(LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

bool Leds::begin() {
    strip.begin();
    strip.setBrightness(_brightness);
    strip.clear();
    strip.show();
    _ready = true;
    _lastFrame = millis();
    return true;
}

void Leds::set(LedPattern p) {
    if (_pattern == p) return;
    _pattern = p;
    _restore = p;
    _frame   = 0;
}

void Leds::flash(LedPattern p, uint16_t ms) {
    if (_flashUntil == 0) _restore = _pattern;
    _pattern    = p;
    _frame      = 0;
    _flashUntil = millis() + ms;
}

void Leds::setBrightness(uint8_t b) {
    _brightness = b;
    if (_ready) strip.setBrightness(b);
}

void Leds::jokeLightsOff() { setBrightness(LED_BRIGHTNESS); set(LED_OFF); }
void Leds::jokeBlinding()  { setBrightness(LED_BRIGHT_MAX); set(LED_NORMAL); }

void Leds::fill(uint8_t r, uint8_t g, uint8_t b) {
    for (uint16_t i = 0; i < LED_COUNT; ++i) strip.setPixelColor(i, r, g, b);
}

// Triangle-wave breathe, 0..255, without a sin() call per frame.
uint8_t Leds::breathe(uint32_t frame, uint16_t periodFrames) {
    const uint32_t phase = frame % periodFrames;
    const uint32_t half  = periodFrames / 2;
    const uint32_t up    = (phase < half) ? phase : (periodFrames - phase);
    return (uint8_t)((up * 255UL) / half);
}

void Leds::render(uint32_t f) {
    switch (_pattern) {

    case LED_IDLE: {
        const uint8_t v = 25 + (breathe(f, 160) / 6);     // 25..67, warm
        fill(v, (uint8_t)(v * 0.55f), 6);
        break;
    }

    case LED_ALERT: {
        const uint8_t v = (f % 8 < 4) ? 255 : 40;
        fill(v, (uint8_t)(v * 0.7f), 0);
        break;
    }

    case LED_SUCCESS: {
        // Green chase that runs once around and settles.
        const uint16_t head = (uint16_t)(f % (LED_COUNT * 2));
        for (uint16_t i = 0; i < LED_COUNT; ++i) {
            const uint8_t v = (i <= head) ? 200 : 20;
            strip.setPixelColor(i, 0, v, (uint8_t)(v / 8));
        }
        break;
    }

    case LED_MISS: {
        const uint8_t v = (f % 10 < 5) ? 255 : 0;
        fill(v, 0, 0);
        break;
    }

    case LED_ANGRY: {
        const bool odd = (f % 6) < 3;
        for (uint16_t i = 0; i < LED_COUNT; ++i) {
            const bool on = ((i % 2 == 0) == odd);
            strip.setPixelColor(i, on ? 255 : 90, on ? 30 : 0, 0);
        }
        break;
    }

    case LED_AI: {
        // Blue/purple scanner. Looks expensive, means nothing.
        const uint16_t head = (uint16_t)((f / 2) % LED_COUNT);
        for (uint16_t i = 0; i < LED_COUNT; ++i) {
            uint16_t d = (i > head) ? (i - head) : (head - i);
            if (d > LED_COUNT / 2) d = LED_COUNT - d;
            const uint8_t v = (d < 3) ? (uint8_t)(230 - d * 70) : 12;
            strip.setPixelColor(i, (uint8_t)(v / 3), 0, v);
        }
        break;
    }

    case LED_CHAOS:
        if (f % 3 == 0) {
            for (uint16_t i = 0; i < LED_COUNT; ++i)
                strip.setPixelColor(i, random(256), random(256), random(256));
        }
        break;

    case LED_SLEEP: {
        const uint8_t v = 3 + (breathe(f, 400) / 20);     // 3..15
        fill(0, (uint8_t)(v / 3), v);
        break;
    }

    case LED_NORMAL:
        fill(200, 200, 190);
        break;

    case LED_ERROR: {
        const uint16_t p = (uint16_t)(f % 40);
        const bool on = (p < 4) || (p >= 8 && p < 12);
        fill(on ? 255 : 0, on ? 120 : 0, 0);
        break;
    }

    case LED_OFF:
    default:
        fill(0, 0, 0);
        break;
    }
}

void Leds::update() {
    if (!_ready) return;
    const uint32_t now = millis();
    if (now - _lastFrame < LED_FRAME_MS) return;
    _lastFrame = now;

    if (_flashUntil && now > _flashUntil) {
        _flashUntil = 0;
        _pattern = _restore;
        _frame   = 0;
    }

    render(_frame++);
    strip.show();
}
