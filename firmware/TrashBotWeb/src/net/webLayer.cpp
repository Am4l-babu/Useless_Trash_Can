#include "webLayer.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>

#include "../config/settings.h"
#include "../core/eventLog.h"
#include "../motor/motors.h"
#include "../safety/safety.h"
#include "../personality/personality.h"
#include "../sound/soundBank.h"
#include "../vision/vision.h"
#include "bleLink.h"

WebLayer webLayer;

static AsyncWebServer server(HTTP_PORT);
static AsyncWebSocket ws("/ws");

// ---------------------------------------------------------------------------
// Inbound ring. Written from the AsyncTCP task, read from loop().
// ---------------------------------------------------------------------------
static const uint8_t INBOX_SIZE = 16;
static InboundMsg     inbox[INBOX_SIZE];
static volatile uint8_t inboxHead = 0, inboxTail = 0;
static portMUX_TYPE   inboxMux = portMUX_INITIALIZER_UNLOCKED;

static bool pushInbound(const InboundMsg& m) {
    bool ok = false;
    portENTER_CRITICAL(&inboxMux);
    uint8_t next = (uint8_t)((inboxHead + 1) % INBOX_SIZE);
    if (next != inboxTail) {
        inbox[inboxHead] = m;
        inboxHead = next;
        ok = true;
    }
    portEXIT_CRITICAL(&inboxMux);
    return ok;
}

bool WebLayer::popInbound(InboundMsg& out) {
    bool ok = false;
    portENTER_CRITICAL(&inboxMux);
    if (inboxTail != inboxHead) {
        out = inbox[inboxTail];
        inboxTail = (uint8_t)((inboxTail + 1) % INBOX_SIZE);
        ok = true;
    }
    portEXIT_CRITICAL(&inboxMux);
    return ok;
}

// Clients waiting to be greeted from loop() rather than from the callback.
// Written in the AsyncTCP task, drained in loop(), so it shares the ring's
// spinlock rather than inventing a second one.
static volatile uint32_t helloQueue[WS_MAX_CLIENTS];
static volatile uint8_t  helloCount = 0;

static void queueHello(uint32_t id) {
    portENTER_CRITICAL(&inboxMux);
    if (helloCount < WS_MAX_CLIENTS) helloQueue[helloCount++] = id;
    portEXIT_CRITICAL(&inboxMux);
}

static bool popHello(uint32_t& id) {
    bool ok = false;
    portENTER_CRITICAL(&inboxMux);
    if (helloCount > 0) { id = helloQueue[--helloCount]; ok = true; }
    portEXIT_CRITICAL(&inboxMux);
    return ok;
}

