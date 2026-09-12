#include "pipeline.h"
#include <Arduino.h>
#include "../config/settings.h"
#include "../core/eventLog.h"
#include "../motor/motors.h"
#include "../safety/safety.h"

Pipeline pipeline;

void Pipeline::begin(FeedbackFn fb) {
    fb_ = fb;
    speedPct_ = 60;
    pendingBusy_ = false;
    lidOpen_ = false;
}

void Pipeline::report(const CommandReport& r) {
    if (fb_) fb_(r);
}

void Pipeline::cancelPending(const char* why) {
    if (!pendingBusy_) return;
    pendingBusy_ = false;
    eventLog.push("COMMAND_CANCELLED", "pending %s dropped: %s",
                  dirName(pendingDec_.actual), why);
}

// ---------------------------------------------------------------------------
void Pipeline::executeDrive(const Decision& d, uint32_t now) {
    motors.setAccelOverride(personality.accelOverride());

    if (d.verdict == V_IGNORED || d.actual == DIR_STOP) {
        motors.setMix(0, 0, 0);
        eventLog.push("COMMAND_EXECUTED", "%s -> nothing", dirName(d.requested));
        return;
    }

    int16_t l = 0, r = 0;
    dirToMix(d.actual, l, r);

    // DRUNK wander. Clamped here and clamped again by the motor layer.
    int8_t bias = personality.steerBias();
    if (bias) {
        l = (int16_t)(l + bias);
        r = (int16_t)(r - bias);
        if (l >  100) l =  100;
        if (l < -100) l = -100;
        if (r >  100) r =  100;
        if (r < -100) r = -100;
    }

    motors.setMix(l, r, d.speedActual);
    safety.noteDriveIntent(now);
    eventLog.push("COMMAND_EXECUTED", "%s at %u%% (L%d R%d)",
                  dirName(d.actual), d.speedActual, l, r);
}

