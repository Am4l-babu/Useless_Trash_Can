#include "behaviorManager.h"
#include "../config/pins.h"
#include "../hardware/sensors.h"
#include "../hardware/lid.h"
#include "../hardware/eye.h"
#include "../hardware/leds.h"
#include "../hardware/audio.h"
#include "../hardware/uselessFinger.h"
#include "../ui/display.h"
#include "../remote/remote.h"
#include "../remote/protocol.h"
#include "../personality/personality.h"

BehaviorManager behavior;

static const char *const kMainStateNames[] = {
    "BOOT", "SELF_TEST", "IDLE", "CURIOUS", "THROW_WAIT", "SUCCESS",
    "MISS_SILENCE", "MISS_REACT", "REMOTE_RESPONSE", "AI_THEATRE",
    "NORMAL_MODE", "SELF_DISABLE", "APOLOGY", "SLEEP", "FAULT"
};

const char *BehaviorManager::stateName() const { return kMainStateNames[_state]; }

void BehaviorManager::begin() {
    _lastInteraction = millis();
    enter(ST_SELF_TEST);
}

void BehaviorManager::enter(MainState s) {
    _state = s;
    _stateSince = millis();
    _step = 0;
    _stepAt = millis();
}

void BehaviorManager::forceSuccess() {
    _forcedSuccess = true;
}

// ===========================================================================
//  SELF TEST
//  Deliberately visible. It reassures the audience that the machine is
//  working before it starts pretending not to.
// ===========================================================================
void BehaviorManager::runSelfTest() {
    if (millis() - _stepAt < SELFTEST_STEP_MS) return;
    _stepAt = millis();

    switch (_step) {
    case 0:
        display.bootScreen();
        audio.play(SND_BOOT);
        leds.set(LED_AI);
        break;
    case 1:
        eye.look(EYE_USER);
        display.selfTestLine("EYE", true);
        break;
    case 2:
        leds.set(LED_ALERT);
        display.selfTestLine("LEDS", true);
        break;
    case 3:
        display.selfTestLine("AUDIO", audio.isAvailable());
        break;
    case 4:
        lid.peek();
        display.selfTestLine("LID", !lid.hasError());
        break;
    case 5:
        lid.close();
        break;
    case 6:
        finger.wiggle();          // a brief taunt, and a mechanism check
        display.selfTestLine("FINGER", true);
        break;
    case 7:
        display.selfTestLine("SENSORS", !sensors.anySensorDegraded());
        break;
    case 8:
        display.selfTestLine("REMOTE", remoteLink.isConnected());
        break;
    case 9:
        display.selfTestDone(100);
        leds.set(LED_IDLE);
        break;
    case 10:
        // A fault found during self-test is reported but is not fatal.
        // A bin that refuses to boot is not funny, it is just broken.
        if (lid.hasError()) {
            enter(ST_FAULT);
        } else {
            eye.center();
            display.setExpression(FACE_NEUTRAL);
            display.showFace();
            enter(ST_IDLE);
        }
        return;
    }
    _step++;
}

// ===========================================================================
//  FAULTS - checked every loop, from every state
// ===========================================================================
void BehaviorManager::handleFaults() {
    // Obstruction is the highest-priority non-boot event. The lid module has
    // already stopped and retreated by the time we see this flag; our job is
    // only to apologise.
    if (lid.consumeObstructionEvent()) {
        personality.onObstruction();
        display.setExpression(FACE_CONFUSED);
        display.say("OH.", "SORRY.", nullptr, 2200);
        audio.play(SND_CONFUSED);
        leds.flash(LED_ALERT, 1200);
        eye.look(EYE_INTO_BIN);
        enter(ST_APOLOGY);
        return;
    }

    if (lid.hasError() && _state != ST_FAULT) {
        personality.onError();
        display.say("LID ERROR", "Have you tried", "turning me off?", 0);
        leds.set(LED_ERROR);
        audio.play(SND_ERROR);
        enter(ST_FAULT);
        return;
    }

    if (finger.consumeFailureEvent()) {
        // The user is holding the NORMAL switch down. Fair enough.
        // Edge-triggered: hasFailed() would re-fire this every loop pass.
        display.say("FINGER JAMMED", "BY USER", nullptr, 2500);
        audio.play(SND_DENIED);
    }

    // Remote link edges, purely for comedy.
    if (remoteLink.consumeDisconnectEvent()) {
        display.say("REMOTE LOST", "Good.", nullptr, 1800);
    }
    if (remoteLink.consumeConnectEvent() && _state == ST_IDLE) {
        display.say("REMOTE FOUND", "Unfortunately.", nullptr, 1600);
    }
}