// ---------------------------------------------------------------------------
// A page to serve when the LittleFS image was never uploaded. It is not the
// real UI - it exists so that a board with an empty filesystem is still a
// working, stoppable robot instead of a blank screen.
// ---------------------------------------------------------------------------
static const char FALLBACK_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TRASHBOT (minimal)</title><style>
body{background:#111;color:#d8d8d8;font:16px ui-monospace,monospace;margin:0;padding:16px;text-align:center}
h1{font-size:15px;letter-spacing:.2em;color:#f5a623}
p{color:#888;font-size:12px;line-height:1.6}
button{font:inherit;background:#1d1d1d;color:#eee;border:1px solid #3a3a3a;border-radius:8px;
padding:18px;min-width:92px;margin:4px;touch-action:none}
button:active{background:#f5a623;color:#111}
#es{background:#7a1414;border-color:#c0392b;width:96%;margin-top:14px;padding:22px}
#st{margin:10px 0;font-size:12px}
</style></head><body>
<h1>TRASHBOT / MINIMAL</h1>
<p>Web assets not found on LittleFS.<br>Upload the <code>data/</code> folder to get the real dashboard.<br>
Until then this pad works: hold a button to drive.</p>
<div id="st">connecting...</div>
<div><button data-d="forward">FWD</button></div>
<div><button data-d="left">LEFT</button><button data-d="stop">STOP</button><button data-d="right">RIGHT</button></div>
<div><button data-d="backward">BACK</button></div>
<button id="es">EMERGENCY STOP</button>
<script>
var s,hold=null;
function conn(){s=new WebSocket("ws://"+location.host+"/ws");
s.onopen=function(){document.getElementById("st").textContent="ONLINE";};
s.onclose=function(){document.getElementById("st").textContent="OFFLINE";setTimeout(conn,1000);};
s.onmessage=function(e){var m=JSON.parse(e.data);
if(m.type=="telemetry")document.getElementById("st").textContent=
"ONLINE / "+m.mode+" / "+m.mood+(m.estop?" / ESTOP":"");};}
conn();
function send(o){if(s&&s.readyState==1)s.send(JSON.stringify(o));}
setInterval(function(){send({type:"ping"});},400);
document.querySelectorAll("button[data-d]").forEach(function(b){
var d=b.dataset.d;
function go(e){e.preventDefault();if(d=="stop"){send({type:"drive",dir:"stop"});return;}
send({type:"drive",dir:d,speed:60});hold=setInterval(function(){send({type:"drive",dir:d,speed:60});},100);}
function end(e){e.preventDefault();clearInterval(hold);send({type:"drive",dir:"stop"});}
b.addEventListener("pointerdown",go);b.addEventListener("pointerup",end);
b.addEventListener("pointercancel",end);b.addEventListener("pointerleave",end);});
document.getElementById("es").addEventListener("click",function(){send({type:"estop"});});
</script></body></html>)HTML";

// ---------------------------------------------------------------------------
// Parsing helpers. Every one of these fails closed.
// ---------------------------------------------------------------------------
// The wire spellings, indexed by Direction.
static const char* const kDirWire[DIR_COUNT] = {
    "stop", "forward", "backward", "left", "right",
    "fwd_left", "fwd_right", "back_left", "back_right"
};

static uint8_t parseDir(const char* s) {
    if (!s) return DIR_COUNT;
    for (uint8_t i = 0; i < DIR_COUNT; ++i)
        if (strcmp(s, kDirWire[i]) == 0) return i;
    return DIR_COUNT;
}

static uint8_t parseAction(const char* s) {
    if (!s) return ACT_NONE;
    if (!strcmp(s, "open"))        return ACT_OPEN;
    if (!strcmp(s, "close"))       return ACT_CLOSE;
    if (!strcmp(s, "please"))      return ACT_PLEASE;
    if (!strcmp(s, "sorry"))       return ACT_SORRY;
    if (!strcmp(s, "panic"))       return ACT_PANIC;
    if (!strcmp(s, "do_nothing"))  return ACT_DO_NOTHING;
    if (!strcmp(s, "normal_mode")) return ACT_NORMAL_MODE;
    return ACT_NONE;
}

static uint8_t parseMode(const char* s) {
    if (!s) return MODE_COUNT;
    if (!strcmp(s, "normal"))        return MODE_NORMAL;
    if (!strcmp(s, "uncooperative")) return MODE_UNCOOPERATIVE;
    if (!strcmp(s, "reverse"))       return MODE_REVERSE;
    if (!strcmp(s, "chaos"))         return MODE_CHAOS;
    if (!strcmp(s, "drunk"))         return MODE_DRUNK;
    if (!strcmp(s, "lazy"))          return MODE_LAZY;
    if (!strcmp(s, "angry"))         return MODE_ANGRY;
    if (!strcmp(s, "panic"))         return MODE_PANIC;
    if (!strcmp(s, "sleep"))         return MODE_SLEEP;
    return MODE_COUNT;
}

static uint8_t parseCalField(const char* s) {
    if (!s) return CAL_COUNT;
    if (!strcmp(s, "left_invert"))  return CAL_LEFT_INVERT;
    if (!strcmp(s, "right_invert")) return CAL_RIGHT_INVERT;
    if (!strcmp(s, "max_duty"))     return CAL_MAX_DUTY;
    if (!strcmp(s, "min_duty"))     return CAL_MIN_DUTY;
    if (!strcmp(s, "accel"))        return CAL_ACCEL;
    if (!strcmp(s, "trim"))         return CAL_TRIM;
    if (!strcmp(s, "save"))         return CAL_SAVE;
    return CAL_COUNT;
}

// Joystick vector -> one of the eight sectors. Below the deadzone it is a
// stop, which is why the stick has to be genuinely centred to stop driving.
static uint8_t vectorToDir(int x, int y, uint8_t& magnitudeOut) {
    float fx = (float)x, fy = (float)y;
    float mag = sqrtf(fx * fx + fy * fy);
    if (mag > 100.0f) mag = 100.0f;
    magnitudeOut = (uint8_t)mag;
    if (mag < 20.0f) return DIR_STOP;

    static const uint8_t kSector[8] = {
        DIR_RIGHT, DIR_FWD_RIGHT, DIR_FORWARD, DIR_FWD_LEFT,
        DIR_LEFT,  DIR_BACK_LEFT, DIR_BACKWARD, DIR_BACK_RIGHT
    };
    float deg = atan2f(fy, fx) * 57.2957795f;
    if (deg < 0) deg += 360.0f;
    uint8_t sector = (uint8_t)(((int)(deg + 22.5f) / 45) % 8);
    return kSector[sector];
}

// ---------------------------------------------------------------------------
static void handleTextFrame(uint32_t clientId, const char* data, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        eventLog.push("COMMAND_REJECTED", "malformed JSON from client %lu",
                      (unsigned long)clientId);
        return;
    }

    const char* type = doc["type"] | "";
    InboundMsg m{};
    m.clientId = clientId;

    if (!strcmp(type, "ping")) {
        m.kind = MSG_PING;
        m.b = (doc["snd"] | false) ? 1 : 0;

    } else if (!strcmp(type, "vision")) {
        // A detection result. Whoever ran the detector says so in "src".
        m.kind = MSG_VISION;
        m.a = (uint8_t)constrain((int)(doc["persons"] | 0), 0, 255);
        m.b = (uint8_t)constrain((int)(doc["conf"] | 0), 0, 100);
        m.v = parseVisionSource(doc["src"] | "phone");
        const char* objs = doc["objects"] | "";
        strlcpy(m.text, objs, sizeof(m.text));

    } else if (!strcmp(type, "sound_test")) {
        m.kind = MSG_SOUND_TEST;
        m.a = parseSoundEvent(doc["event"] | "");
        if (m.a >= SND_COUNT) return;

    } else if (!strcmp(type, "drive")) {
        m.kind = MSG_DRIVE;
        uint8_t speed = (uint8_t)constrain((int)(doc["speed"] | 60), 0, 100);
        if (doc["dir"].is<const char*>()) {
            m.a = parseDir(doc["dir"]);
            if (m.a >= DIR_COUNT) return;
            m.b = speed;
        } else {
            int x = constrain((int)(doc["x"] | 0), -100, 100);
            int y = constrain((int)(doc["y"] | 0), -100, 100);
            uint8_t mag = 0;
            m.a = vectorToDir(x, y, mag);
            // The stick's deflection scales the slider, so a small push is a
            // small request even with the slider at 100%.
            m.b = (uint8_t)((uint16_t)speed * mag / 100);
        }

    } else if (!strcmp(type, "action")) {
        m.kind = MSG_ACTION;
        m.a = parseAction(doc["action"]);
        if (m.a == ACT_NONE) return;

    } else if (!strcmp(type, "mode")) {
        m.kind = MSG_MODE;
        m.a = parseMode(doc["mode"]);
        if (m.a >= MODE_COUNT) return;

    } else if (!strcmp(type, "speed")) {
        m.kind = MSG_SPEED;
        m.b = (uint8_t)constrain((int)(doc["value"] | 60), 0, 100);

    } else if (!strcmp(type, "estop")) {
        // Latched here, in the network task, so it does not wait for loop().
        // The queued copy only exists to produce the log line and telemetry.
        motors.hardStop();
        safety.triggerEstop("user pressed EMERGENCY STOP");
        m.kind = MSG_ESTOP;

    } else if (!strcmp(type, "reset_safety")) {
        m.kind = MSG_RESET_SAFETY;

    } else if (!strcmp(type, "cal_jog")) {
        m.kind = MSG_CAL_JOG;
        const char* motor = doc["motor"] | "left";
        const char* dir   = doc["dir"] | "stop";
        m.a = strcmp(motor, "right") == 0 ? 1 : 0;
        m.b = !strcmp(dir, "forward") ? 2 : (!strcmp(dir, "reverse") ? 0 : 1);

    } else if (!strcmp(type, "cal_set")) {
        m.kind = MSG_CAL_SET;
        m.a = parseCalField(doc["field"]);
        if (m.a >= CAL_COUNT) return;
        m.v = (int16_t)constrain((int)(doc["value"] | 0), -1000, 1000);

    } else {
        return;
    }

    safety.noteLink(millis());
    if (!pushInbound(m)) eventLog.push("QUEUE_FULL", "inbound queue full, command dropped");
}

void WebLayer::ingest(uint32_t clientId, const char* data, size_t len) {
    if (!data || len == 0 || len > WS_MAX_FRAME_BYTES) return;
    handleTextFrame(clientId, data, len);
}

bool WebLayer::audioReady(uint32_t now) const {
    return lastAudioReady_ && (uint32_t)(now - lastAudioReady_) < SND_AUDIO_READY_TTL_MS;
}

// ---------------------------------------------------------------------------
// The audio API. Handlers run in the AsyncTCP task; they touch LittleFS and
// the sound bank's map (spinlocked), and never anything else.
//
//   GET    /api/audio             the library, the map, the event catalogue
//   POST   /api/audio             multipart upload of one clip
//   DELETE /api/audio?name=x      remove a clip (and unassign it)
//   POST   /api/audio/map         form: event=<id> clip=<name or empty>
// ---------------------------------------------------------------------------
static const char* uploadError(int code) {
    switch (code) {
        case 1:  return "bad filename - use letters, digits, . _ - and an audio extension";
        case 2:  return "clip too large";
        case 3:  return "not enough space on the bin";
        case 4:  return "could not open file for writing";
        case 5:  return "write failed";
        default: return "upload failed";
    }
}

static void handleAudioUpload(AsyncWebServerRequest* req, const String& filename,
                              size_t index, uint8_t* data, size_t len, bool final) {
    if (index == 0) {
        req->_tempObject = nullptr;
        char safe[SND_NAME_MAX];
        if (!SoundBank::sanitize(filename.c_str(), safe, sizeof(safe))) {
            req->_tempObject = (void*)(intptr_t)1;
            return;
        }
        if (req->contentLength() > SND_MAX_CLIP_BYTES + 2048) {
            req->_tempObject = (void*)(intptr_t)2;
            return;
        }
        size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
        if (freeBytes < req->contentLength() + 16384) {
            req->_tempObject = (void*)(intptr_t)3;
            return;
        }
        String path = String(SND_DIR "/") + safe;
        req->_tempFile = LittleFS.open(path, "w");
        if (!req->_tempFile) {
            req->_tempObject = (void*)(intptr_t)4;
            return;
        }
    }
    if (req->_tempObject) return;                        // already failed; drain silently

    if (len && req->_tempFile) {
        if (index + len > SND_MAX_CLIP_BYTES) {
            String partial = String(SND_DIR "/") + req->_tempFile.name();
            req->_tempFile.close();
            LittleFS.remove(partial);
            req->_tempObject = (void*)(intptr_t)2;
            return;
        }
        if (req->_tempFile.write(data, len) != len) {
            req->_tempFile.close();
            req->_tempObject = (void*)(intptr_t)5;
            return;
        }
    }
    if (final && req->_tempFile) {
        size_t size = req->_tempFile.size();
        String name = req->_tempFile.name();
        req->_tempFile.close();
        eventLog.push("AUDIO_ADDED", "%s (%u bytes)", name.c_str(), (unsigned)size);
        soundBank.markDirty();
    }
}

static void registerAudioRoutes() {
    server.on("/api/audio", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        SoundBank::fillClips(doc["clips"].to<JsonArray>());
        soundBank.fillMap(doc["map"].to<JsonObject>());
        SoundBank::fillEvents(doc["events"].to<JsonArray>());
        JsonObject fs = doc["fs"].to<JsonObject>();
        fs["used"]  = (uint32_t)LittleFS.usedBytes();
        fs["total"] = (uint32_t)LittleFS.totalBytes();
        doc["maxClip"] = SND_MAX_CLIP_BYTES;
        AsyncResponseStream* res = req->beginResponseStream("application/json");
        serializeJson(doc, *res);
        req->send(res);
    });

    server.on("/api/audio", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            int err = (int)(intptr_t)req->_tempObject;
            if (err) req->send(400, "text/plain", uploadError(err));
            else     req->send(200, "text/plain", "ok");
        },
        handleAudioUpload);

    server.on("/api/audio", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("name")) { req->send(400, "text/plain", "name required"); return; }
        String name = req->getParam("name")->value();
        if (!SoundBank::validName(name.c_str())) { req->send(400, "text/plain", "bad name"); return; }
        String path = String(SND_DIR "/") + name;
        if (!LittleFS.exists(path)) { req->send(404, "text/plain", "no such clip"); return; }
        LittleFS.remove(path);
        soundBank.clipDeleted(name.c_str());
        eventLog.push("AUDIO_REMOVED", "%s", name.c_str());
        soundBank.markDirty();
        req->send(200, "text/plain", "ok");
    });

    server.on("/api/audio/map", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("event", true)) { req->send(400, "text/plain", "event required"); return; }
        uint8_t e = parseSoundEvent(req->getParam("event", true)->value().c_str());
        if (e >= SND_COUNT) { req->send(400, "text/plain", "unknown event"); return; }
        String clip = req->hasParam("clip", true) ? req->getParam("clip", true)->value() : "";
        if (clip.length() && !SoundBank::clipExists(clip.c_str())) {
            req->send(404, "text/plain", "no such clip");
            return;
        }
        if (!soundBank.assign(e, clip.c_str())) { req->send(500, "text/plain", "could not save"); return; }
        soundBank.markDirty();
        req->send(200, "text/plain", "ok");
    });

    // A camera board on Wi-Fi reports here. Form fields, so a bare
    // HTTPClient can do it in one line.
    server.on("/api/vision", HTTP_POST, [](AsyncWebServerRequest* req) {
        InboundMsg m{};
        m.kind = MSG_VISION;
        m.clientId = CLIENT_ID_HTTP;
        m.a = (uint8_t)constrain(req->hasParam("persons", true) ? req->getParam("persons", true)->value().toInt() : 0, 0, 255);
        m.b = (uint8_t)constrain(req->hasParam("conf", true)    ? req->getParam("conf", true)->value().toInt()    : 0, 0, 100);
        m.v = parseVisionSource(req->hasParam("src", true) ? req->getParam("src", true)->value().c_str() : "http");
        if (req->hasParam("objects", true))
            strlcpy(m.text, req->getParam("objects", true)->value().c_str(), sizeof(m.text));
        safety.noteLink(millis());
        req->send(pushInbound(m) ? 200 : 503, "text/plain", "ok");
    });
}

