// =============================================================================
//  pipeline.h - one command's journey, in the order the brief demands:
//
//      WEB BUTTON -> INPUT VALIDATION -> SAFETY CHECK -> PERSONALITY ENGINE
//      -> COMMAND TRANSFORMATION -> OPTIONAL DELAY -> MOTOR CONTROLLER
//      -> TELEMETRY -> WEB UI
//
//  The safety check happens before the personality engine is consulted, so
//  there is no code path in which a mood can decide whether a stop happens.
//
//  The optional delay is a scheduled timestamp, not a delay() call. Nothing
//  in this firmware blocks after setup() returns.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../core/types.h"
#include "../personality/personality.h"

// What the browser is told about a single command. Sent up to three times
// for one press: on arrival, when a delayed command starts, and when done.
struct CommandReport {
    bool        isDrive        = true;
    uint8_t     requested      = 0;
    uint8_t     actual         = 0;
    uint8_t     verdict        = V_OBEYED;
    uint16_t    delayMs        = 0;
    uint8_t     speedRequested = 0;
    uint8_t     speedActual    = 0;
    const char* reason         = "";
    const char* quip           = "";
    const char* phase          = "done";   // received | executing | done
    bool        simulated      = false;    // the target hardware is not fitted
};

typedef void (*FeedbackFn)(const CommandReport&);

class Pipeline {
  public:
    void begin(FeedbackFn fb);
    void handle(const InboundMsg& m, uint32_t now);
    void tick(uint32_t now);

    void cancelPending(const char* why);
    bool pending() const { return pendingBusy_; }

    uint8_t speedSetting() const { return speedPct_; }
    // Lid state is SIMULATED - there is no servo on this board. Never
    // reported to the browser as hardware.
    bool lidOpenSimulated() const { return lidOpen_; }

  private:
    void executeDrive(const Decision& d, uint32_t now);
    void report(const CommandReport& r);

    FeedbackFn fb_ = nullptr;
    uint8_t    speedPct_ = 60;

    bool       pendingBusy_ = false;
    uint32_t   pendingDue_  = 0;
    Decision   pendingDec_{};

    bool       lidOpen_ = false;
};

extern Pipeline pipeline;