// ===========================================================================
//  SENSORS -> behaviour
// ===========================================================================
void BehaviorManager::handleSensors() {
    // The hidden demo trigger. Looks exactly like a successful throw.
    if (sensors.hiddenTriggerPressed()) {
        forceSuccess();
    }

    if (_state == ST_SLEEP) {
        if (sensors.personNear()) {
            eye.wake();
            leds.set(LED_IDLE);
            display.setExpression(FACE_SUSPICIOUS);
            display.say("WHO DISTURBED", "ME?", nullptr, 2000);
            audio.play(SND_CONFUSED);
            _lastInteraction = millis();
            enter(ST_IDLE);
        }
        return;
    }

    if (_state == ST_IDLE) {
        if (_personEdge) {
            personality.onInteraction();
            personality.setState(P_CURIOUS);
            _lastInteraction = millis();
            eye.look(EYE_USER);
            display.setExpression(FACE_SUSPICIOUS);
            display.say("OH NO.", nullptr, nullptr, 1600);
            audio.play(SND_DETECTED);
            leds.set(LED_ALERT);
            enter(ST_CURIOUS);
            return;
        }
        // Something came in without any warning - react anyway.
        if (_objectEdge) {
            _lastInteraction = millis();
            startThrowResponse();
            return;
        }
    }

    if (_state == ST_CURIOUS) {
        if (_objectEdge) {
            _lastInteraction = millis();
            startThrowResponse();
            return;
        }
        if (msInState() > 4000) {
            leds.set(LED_IDLE);
            display.setExpression(FACE_NEUTRAL);
            display.showFace();
            personality.setState(P_IDLE);
            enter(ST_IDLE);
        }
    }
}

// ===========================================================================
//  THROW HANDLING
// ===========================================================================
void BehaviorManager::startThrowResponse() {
    personality.onInteraction();
    personality.setState(P_ALERT);
    eye.look(EYE_INTO_BIN);
    display.setExpression(FACE_SCANNING);
    display.say("TRASH DETECTED", nullptr, nullptr, 1400);
    audio.play(SND_OPEN);
    leds.set(LED_ALERT);

    lid.setAutoClose(true);
    lid.holdOpenFor(LID_HOLD_OPEN_MS);

    _throatWasBlocked = false;
    _forcedSuccess = false;
    enter(ST_THROW_WAIT);
}

void BehaviorManager::reactSuccess() {
    personality.onSuccess();
    personality.setState(P_HAPPY);
    display.setExpression(FACE_HAPPY);
    display.say(personality.successLine(), nullptr, nullptr, 2000);
    audio.play(SND_SUCCESS);
    leds.set(LED_SUCCESS);
    eye.look(EYE_USER);
    lid.close();
    enter(ST_SUCCESS);
}

void BehaviorManager::reactMiss() {
    personality.onMiss();
    // The pause is the joke. Say nothing, look directly at the thrower,
    // and let it get uncomfortable.
    eye.look(EYE_USER, 500);
    display.setExpression(FACE_NEUTRAL);
    display.showFace();
    leds.set(LED_IDLE);
    lid.close();
    enter(ST_MISS_SILENCE);
}

// ===========================================================================
//  REMOTE
// ===========================================================================
void BehaviorManager::handleRemote() {
    const uint8_t requested = remoteLink.poll();
    if (requested == CMD_NONE || requested == CMD_PING) return;

    _lastInteraction = millis();
    personality.onRemoteCommand();

    bool obeyed = false;
    const uint8_t actual = personality.mistranslate(requested, obeyed);

    Serial.printf("[remote] asked %s -> doing %s%s\r\n",
                  binchad_cmd_name(requested), binchad_cmd_name(actual),
                  obeyed ? " (obeyed!)" : "");

    remoteLink.sendAck(remoteLink.lastSequence(), (uint8_t)personality.state(),
                       obeyed, personality.frustration());

    executeCommand(actual, obeyed);
}