// ---------------------------------------------------------------------------
static void onWsEvent(AsyncWebSocket* /*server*/, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            safety.noteLink(millis());
            eventLog.push("USER_CONNECTED", "browser %lu from %s",
                          (unsigned long)client->id(),
                          client->remoteIP().toString().c_str());
            queueHello(client->id());
            break;

        case WS_EVT_DISCONNECT:
            eventLog.push("USER_DISCONNECTED", "browser gone - motors will stop");
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            // Single complete text frames only. Every message this protocol
            // defines is far under one frame; anything else is not ours.
            if (!info->final || info->index != 0 || info->len != len) return;
            if (info->opcode != WS_TEXT) return;
            if (len == 0 || len > WS_MAX_FRAME_BYTES) {
                eventLog.push("COMMAND_REJECTED", "frame of %u bytes refused", (unsigned)len);
                return;
            }
            handleTextFrame(client->id(), (const char*)data, len);
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
bool WebLayer::mountFs() {
    if (fsMounted_) return true;
    fsMounted_ = LittleFS.begin(true);
    fsReady_ = fsMounted_ && LittleFS.exists("/index.html");
    return fsMounted_;
}

void WebLayer::begin() {
    mountFs();

    if (fsMounted_) {
        // Clips first, with a long cache life: the browser keys its requests
        // by name+size, so a replaced clip is fetched fresh anyway.
        server.serveStatic("/audio/", LittleFS, SND_DIR "/", "max-age=86400");
    }
    if (fsReady_) {
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
        eventLog.push("BOOT", "web assets served from LittleFS");
    } else {
        eventLog.push("BOOT", "no /index.html on LittleFS - serving minimal page");
    }
    if (fsMounted_) registerAudioRoutes();

    server.onNotFound([](AsyncWebServerRequest* req) {
        if (req->url() == "/" || req->url() == "/index.html") {
            // PROGMEM is transparently readable on ESP32, so the plain
            // send() overload works and stays portable across library forks.
            req->send(200, "text/html", FALLBACK_HTML);
        } else {
            req->send(404, "text/plain", "not found");
        }
    });

    // Station credentials. Reachable from the SETUP tab, including over the
    // AP fallback, which is the only way to configure a board that has never
    // been on the network. Stored in NVS; the board restarts to apply them.
    server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("ssid", true)) {
            req->send(400, "text/plain", "ssid required");
            return;
        }
        String ssid = req->getParam("ssid", true)->value();
        String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
        if (ssid.length() > 32 || pass.length() > 63) {
            req->send(400, "text/plain", "too long");
            return;
        }
        Preferences p;
        p.begin("trashbot", false);
        p.putString("ssid", ssid);
        p.putString("pass", pass);
        p.end();
        eventLog.push("NET_CONFIG", "station credentials stored for '%s', restarting", ssid.c_str());
        req->send(200, "text/plain", "saved, restarting");
        webLayer.requestReboot(600);
    });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
}