// ---------------------------------------------------------------------------
void Pipeline::handle(const InboundMsg& m, uint32_t now) {
    switch (m.kind) {

    // -----------------------------------------------------------------
    case MSG_DRIVE: {
        // 1. INPUT VALIDATION
        if (m.a >= DIR_COUNT) {
            eventLog.push("COMMAND_REJECTED", "invalid direction %u", m.a);
            return;
        }
        uint8_t speed = m.b > 100 ? 100 : m.b;
        const uint8_t dir = m.a;

        // 2. SAFETY CHECK - before any personality is consulted.
        if (safety.estopActive()) {
            cancelPending("emergency stop");
            CommandReport r;
            r.requested = dir; r.actual = DIR_STOP; r.verdict = V_REJECTED;
            r.speedRequested = speed; r.speedActual = 0;
            r.reason = "EMERGENCY STOP ACTIVE";
            eventLog.push("COMMAND_REJECTED", "%s refused: emergency stop", dirName(dir));
            report(r);
            return;
        }

        if (dir == DIR_STOP) {
            // A stop supersedes anything still waiting to happen. Without
            // this, a delayed FORWARD could fire after the user let go.
            cancelPending("stop requested");
            Decision d = personality.judgeDrive(DIR_STOP, 0, now);
            motors.setMix(0, 0, 0);
            CommandReport r;
            r.requested = DIR_STOP; r.actual = DIR_STOP; r.verdict = V_OBEYED;
            r.reason = d.reason; r.quip = d.quip;
            eventLog.push("COMMAND_EXECUTED", "STOP");
            report(r);
            return;
        }

        safety.noteDriveIntent(now);

        // 3+4. PERSONALITY ENGINE and COMMAND TRANSFORMATION
        Decision d = personality.judgeDrive(dir, speed, now);

        CommandReport r;
        r.requested = d.requested; r.actual = d.actual; r.verdict = d.verdict;
        r.delayMs = d.delayMs;
        r.speedRequested = d.speedRequested; r.speedActual = d.speedActual;
        r.reason = d.reason; r.quip = d.quip;

        if (d.verdict == V_MODIFIED)
            eventLog.push("COMMAND_MODIFIED", "%s -> %s (%s)",
                          dirName(d.requested), dirName(d.actual), d.reason);
        else if (d.verdict == V_IGNORED)
            eventLog.push("COMMAND_REJECTED", "%s ignored (%s)",
                          dirName(d.requested), d.reason);

        // 5. OPTIONAL DELAY
        if (d.delayMs > 0) {
            pendingBusy_ = true;
            pendingDue_  = now + d.delayMs;
            pendingDec_  = d;
            r.phase = "received";
            eventLog.push("COMMAND_RECEIVED", "%s held for %u ms",
                          dirName(d.requested), d.delayMs);
            report(r);
            return;
        }

        // 6. MOTOR CONTROLLER
        executeDrive(d, now);
        r.phase = "done";
        report(r);                       // 7+8. TELEMETRY -> WEB UI
        return;
    }

    // -----------------------------------------------------------------
    case MSG_ACTION: {
        if (m.a >= ACT_COUNT || m.a == ACT_NONE) {
            eventLog.push("COMMAND_REJECTED", "invalid action %u", m.a);
            return;
        }
        Decision d = personality.judgeAction(m.a, now);

        CommandReport r;
        r.isDrive = false;
        r.requested = d.requested; r.actual = d.actual; r.verdict = d.verdict;
        r.delayMs = d.delayMs; r.reason = d.reason; r.quip = d.quip;

        if (m.a == ACT_OPEN || m.a == ACT_CLOSE) {
            // No servo is fitted. The lid state is a UI fiction and is
            // flagged as such all the way to the browser.
            r.simulated = !HW_LID_SERVO;
            if (d.verdict != V_IGNORED) lidOpen_ = (d.actual == ACT_OPEN);
            eventLog.push(lidOpen_ ? "LID_OPENED" : "LID_CLOSED",
                          "%s -> %s%s", actionName(d.requested), actionName(d.actual),
                          HW_LID_SERVO ? "" : " (simulated, no servo fitted)");
        } else {
            eventLog.push("ACTION", "%s -> %s", actionName(d.requested), verdictName(d.verdict));
        }
        report(r);
        return;
    }

    // -----------------------------------------------------------------
    case MSG_MODE:
        if (m.a >= MODE_COUNT) return;
        cancelPending("mode changed");
        personality.setMode(m.a, now);
        motors.setAccelOverride(personality.accelOverride());
        return;

    case MSG_SPEED: {
        uint8_t v = m.b;
        if (v < 10)  v = 10;
        if (v > 100) v = 100;
        speedPct_ = v;
        eventLog.push("SPEED_SET", "speed request %u%%", v);
        return;
    }

    // -----------------------------------------------------------------
    // The two that no personality state can influence.
    case MSG_ESTOP:
        cancelPending("emergency stop");
        motors.hardStop();
        safety.triggerEstop("user pressed EMERGENCY STOP");
        return;

    case MSG_RESET_SAFETY:
        if (!safety.resetEstop())
            eventLog.push("SAFETY_RESET", "reset ignored - no active emergency stop");
        return;

    // -----------------------------------------------------------------
    case MSG_CAL_JOG: {
        if (safety.estopActive()) {
            eventLog.push("COMMAND_REJECTED", "calibration jog refused: emergency stop");
            return;
        }
        cancelPending("calibration jog");
        int8_t dir = (m.b == 2) ? 1 : (m.b == 0 ? -1 : 0);
        motors.setAccelOverride(0);
        motors.jog(m.a == 0, dir, now);
        eventLog.push("CAL_JOG", "%s motor %s", m.a == 0 ? "left" : "right",
                      dir > 0 ? "forward" : (dir < 0 ? "reverse" : "stop"));
        return;
    }

    case MSG_CAL_SET: {
        MotorCal& c = motors.cal();
        switch (m.a) {
            case CAL_LEFT_INVERT:  c.leftInvert  = m.v != 0; break;
            case CAL_RIGHT_INVERT: c.rightInvert = m.v != 0; break;
            case CAL_MAX_DUTY:
                c.maxDuty = (uint8_t)constrain(m.v, 40, (int)MOTOR_DUTY_CEILING);
                if (c.minDuty >= c.maxDuty) c.minDuty = (uint8_t)(c.maxDuty / 3);
                break;
            case CAL_MIN_DUTY:
                c.minDuty = (uint8_t)constrain(m.v, 0, (int)c.maxDuty - 1);
                break;
            case CAL_ACCEL:
                c.accel = (uint8_t)constrain(m.v, 1, 60);
                break;
            case CAL_TRIM:
                c.trim = (int8_t)constrain(m.v, -20, 20);
                break;
            case CAL_SAVE:
                motors.saveCal();
                break;
            default:
                return;
        }
        return;
    }

    case MSG_PING:
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
void Pipeline::tick(uint32_t now) {
    if (!pendingBusy_) return;
    if ((int32_t)(now - pendingDue_) < 0) return;

    pendingBusy_ = false;

    // The world may have changed during the dramatic pause.
    if (safety.estopActive()) {
        eventLog.push("COMMAND_CANCELLED", "pending command dropped: emergency stop");
        return;
    }
    // Hold-to-drive: if the user has let go, the delayed command is stale and
    // must not start the motors.
    if (safety.driveTimedOut(now)) {
        eventLog.push("COMMAND_CANCELLED", "pending %s dropped: control released",
                      dirName(pendingDec_.actual));
        CommandReport r;
        r.requested = pendingDec_.requested; r.actual = DIR_STOP;
        r.verdict = V_IGNORED; r.phase = "done";
        r.reason = "You let go.";
        report(r);
        return;
    }

    CommandReport r;
    r.requested = pendingDec_.requested; r.actual = pendingDec_.actual;
    r.verdict = pendingDec_.verdict;
    r.speedRequested = pendingDec_.speedRequested;
    r.speedActual = pendingDec_.speedActual;
    r.reason = pendingDec_.reason; r.quip = pendingDec_.quip;
    r.phase = "executing";
    report(r);

    executeDrive(pendingDec_, now);
}
