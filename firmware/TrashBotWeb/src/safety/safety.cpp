#include "safety.h"
#include <Arduino.h>
#include "../config/settings.h"
#include "../core/eventLog.h"
#include "../motor/motors.h"

Safety safety;

void Safety::begin() {
    estop_ = false;
    reason_ = "";
    lastLink_ = millis();
    lastDrive_ = 0;
    clients_ = 0;
    inhibited_ = true;
    inhibitReason_ = "NO CLIENT";
}

void Safety::triggerEstop(const char* why) {
    if (!estop_) {
        reason_ = why;          // pointer store; callers pass string literals
        estop_ = true;
        eventLog.push("SAFETY_STOP", "EMERGENCY STOP: %s", why);
    }
}

bool Safety::resetEstop() {
    if (!estop_) return false;
    estop_ = false;
    reason_ = "";
    // Targets are already zero and the drive timeout has long since expired,
    // so nothing moves until the user makes a fresh request. That is the
    // point of requiring a deliberate reset.
    lastDrive_ = 0;
    eventLog.push("SAFETY_RESET", "safety system reset by user");
    return true;
}

bool Safety::driveTimedOut(uint32_t now) const {
    return (uint32_t)(now - lastDrive_) > DRIVE_TIMEOUT_MS;
}

bool Safety::linkTimedOut(uint32_t now) const {
    return (uint32_t)(now - lastLink_) > LINK_TIMEOUT_MS;
}

void Safety::enforce(Motors& m, uint32_t now) {
    const char* why = nullptr;

    if (estop_)                 why = "EMERGENCY STOP";
    else if (clients_ == 0)     why = "NO CLIENT";
    else if (linkTimedOut(now)) why = "LINK TIMEOUT";

    if (why) {
        if (m.moving() || m.leftTarget() || m.rightTarget()) {
            m.hardStop();
            eventLog.push("SAFETY_STOP", "motors cut: %s", why);
        }
        if (!inhibited_ || inhibitReason_ != why) inhibitReason_ = why;
        inhibited_ = true;
        return;
    }

    if (inhibited_) {
        inhibited_ = false;
        inhibitReason_ = "";
    }

    // Hold-to-drive. A calibration jog runs its own expiry timer instead.
    if (!m.jogActive() && driveTimedOut(now) && (m.leftTarget() || m.rightTarget())) {
        m.setMix(0, 0, 0);
    }
}