void WebLayer::requestReboot(uint32_t inMs) {
    rebootAt_ = millis() + inMs;
}

uint8_t WebLayer::clientCount() const {
    return (uint8_t)ws.count();
}

// ---------------------------------------------------------------------------
// Outbound messages
// ---------------------------------------------------------------------------
static void sendDoc(JsonDocument& doc, AsyncWebSocketClient* only) {
    // Called from loop() only, so one shared buffer is safe. Telemetry is the
    // largest message at roughly 700 bytes with the vision block.
    static char buf[1536];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        // Serial rather than the event log: an oversized message every 100 ms
        // would flood the very log you would be reading to diagnose it.
        Serial.println("[web] message too large to serialize, dropped");
        return;
    }
    if (only) only->text(buf, n);
    else      ws.textAll(buf, n);
}

static void fillEvent(JsonDocument& doc, const LogEntry& e) {
    doc["type"] = "event";
    doc["seq"]  = e.seq;
    doc["ms"]   = e.ms;
    doc["code"] = e.code;
    doc["text"] = e.text;
}

static void sendHello(AsyncWebSocketClient* c) {
    JsonDocument doc;
    doc["type"]    = "hello";
    doc["version"] = TRASHBOT_VERSION;
    char seedStr[12];
    snprintf(seedStr, sizeof(seedStr), "0x%08lX", (unsigned long)personality.seed());
    doc["seed"] = seedStr;
    doc["pingMs"] = CLIENT_PING_MS;
    doc["driveRepeatMs"] = 100;
    doc["driveTimeoutMs"] = DRIVE_TIMEOUT_MS;

    // Honest inventory. Nothing here is auto-detected - see the README. The
    // camera and the speaker are the two exceptions: they are the phone, and
    // whether the phone is currently acting as one IS observable, so those
    // two are reported live in telemetry instead.
    JsonObject hw = doc["hw"].to<JsonObject>();
    hw["esp32"]   = "online";
    hw["l298n"]   = "configured";
    hw["motorL"]  = "configured";
    hw["motorR"]  = "configured";
    hw["lid"]     = HW_LID_SERVO   ? "configured" : "not_installed";
    hw["camera"]  = HW_CAMERA      ? "configured" : "see_telemetry";
    hw["mic"]     = HW_MICROPHONE  ? "configured" : "not_installed";
    hw["leds"]    = HW_LEDS        ? "configured" : "not_installed";
    hw["speaker"] = HW_SPEAKER     ? "configured" : "see_telemetry";
    hw["display"] = HW_DISPLAY     ? "configured" : "not_installed";
    hw["tof"]     = HW_TOF         ? "configured" : "not_installed";
    hw["encoders"]= HW_ENCODERS    ? "configured" : "not_installed";
    hw["battery"] = HW_BATTERY_ADC ? "configured" : "not_instrumented";
    hw["finger"]  = HW_FINGER      ? "configured" : "not_installed";
    hw["ble"]     = bleLink.enabled() ? (bleLink.ready() ? "online" : "failed") : "not_installed";
    hw["usb"]     = TRASHBOT_USB_LINK ? "configured" : "not_installed";
    hw["visionUart"] = TRASHBOT_VISION_UART ? "configured" : "not_installed";

    // So the browser does not have to hard-code the radio.
    JsonObject ble = doc["ble"].to<JsonObject>();
    ble["name"]    = BLE_DEVICE_NAME;
    ble["service"] = BLE_SVC_UUID;
    ble["sound"]   = BLE_SOUND_UUID;
    ble["command"] = BLE_CMD_UUID;

    sendDoc(doc, c);
}

