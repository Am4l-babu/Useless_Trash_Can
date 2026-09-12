// =============================================================================
//  TRASHBOT WEB REMOTE
//
//  A browser-based command centre for a trash can that has developed an
//  attitude. The phone IS the remote: the ESP32 serves the dashboard and
//  takes commands over a WebSocket, then decides how much of each one it
//  feels like honouring.
//
//  The phone is also the speaker and the eyes. The bin never holds audio
//  data or a camera frame: it sends a tiny "play this" message (over Wi-Fi,
//  BLE and USB serial, all at once) and receives "I saw a person and a cup"
//  reports from wherever the detector runs. See src/sound/ and src/vision/.
//
//  Hardware for this build:  ESP32 dev module + L298N + 2 DC motors.
//  Nothing else is required, and nothing else is pretended to exist.
//
//  Loop order matters and is not arbitrary:
//
//      drain inbound queue -> pipeline (validate, safety, personality)
//      -> personality tick -> vision tick -> pending delayed commands
//      -> SAFETY ENFORCE -> motor ramp -> sounds for what just changed
//      -> telemetry out on every link
//
//  Safety::enforce() runs after every possible command source and before the
//  motors are written, so it always has the last word.
//
//  There is no delay() after setup() returns.
//
//  See README.md in this folder for wiring, libraries and flashing.
// =============================================================================
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

#include "src/config/settings.h"
#include "src/core/eventLog.h"
#include "src/motor/motors.h"
#include "src/safety/safety.h"
#include "src/personality/personality.h"
#include "src/command/pipeline.h"
#include "src/sound/soundBank.h"
#include "src/vision/vision.h"
#include "src/net/webLayer.h"
#include "src/net/bleLink.h"
#include "src/net/usbLink.h"

static bool     apMode = false;
static uint32_t lastStaRetry = 0;

// ---------------------------------------------------------------------------
static uint32_t chooseSeed() {
    Preferences p;
    p.begin("trashbot", true);
    uint32_t pinned = p.getULong("seed", 0);
    p.end();
    // A pinned seed makes a demo reproducible: same seed, same sequence of
    // wrong answers. Zero means "surprise me".
    return pinned ? pinned : esp_random();
}

static void startNetwork() {
    Preferences p;
    p.begin("trashbot", true);
    String ssid   = p.getString("ssid", "");
    String pass   = p.getString("pass", "");
    String apPass = p.getString("apPass", AP_PASS_DEFAULT);
    p.end();

    WiFi.persistent(false);
    WiFi.setHostname(WIFI_HOSTNAME);

    if (ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.printf("joining '%s'", ssid.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
            delay(200);            // setup() only - nothing blocks after this
            Serial.print(".");
        }
        Serial.println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        apMode = false;
        eventLog.push("NET_UP", "station %s", WiFi.localIP().toString().c_str());
    } else {
        // No usable network is the normal case in a room full of people, so
        // the fallback is a first-class path rather than an error.
        apMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID_DEFAULT, apPass.c_str());
        eventLog.push("NET_UP", "AP '%s' at %s", AP_SSID_DEFAULT,
                      WiFi.softAPIP().toString().c_str());
    }

    if (MDNS.begin(WIFI_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
    }

    Serial.println();
    Serial.println("=====================================================");
    Serial.printf("  TRASHBOT OS v%s\n", TRASHBOT_VERSION);
    Serial.printf("  open  http://%s.local\n", WIFI_HOSTNAME);
    Serial.printf("  or    http://%s\n",
                  apMode ? WiFi.softAPIP().toString().c_str()
                         : WiFi.localIP().toString().c_str());
    if (apMode) Serial.printf("  AP fallback - join '%s'\n", AP_SSID_DEFAULT);
    Serial.println("=====================================================");
}

static void maintainNetwork(uint32_t now) {
    if (!apMode) return;
    if ((uint32_t)(now - lastStaRetry) < WIFI_RETRY_MS) return;
    lastStaRetry = now;
    // Nothing to do yet - the AP is serving the UI perfectly well. Stored
    // credentials are applied by a restart from the SETUP tab, because
    // switching interfaces underneath a live WebSocket is how you end up
    // with a robot that is driving and unreachable at the same time.
}

// ---------------------------------------------------------------------------
// Sounds. Every command report passes through here on its way to the
// browser, and every state change loop() can see is checked once per pass.
// The sound bank decides whether anything is actually assigned.
// ---------------------------------------------------------------------------
static void soundForReport(const CommandReport& r, uint32_t now) {
    uint8_t ev = SND_COUNT;

    if (r.isDrive) {
        if (r.requested == DIR_STOP)                 ev = SND_STOP;
        else if (strcmp(r.phase, "received") == 0)  ev = SND_CMD_DELAYED;
        else switch (r.verdict) {
            case V_OBEYED:   ev = SND_CMD_OBEYED;   break;
            case V_MODIFIED: ev = SND_CMD_MODIFIED; break;
            case V_IGNORED:  ev = SND_CMD_IGNORED;  break;
            case V_REJECTED: ev = SND_CMD_REJECTED; break;
            default: break;
        }
    } else {
        switch (r.requested) {
            case ACT_OPEN:
            case ACT_CLOSE:
                if (r.verdict == V_IGNORED) ev = SND_CMD_IGNORED;
                else ev = (r.actual == ACT_OPEN) ? SND_LID_OPEN : SND_LID_CLOSE;
                break;
            case ACT_PLEASE:      ev = SND_PLEASE;     break;
            case ACT_SORRY:       ev = SND_SORRY;      break;
            case ACT_PANIC:       ev = SND_PANIC;      break;
            case ACT_DO_NOTHING:  ev = SND_DO_NOTHING; break;
            case ACT_NORMAL_MODE:
                // The press is OBEYED; the mechanical intervention arrives
                // later as an IGNORED report from announceNormalModeTermination.
                ev = (r.verdict == V_IGNORED) ? SND_NORMAL_OFF : SND_NORMAL_ON;
                break;
            default: break;
        }
    }
    if (ev < SND_COUNT) soundBank.trigger(ev, now);
}