void BehaviorManager::executeCommand(uint8_t cmd, bool obeyed) {
    switch (cmd) {

    case CMD_NONE:
        // The silent treatment. Not a bug: the bin heard you perfectly.
        display.say(personality.remoteRefusalLine(), nullptr, nullptr, 1600);
        leds.flash(LED_MISS, 400);
        enter(ST_REMOTE_RESPONSE);
        return;

    case CMD_OPEN:
        lid.setAutoClose(true);
        lid.holdOpenFor(LID_HOLD_OPEN_MS);
        audio.play(SND_OPEN);
        break;

    case CMD_CLOSE:
        lid.setAutoClose(false);
        lid.close();
        audio.play(SND_CLOSE);
        break;

    case CMD_UP:    eye.lookNormalized(0, +1.0f); break;
    case CMD_DOWN:  eye.lookNormalized(0, -1.0f); break;
    case CMD_LEFT:  eye.lookNormalized(-1.0f, 0); break;
    case CMD_RIGHT: eye.lookNormalized(+1.0f, 0); break;

    case CMD_OK:
        display.say("OK", "IS SUBJECTIVE", nullptr, 1600);
        break;

    case CMD_STOP:
        // "STOP" makes everything faster for a second. Obviously.
        display.sayBig("FASTER", 1000);
        eye.jitter(10);
        leds.flash(LED_CHAOS, 1000);
        if (!lid.isMoving()) lid.open(LID_SPEED_ANGRY);
        audio.play(SND_LAUGH);
        break;

    case CMD_AI:
        startAiTheatre();
        return;

    case CMD_ANGRY:
        personality.setState(P_ANGRY);
        display.setExpression(FACE_ANGRY);
        display.say(personality.angryLine(), nullptr, nullptr, 2000);
        audio.play(SND_ANGRY);
        leds.set(LED_ANGRY);
        eye.jitter(8);
        break;

    case CMD_MOOD: {
        const PersonalityState s =
            (PersonalityState)personality.rndBelow((uint32_t)P_STATE_COUNT_);
        personality.setState(s);
        display.say("NEW MOOD:", Personality::stateName(s), nullptr, 2000);
        leds.flash(LED_CHAOS, 900);
        break;
    }

    case CMD_MUTE:
        audio.jokeMute();
        display.say("MUTED", "(LOUDER)", nullptr, 1600);
        audio.play(SND_LAUGH);
        break;

    case CMD_LIGHT:
        leds.jokeLightsOff();
        display.say("LIGHTS ON", nullptr, nullptr, 1500);
        break;

    case CMD_DARK:
        leds.jokeBlinding();
        display.say("DARKNESS", nullptr, nullptr, 1500);
        break;

    case CMD_NORMAL:
        // Asking for normal mode over the radio does not count. There is a
        // button for that, and the button is the entire point.
        display.say("USE THE BUTTON.", nullptr, nullptr, 1800);
        eye.look(EYE_BUTTON);
        audio.play(SND_DENIED);
        break;

    case CMD_SECRET:
        display.say("YOU FOUND", "NOTHING.", nullptr, 2000);
        audio.play(SND_LAUGH);
        leds.flash(LED_CHAOS, 1500);
        break;

    default:
        break;
    }

    if (obeyed) {
        display.say("...FINE.", nullptr, nullptr, 1200);
    }
    personality.setState(obeyed ? P_SARCASTIC : P_USELESS_MODE);
    enter(ST_REMOTE_RESPONSE);
}

// ===========================================================================
//  AI THEATRE
//  No machine learning happens here, and the firmware does not claim
//  otherwise anywhere the user can see. It is a progress bar with opinions.
// ===========================================================================
void BehaviorManager::startAiTheatre() {
    personality.setState(P_AI_MODE);
    display.setExpression(FACE_SCANNING);
    leds.set(LED_AI);
    eye.look(EYE_USER);
    audio.play(SND_AI);
    _aiPercent = 0;
    enter(ST_AI_THEATRE);
}

// ===========================================================================
//  NORMAL MODE and the self-disable
// ===========================================================================
void BehaviorManager::handleNormalSwitch() {
    if (sensors.normalSwitchJustTurnedOn() && _state != ST_NORMAL_MODE
                                           && _state != ST_SELF_DISABLE) {
        startNormalMode();
    }
}