void WebLayer::sendSound(const SoundMsg& m) {
    if (ws.count() == 0) return;
    JsonDocument doc;
    doc["type"]  = "sound";
    doc["seq"]   = m.seq;
    doc["event"] = soundEventInfo(m.event).id;
    doc["label"] = soundEventInfo(m.event).label;
    doc["clip"]  = m.clip;
    sendDoc(doc, nullptr);
}

void WebLayer::broadcastAudioChanged() {
    if (ws.count() == 0) return;
    JsonDocument doc;
    doc["type"] = "audio_changed";
    sendDoc(doc, nullptr);
}

void WebLayer::broadcastConfig() {
    JsonDocument doc;
    doc["type"] = "config";
    MotorCal& c = motors.cal();
    JsonObject cal = doc["cal"].to<JsonObject>();
    cal["left_invert"]  = c.leftInvert;
    cal["right_invert"] = c.rightInvert;
    cal["max_duty"]     = c.maxDuty;
    cal["min_duty"]     = c.minDuty;
    cal["accel"]        = c.accel;
    cal["trim"]         = c.trim;
    cal["duty_ceiling"] = MOTOR_DUTY_CEILING;
    sendDoc(doc, nullptr);
}

void WebLayer::broadcastTelemetry(uint32_t now) {
    if ((uint32_t)(now - lastTelemetry_) < TELEMETRY_MS) return;
    lastTelemetry_ = now;
    if (ws.count() == 0) return;

    const MotorCal& c = motors.cal();
    const uint8_t maxd = c.maxDuty ? c.maxDuty : 1;

    JsonDocument doc;
    doc["type"]    = "telemetry";
    doc["mode"]    = modeName(personality.mode());
    doc["mood"]    = moodName(personality.mood());
    doc["modeId"]  = personality.mode();
    doc["moodId"]  = personality.mood();

    const Traits& t = personality.traits();
    JsonObject tr = doc["traits"].to<JsonObject>();
    tr["anger"]     = t.anger;
    tr["trust"]     = t.trust;
    tr["happiness"] = t.happiness;
    tr["confusion"] = t.confusion;
    tr["boredom"]   = t.boredom;
    tr["obedience"] = t.obedience;
    tr["rebellion"] = t.rebellion;

    doc["leftMotor"]   = (int)((int32_t)motors.leftDuty()  * 100 / maxd);
    doc["rightMotor"]  = (int)((int32_t)motors.rightDuty() * 100 / maxd);
    doc["leftTarget"]  = (int)((int32_t)motors.leftTarget()  * 100 / maxd);
    doc["rightTarget"] = (int)((int32_t)motors.rightTarget() * 100 / maxd);
    doc["speedReq"]    = pipeline.speedSetting();

    doc["estop"]       = safety.estopActive();
    doc["estopReason"] = safety.estopReason();
    doc["inhibit"]     = safety.inhibitReason();
    doc["clients"]     = ws.count();
    doc["bleClients"]  = bleLink.clientCount();
    doc["uptime"]      = now;
    doc["heap"]        = (uint32_t)ESP.getFreeHeap();
    doc["rssi"]        = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    doc["pendingCmd"]  = pipeline.pending();
    doc["normalGrace"] = personality.normalGraceActive();
    doc["jog"]         = motors.jogActive();

    JsonObject lid = doc["lid"].to<JsonObject>();
    lid["installed"] = HW_LID_SERVO;
    lid["open"]      = pipeline.lidOpenSimulated();
    lid["simulated"] = !HW_LID_SERVO;

    // No divider is fitted, so there is no voltage to report. A made-up
    // percentage here would be the one lie this project does not tell.
    doc["battery"] = nullptr;

    // What the camera - wherever it is - last reported, and whether the
    // report is still fresh. "live" false with a source name means the
    // source went quiet and its numbers are stale.
    JsonObject v = doc["vision"].to<JsonObject>();
    v["source"]  = vision.sourceName();
    v["live"]    = vision.live(now);
    v["persons"] = vision.persons();
    v["present"] = vision.personPresent();
    v["objects"] = vision.objects();
    v["conf"]    = vision.conf();
    v["humans"]  = vision.humansSeen();
    v["things"]  = vision.objectsSeen();

    // A phone that has unlocked audio pinged recently: the bin has a speaker.
    doc["audioReady"] = audioReady(now);

    sendDoc(doc, nullptr);
}

