// =============================================================================
//  prng.h - the one source of randomness in the personality engine.
//
//  Deterministic on purpose. Same seed plus the same sequence of button
//  presses gives the same sequence of wrong answers, which is what makes the
//  behaviour read as deliberate rather than broken - and what lets you
//  rehearse a demo, or reproduce a bug someone reported.
//
//  The seed is printed at boot and can be pinned from the SETUP tab.
//  rand() is deliberately not used: it is shared with whatever else links
//  into the sketch, so it would not be reproducible.
// =============================================================================
#pragma once
#include <stdint.h>

class Prng {
  public:
    void seed(uint32_t s) {
        // xorshift32 locks up on zero and never recovers.
        state_ = s ? s : 0x2A2A2A2Au;
        initial_ = state_;
    }

    uint32_t initialSeed() const { return initial_; }

    uint32_t next() {
        uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state_ = x;
        return x;
    }

    // 0 .. n-1
    uint32_t pick(uint32_t n) { return n ? next() % n : 0; }

    // lo .. hi inclusive
    int32_t range(int32_t lo, int32_t hi) {
        if (hi <= lo) return lo;
        return lo + (int32_t)(next() % (uint32_t)(hi - lo + 1));
    }

    bool chance(uint8_t percent) {
        if (percent == 0) return false;
        if (percent >= 100) return true;
        return (next() % 100u) < percent;
    }

  private:
    uint32_t state_   = 0x2A2A2A2Au;
    uint32_t initial_ = 0x2A2A2A2Au;
};
