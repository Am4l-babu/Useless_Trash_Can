// =============================================================================
//  BIN CONTROL SYSTEM v0.0001
//  Remote firmware for ESP32-C3 (SuperMini or equivalent).
//
//  Board:   ESP32C3 Dev Module
//  Core:    Arduino-ESP32 3.x
//  Tools:   USB CDC On Boot = Enabled  (mandatory - GPIO20/21 are buttons)
//
//  Libraries:
//      Adafruit SSD1306, Adafruit GFX, Adafruit NeoPixel
//
//  What this firmware does: reads eleven buttons, sends the honest command
//  over ESP-NOW, and then reports a status that may or may not resemble
//  what happened. All of the actual misbehaviour lives in the bin - the
//  remote is a truthful transmitter attached to a dishonest display, which
//  is funnier and far easier to debug than a lying transmitter.
//
//  Pairing: none required. The remote broadcasts; the bin answers and is
//  then remembered as a unicast peer. Both must be on the same channel
//  (BINCHAD_ESPNOW_CHANNEL in protocol.h).
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "src/config/pins.h"
#include "src/config/protocol.h"
#include "src/ui/remoteUi.h"

#if REMOTE_LED_IS_NEOPIXEL
#include <Adafruit_NeoPixel.h>
static Adafruit_NeoPixel statusLed(1, PIN_STATUS_LED, NEO_GRB + NEO_KHZ800);
#endif

// ---------------------------------------------------------------------------
// Button table
// ---------------------------------------------------------------------------
struct ButtonDef { uint8_t pin; uint8_t command; };

static const ButtonDef kButtons[] = {
    { PIN_BTN_UP,     CMD_UP    },
    { PIN_BTN_DOWN,   CMD_DOWN  },
    { PIN_BTN_LEFT,   CMD_LEFT  },
    { PIN_BTN_RIGHT,  CMD_RIGHT },
    { PIN_BTN_OK,     CMD_OK    },
    { PIN_BTN_OPEN,   CMD_OPEN  },
    { PIN_BTN_CLOSE,  CMD_CLOSE },
    { PIN_BTN_SECRET, CMD_SECRET}
};
static const uint8_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

static bool     btnStable[kButtonCount] = {false};
static bool     btnRaw[kButtonCount]    = {false};
static uint32_t btnChanged[kButtonCount] = {0};
static const uint16_t BTN_DEBOUNCE_MS = 25;

// Aux ladder: order must match LADDER_CENTERS in pins.h
static const uint8_t kLadderCommands[] = {
    CMD_AI, CMD_ANGRY, CMD_MOOD, CMD_STOP,
    CMD_NORMAL, CMD_MUTE, CMD_LIGHT, CMD_DARK
};
static const uint16_t kLadderCenters[] = LADDER_CENTERS;
static const uint8_t  kLadderCount = sizeof(kLadderCommands) / sizeof(kLadderCommands[0]);

static uint8_t  ladderStable = CMD_NONE;
static uint8_t  ladderRaw    = CMD_NONE;
static uint32_t ladderChanged = 0;

// ---------------------------------------------------------------------------
// Link state
// ---------------------------------------------------------------------------
static uint8_t  binMac[6] = {0};
static bool     haveBin = false;
static uint8_t  sequence = 0;
static uint32_t lastAckMs = 0;
static uint32_t lastSendMs = 0;
static uint32_t lastPingMs = 0;

// Retransmit state: each press is sent up to 3 times, 40 ms apart. The bin
// de-duplicates by sequence number, so this costs nothing but reliability.
static uint8_t  pendingCmd = CMD_NONE;
static uint8_t  pendingSeq = 0;
static uint8_t  pendingSends = 0;
static uint32_t pendingNextAt = 0;

static void setStatusLed(uint8_t r, uint8_t g, uint8_t b) {
#if REMOTE_LED_IS_NEOPIXEL
    statusLed.setPixelColor(0, r, g, b);
    statusLed.show();
#else
    digitalWrite(PIN_STATUS_LED, (r || g || b) ? HIGH : LOW);
#endif
}

// ---------------------------------------------------------------------------
// ESP-NOW receive: the bin's ACK
// ---------------------------------------------------------------------------
static void handleAck(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != (int)sizeof(BinStatusPacket)) return;

    BinStatusPacket s;
    memcpy(&s, data, sizeof(s));
    if (s.deviceId != BINCHAD_DEVICE_ID) return;
    if (s.version  != BINCHAD_PROTO_VERSION)  return;
    if (!binchad_verify(&s, sizeof(s)))       return;

    lastAckMs = millis();

    if (!haveBin) {
        memcpy(binMac, mac, 6);
        haveBin = true;
        if (!esp_now_is_peer_exist(binMac)) {
            esp_now_peer_info_t p = {};
            memcpy(p.peer_addr, binMac, 6);
            p.channel = BINCHAD_ESPNOW_CHANNEL;
            p.encrypt = false;
            esp_now_add_peer(&p);
        }
        Serial.println(F("[link] bin found"));
    }

    remoteUi.showAck(binchad_cmd_name(pendingCmd), s.obeyed != 0, s.frustration);
    setStatusLed(s.obeyed ? 0 : 40, s.obeyed ? 40 : 0, 0);
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    handleAck(info->src_addr, data, len);
}
#else
static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
    handleAck(mac, data, len);
}
#endif

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------
static void transmit(uint8_t command, uint8_t seq, uint8_t repeat) {
    RemotePacket p = {};
    p.deviceId = BINCHAD_DEVICE_ID;
    p.version  = BINCHAD_PROTO_VERSION;
    p.type     = PKT_COMMAND;
    p.command  = command;
    p.sequence = seq;
    p.repeat   = repeat;
    p.flags    = 0;
    binchad_sign(&p, sizeof(p));

    const uint8_t *dest = haveBin ? binMac : BINCHAD_BROADCAST;
    esp_now_send(dest, (const uint8_t *)&p, sizeof(p));
    lastSendMs = millis();
}