void WebLayer::broadcastNewEvents() {
    if (ws.count() == 0) {
        lastEventSeq_ = eventLog.lastSeq();
        return;
    }
    if (eventLog.lastSeq() == lastEventSeq_) return;

    LogEntry e;
    for (uint8_t i = 0; i < eventLog.count(); ++i) {
        if (!eventLog.get(i, e)) break;
        if (e.seq <= lastEventSeq_) continue;
        JsonDocument doc;
        fillEvent(doc, e);
        sendDoc(doc, nullptr);
    }
    lastEventSeq_ = eventLog.lastSeq();
}

void webSendCommandReport(const CommandReport& r) {
    if (ws.count() == 0) return;
    JsonDocument doc;
    doc["type"]      = "command";
    doc["kind"]      = r.isDrive ? "drive" : "action";
    doc["requested"] = r.isDrive ? dirName(r.requested) : actionName(r.requested);
    doc["actual"]    = r.isDrive ? dirName(r.actual)    : actionName(r.actual);
    doc["verdict"]   = verdictName(r.verdict);
    doc["delayMs"]   = r.delayMs;
    doc["speedReq"]  = r.speedRequested;
    doc["speedAct"]  = r.speedActual;
    doc["reason"]    = r.reason;
    doc["quip"]      = r.quip;
    doc["phase"]     = r.phase;
    doc["simulated"] = r.simulated;
    sendDoc(doc, nullptr);
}