static void onReport(const CommandReport& r) {
    soundForReport(r, millis());
    webSendCommandReport(r);
}

static void reactToState(uint32_t now) {
    static uint8_t lastMood    = 0xFF;
    static bool    lastEstop   = false;
    static uint8_t lastClients = 0;

    uint8_t mood = personality.mood();
    if (lastMood != 0xFF && mood != lastMood && mood < MOOD_COUNT)
        soundBank.trigger((uint8_t)(SND_MOOD_NORMAL + mood), now);
    lastMood = mood;

    bool estop = safety.estopActive();
    if (estop != lastEstop) soundBank.trigger(estop ? SND_ESTOP : SND_SAFETY_RESET, now, estop);
    lastEstop = estop;

    uint8_t clients = (uint8_t)(webLayer.clientCount() + bleLink.clientCount() + usbLink.clientCount(now));
    if (clients > lastClients)      soundBank.trigger(SND_CONNECTED, now);
    else if (clients < lastClients) soundBank.trigger(SND_DISCONNECTED, now);
    lastClients = clients;
}

// ---------------------------------------------------------------------------
static void announceNormalModeTermination(uint16_t heldMs) {
    static char quip[56];
    snprintf(quip, sizeof(quip), "NORMAL MODE: %u.%u SEC",
             heldMs / 1000, (heldMs % 1000) / 100);

    CommandReport r;
    r.isDrive   = false;
    r.requested = ACT_NORMAL_MODE;
    r.actual    = ACT_NORMAL_MODE;
    r.verdict   = V_IGNORED;
    r.reason    = HW_FINGER ? "MECHANICAL INTERVENTION"
                            : "MECHANICAL INTERVENTION (simulated - no finger fitted)";
    r.quip      = quip;
    r.phase     = "done";
    r.simulated = !HW_FINGER;
    onReport(r);
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(300);                    // setup() only: let USB CDC enumerate
    Serial.println();

    eventLog.begin();
    eventLog.push("BOOT", "TRASHBOT OS v%s starting", TRASHBOT_VERSION);

    // Motors first, and stopped, before anything can ask them to move.
    motors.begin();
    safety.begin();
    personality.begin(chooseSeed());
    pipeline.begin(onReport);

    // Files before anything that reads them.
    if (!webLayer.mountFs()) eventLog.push("BOOT", "LittleFS did not mount - no clips, no dashboard");
    soundBank.begin();
    vision.begin();

    startNetwork();
    webLayer.begin();
    bleLink.begin();
    usbLink.begin();

    eventLog.push("BOOT", "ready. mode %s, mood %s",
                  modeName(personality.mode()), moodName(personality.mood()));
    soundBank.trigger(SND_BOOT, millis(), true);
}

void loop() {
    const uint32_t now = millis();

    bool calChanged = false;
    InboundMsg m;
    while (webLayer.popInbound(m)) {
        switch (m.kind) {
            case MSG_VISION: {
                VisionReport r{};
                r.source  = (uint8_t)m.v;
                r.persons = m.a;
                r.conf    = m.b;
                strlcpy(r.objects, m.text, sizeof(r.objects));
                vision.report(r, now);
                break;
            }
            case MSG_SOUND_TEST:
                eventLog.push("AUDIO_TEST", "%s", soundEventInfo(m.a).id);
                if (!soundBank.trigger(m.a, now, true))
                    eventLog.push("AUDIO_TEST", "nothing assigned to %s", soundEventInfo(m.a).id);
                break;
            case MSG_PING:
                if (m.b) webLayer.noteAudioReady(now);
                break;
            default:
                pipeline.handle(m, now);
                if (m.kind == MSG_CAL_SET) calChanged = true;
                break;
        }
    }

    personality.tick(now);
    vision.tick(now);

    uint16_t heldMs = 0;
    if (personality.consumeNormalTerminated(heldMs)) announceNormalModeTermination(heldMs);

    pipeline.tick(now);

    // Everything above may have set a motor target. Nothing below may.
    safety.setClientCount((uint8_t)(webLayer.clientCount() + bleLink.clientCount() + usbLink.clientCount(now)));
    safety.enforce(motors, now);
    motors.tick(now);

    reactToState(now);

    webLayer.tick(now);
    bleLink.tick(now);
    usbLink.tick(now);
    if (calChanged) webLayer.broadcastConfig();
    if (soundBank.takeDirty()) webLayer.broadcastAudioChanged();

    // One message, three links. The phone de-duplicates on seq.
    SoundMsg s;
    while (soundBank.pop(s)) {
        webLayer.sendSound(s);
        bleLink.sendSound(s);
        usbLink.sendSound(s);
    }

    maintainNetwork(now);
}