void BehaviorManager::startNormalMode() {
    personality.onNormalButton();
    personality.setState(P_NORMAL_MODE);
    _lastInteraction = millis();

#if USE_NORMAL_LAMP
    digitalWrite(PIN_NORMAL_LAMP, HIGH);
#endif

    display.say("NORMAL MODE", "ACTIVATED", nullptr, 0);
    leds.set(LED_NORMAL);
    audio.play(SND_SUCCESS);
    eye.center();
    eye.setIdleWander(false);
    enter(ST_NORMAL_MODE);
}

void BehaviorManager::startSelfDisable() {
    // The punchline. Look at the button first - telegraphing the move is
    // what turns a mechanism into a joke.
    eye.look(EYE_BUTTON);
    display.setExpression(FACE_SUSPICIOUS);
    display.showFace();
    leds.set(LED_IDLE);
    enter(ST_SELF_DISABLE);
}

// ===========================================================================
//  update()
// ===========================================================================
void BehaviorManager::update() {
    // Latch the sensor edges for this pass, before anything can early-return.
    // Every state below reads _objectEdge / _personEdge, never the sensor
    // one-shots directly, so an edge is always consumed exactly once.
    _objectEdge = sensors.objectJustEntered();
    _personEdge = sensors.personJustArrived();

    handleFaults();
    handleNormalSwitch();

    if (_state != ST_SELF_TEST && _state != ST_FAULT) {
        handleRemote();
        handleSensors();
    }

    switch (_state) {

    case ST_BOOT:
        enter(ST_SELF_TEST);
        break;

    case ST_SELF_TEST:
        runSelfTest();
        break;

    case ST_IDLE:
        if (personality.state() != P_IDLE && personality.state() != P_ANGRY)
            personality.setState(P_IDLE);
        if (millis() - _lastInteraction > SLEEP_AFTER_MS) {
            display.setExpression(FACE_SLEEPY);
            display.say("Zzz...", nullptr, nullptr, 2000);
            leds.set(LED_SLEEP);
            eye.sleep();
            personality.setState(P_SLEEPING);
            enter(ST_SLEEP);
        }
        break;

    case ST_CURIOUS:
        // handled in handleSensors()
        break;

    case ST_THROW_WAIT: {
        // Success = something actually passed through the throat, or the
        // operator rescued the demo with the hidden trigger.
        if (_forcedSuccess) { _forcedSuccess = false; reactSuccess(); break; }

        if (sensors.objectInThroat()) _throatWasBlocked = true;
        if (_throatWasBlocked && !sensors.objectInThroat() && msInState() > 300) {
            reactSuccess();
            break;
        }
        if (msInState() > THROW_WINDOW_MS) {
            reactMiss();
        }
        break;
    }

    case ST_SUCCESS:
        if (msInState() > REACTION_MIN_MS + 900) {
            leds.set(LED_IDLE);
            display.setExpression(FACE_NEUTRAL);
            display.showFace();
            personality.setState(P_IDLE);
            enter(ST_IDLE);
        }
        break;

    case ST_MISS_SILENCE:
        // Two full seconds of nothing. Resist the urge to shorten this.
        if (msInState() > MISS_SILENCE_MS) {
            display.say(personality.missLine(), nullptr, nullptr, 2400);
            audio.play(SND_MISS);
            leds.flash(LED_MISS, 800);
            display.setExpression(personality.anger() > 60 ? FACE_ANGRY : FACE_NEUTRAL);
            enter(ST_MISS_REACT);
        }
        break;

    case ST_MISS_REACT:
        if (msInState() > 2400) {
            if (personality.state() == P_ANGRY) {
                leds.set(LED_ANGRY);
                display.setExpression(FACE_ANGRY);
            } else {
                leds.set(LED_IDLE);
            }
            display.showFace();
            enter(ST_IDLE);
        }
        break;

    case ST_REMOTE_RESPONSE:
        if (msInState() > 1400) {
            if (personality.state() != P_ANGRY) leds.set(LED_IDLE);
            display.showFace();
            enter(ST_IDLE);
        }
        break;

    case ST_AI_THEATRE: {
        if (millis() - _stepAt < 260) break;
        _stepAt = millis();

        if (_aiPercent < 100) {
            // Climbs convincingly, then stalls at 99 like all real software.
            const uint8_t jump = (_aiPercent > 90) ? 1 : (uint8_t)(4 + personality.rndBelow(9));
            _aiPercent = (uint8_t)min(100, _aiPercent + jump);
            const char *label = (_aiPercent < 30) ? "CALIBRATING USER" :
                                (_aiPercent < 60) ? "ANALYZING THROW" :
                                (_aiPercent < 95) ? "PREDICTING INTENT" :
                                                    "UNDERSTANDING...";
            display.showProgress(label, _aiPercent);
        } else {
            display.say("RESULT:", personality.aiVerdictLine(), nullptr, 2600);
            audio.play(SND_LAUGH);
            leds.set(LED_CHAOS);
            // ...and then do something entirely unrelated.
            const uint8_t nonsense[] = { CMD_OPEN, CMD_CLOSE, CMD_LEFT, CMD_RIGHT, CMD_ANGRY };
            executeCommand(nonsense[personality.rndBelow(sizeof(nonsense))], false);
        }
        break;
    }

    case ST_NORMAL_MODE: {
        // For a few seconds it is a completely ordinary automatic bin.
        // This contrast is what makes the next part land.
        if (_objectEdge && !lid.isMoving()) {
            lid.setAutoClose(true);
            lid.holdOpenFor(2000);
            audio.play(SND_OPEN);
        }
        if (sensors.normalSwitchJustTurnedOff()) {
            // User switched it off themselves. Denied the punchline, the bin
            // sulks, but it does not deploy the finger for no reason.
#if USE_NORMAL_LAMP
            digitalWrite(PIN_NORMAL_LAMP, LOW);
#endif
            eye.setIdleWander(true);
            personality.setState(P_USELESS_MODE);
            display.say("I WAS GOING", "TO DO THAT.", nullptr, 2200);
            enter(ST_IDLE);
            break;
        }
        if (msInState() > NORMAL_MODE_GRACE_MS && !lid.isMoving()) {
            startSelfDisable();
        }
        break;
    }

    case ST_SELF_DISABLE: {
        switch (_step) {
        case 0:
            if (msInState() > 900) {          // the stare
                finger.deploy();
                _step = 1;
            }
            break;
        case 1:
            if (finger.consumePressEvent()) {
                audio.play(SND_DENIED);
                leds.flash(LED_MISS, 700);
                _step = 2;
            } else if (!finger.isBusy() && msInState() > 6000) {
                _step = 2;                    // mechanism gave up; carry on
            }
            break;
        case 2:
            if (!finger.isBusy()) {
#if USE_NORMAL_LAMP
                digitalWrite(PIN_NORMAL_LAMP, LOW);
#endif
                display.sayBig("NORMAL MODE", 900);
                _step = 3;
                _stepAt = millis();
            }
            break;
        case 3:
            if (millis() - _stepAt > 900) {
                display.say("NORMAL MODE", "CANCELLED", nullptr, 2600);
                audio.play(SND_LAUGH);
                leds.set(LED_CHAOS);
                eye.look(EYE_USER);
                personality.setState(P_USELESS_MODE);
                _step = 4;
                _stepAt = millis();
            }
            break;
        default:
            if (millis() - _stepAt > 2600) {
                eye.setIdleWander(true);
                leds.set(LED_IDLE);
                display.setExpression(FACE_NEUTRAL);
                display.showFace();
                enter(ST_IDLE);
            }
            break;
        }
        break;
    }

    case ST_APOLOGY:
        if (msInState() > 2400) {
            display.showFace();
            leds.set(LED_IDLE);
            enter(ST_IDLE);
        }
        break;

    case ST_SLEEP:
        // Waking is handled in handleSensors().
        break;

    case ST_FAULT:
        // Stay here, keep the display honest, but keep servicing the remote
        // so an operator can clear the fault without a power cycle.
        if (sensors.hiddenTriggerPressed()) {
            lid.clearError();
            leds.set(LED_IDLE);
            display.say("PRETENDING", "THAT'S FIXED", nullptr, 1800);
            enter(ST_IDLE);
        }
        break;
    }
}