// ---------------------------------------------------------------------------
void WebLayer::tick(uint32_t now) {
    ws.cleanupClients();

    if (rebootAt_ && (int32_t)(now - rebootAt_) >= 0) {
        motors.hardStop();
        ESP.restart();
    }

    uint32_t id;
    while (popHello(id)) {
        AsyncWebSocketClient* c = ws.client(id);
        if (!c) continue;
        sendHello(c);

        JsonDocument cfg;
        cfg["type"] = "config";
        MotorCal& mc = motors.cal();
        JsonObject cal = cfg["cal"].to<JsonObject>();
        cal["left_invert"]  = mc.leftInvert;
        cal["right_invert"] = mc.rightInvert;
        cal["max_duty"]     = mc.maxDuty;
        cal["min_duty"]     = mc.minDuty;
        cal["accel"]        = mc.accel;
        cal["trim"]         = mc.trim;
        cal["duty_ceiling"] = MOTOR_DUTY_CEILING;
        sendDoc(cfg, c);

        // Replay the tail of the log so a browser that joins late still has
        // context for what the machine has been doing.
        //
        // Stop at the broadcast cursor: anything newer is delivered by
        // broadcastNewEvents() further down this same tick, and sending it
        // here as well would show the new client a duplicate of its own
        // USER_CONNECTED line.
        uint8_t total = eventLog.count();
        uint8_t from  = total > EVENT_BACKLOG_ON_JOIN ? (uint8_t)(total - EVENT_BACKLOG_ON_JOIN) : 0;
        LogEntry e;
        for (uint8_t i = from; i < total; ++i) {
            if (!eventLog.get(i, e)) break;
            if (e.seq > lastEventSeq_) break;
            JsonDocument doc;
            fillEvent(doc, e);
            sendDoc(doc, c);
        }
    }

    broadcastTelemetry(now);
    broadcastNewEvents();
}