static void queueCommand(uint8_t command) {
    pendingCmd    = command;
    pendingSeq    = ++sequence;
    pendingSends  = 0;
    pendingNextAt = millis();
    remoteUi.showSent(binchad_cmd_name(command));
    setStatusLed(0, 0, 60);
    Serial.printf("[tx] %s seq=%u\r\n", binchad_cmd_name(command), pendingSeq);
}

static void servicePending() {
    if (pendingCmd == CMD_NONE || pendingSends >= 3) return;
    if (millis() < pendingNextAt) return;
    transmit(pendingCmd, pendingSeq, pendingSends);
    pendingSends++;
    pendingNextAt = millis() + 40;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void readButtons() {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        const bool raw = (digitalRead(kButtons[i].pin) == LOW);
        if (raw != btnRaw[i]) {
            btnRaw[i] = raw;
            btnChanged[i] = now;
            continue;
        }
        if (raw != btnStable[i] && (now - btnChanged[i]) >= BTN_DEBOUNCE_MS) {
            btnStable[i] = raw;
            if (raw) queueCommand(kButtons[i].command);
        }
    }
}

static uint8_t decodeLadder(uint16_t counts) {
    if (counts >= LADDER_IDLE_MIN) return CMD_NONE;
    for (uint8_t i = 0; i < kLadderCount; ++i) {
        const int32_t d = (int32_t)counts - (int32_t)kLadderCenters[i];
        if (d > -LADDER_WINDOW && d < LADDER_WINDOW) return kLadderCommands[i];
    }
    return CMD_NONE;   // between two buttons, or two pressed at once
}

static void readLadder() {
    const uint16_t counts = (uint16_t)analogRead(PIN_AUX_LADDER);

#if CALIBRATE_LADDER
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 200) {
        lastPrint = millis();
        Serial.printf("[ladder] raw=%u\r\n", counts);
    }
#endif

    const uint8_t cmd = decodeLadder(counts);
    const uint32_t now = millis();
    if (cmd != ladderRaw) {
        ladderRaw = cmd;
        ladderChanged = now;
        return;
    }
    if (cmd != ladderStable && (now - ladderChanged) >= BTN_DEBOUNCE_MS * 2) {
        ladderStable = cmd;
        if (cmd != CMD_NONE) queueCommand(cmd);
    }
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(150);
    Serial.println(F("\n=== BIN CONTROL SYSTEM v0.0001 ==="));
    Serial.println(F("PLEASE DO NOT TRUST."));

    for (uint8_t i = 0; i < kButtonCount; ++i) {
        pinMode(kButtons[i].pin, INPUT_PULLUP);
        btnRaw[i] = btnStable[i] = (digitalRead(kButtons[i].pin) == LOW);
    }
    analogReadResolution(12);
    // Full-scale attenuation: the ladder's top step sits close to 3V3.
    analogSetPinAttenuation(PIN_AUX_LADDER, ADC_11db);

#if REMOTE_LED_IS_NEOPIXEL
    statusLed.begin();
    statusLed.setBrightness(60);
#else
    pinMode(PIN_STATUS_LED, OUTPUT);
#endif
    setStatusLed(30, 0, 30);

    remoteUi.begin();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(BINCHAD_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[link] esp_now_init failed"));
        remoteUi.showLinkLost();
    } else {
        esp_now_register_recv_cb(onRecv);
        esp_now_peer_info_t bcast = {};
        memcpy(bcast.peer_addr, BINCHAD_BROADCAST, 6);
        bcast.channel = BINCHAD_ESPNOW_CHANNEL;
        bcast.encrypt = false;
        esp_now_add_peer(&bcast);
    }

    Serial.print(F("[link] remote MAC: "));
    Serial.println(WiFi.macAddress());
    randomSeed(micros() ^ ((uint32_t)analogRead(PIN_AUX_LADDER) << 16));
}

void loop() {
    readButtons();
    readLadder();
    servicePending();

    // Keepalive so the bin's "REMOTE LOST / Good." message only appears
    // when the remote is genuinely gone, not merely idle.
    if (millis() - lastPingMs > 1500) {
        lastPingMs = millis();
        transmit(CMD_PING, sequence, 0);
    }

    const bool linked = (millis() - lastAckMs) < 4000;
    remoteUi.setLinked(linked);
    if (!linked && (millis() - lastSendMs) > 2000) setStatusLed(20, 8, 0);

    remoteUi.update();
}
