// =============================================================================
//  safety.h - the layer no joke is allowed through.
//
//  The rule this whole project rests on: safety runs BEFORE the personality
//  engine, and the personality engine has no way to reach back into it. It
//  cannot clear the emergency stop, extend a timeout, or delay a stop.
//
//  Three independent inhibits, any one of which cuts the motors:
//
//    1. EMERGENCY STOP  latched, cleared only by an explicit reset message
//    2. NO CLIENTS      the browser is gone; nothing is steering
//    3. LINK TIMEOUT    a client is nominally connected but has gone quiet
//
//  Plus one softer one: DRIVE TIMEOUT ramps the target to zero when the user
//  stops holding the control. Hold-to-drive is deliberate - a mobile robot
//  that keeps going after you let go is a hazard, and "it wouldn't stop" is
//  not the joke this machine is making.
// =============================================================================
#pragma once
#include <stdint.h>

class Motors;

class Safety {
  public:
    void begin();

    // Safe to call from the AsyncTCP callback task. `why` must be a string
    // literal or otherwise have static lifetime.
    void triggerEstop(const char* why);
    bool resetEstop();

    bool        estopActive() const { return estop_; }
    const char* estopReason() const { return reason_; }

    void noteLink(uint32_t now)        { lastLink_ = now; }
    void noteDriveIntent(uint32_t now) { lastDrive_ = now; }
    void setClientCount(uint8_t n)     { clients_ = n; }
    uint8_t clientCount() const        { return clients_; }

    bool driveTimedOut(uint32_t now) const;
    bool linkTimedOut(uint32_t now) const;

    // The one place that decides whether the motors may run. Called every
    // loop iteration, after commands are processed and before motors.tick().
    void enforce(Motors& m, uint32_t now);

    bool        motorsInhibited() const { return inhibited_; }
    const char* inhibitReason()   const { return inhibitReason_; }

  private:
    volatile bool     estop_    = false;
    const char*       reason_   = "";
    volatile uint32_t lastLink_ = 0;
    uint32_t          lastDrive_ = 0;
    uint8_t           clients_  = 0;
    bool              inhibited_ = false;
    const char*       inhibitReason_ = "";
};

extern Safety safety;
