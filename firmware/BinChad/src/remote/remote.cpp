#include "remote.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

RemoteLink remoteLink;

// ---------------------------------------------------------------------------
// Ring buffer written from the Wi-Fi task, read from loop(). Small and
// lock-free-ish: one producer, one consumer, indices are volatile and the
// buffer is sized a power of two.
// ---------------------------------------------------------------------------
#define RX_RING 8
static volatile uint8_t rxHead = 0, rxTail = 0;
static volatile uint8_t rxCmd[RX_RING];
static volatile uint8_t rxSeq[RX_RING];

static volatile uint32_t g_lastPacketMs = 0;
static volatile uint32_t g_received = 0;
static volatile uint32_t g_rejected = 0;

static uint8_t  lastPeer[6] = {0};
static bool     havePeer = false;

// Duplicate suppression state (main-loop side only).
static uint8_t  lastAcceptedSeq = 0xFF;
static uint8_t  lastAcceptedCmd = CMD_NONE;
static uint32_t lastAcceptedMs  = 0;

static void rememberPeer(const uint8_t *mac) {
    if (havePeer && memcmp(lastPeer, mac, 6) == 0) return;
    memcpy(lastPeer, mac, 6);
    havePeer = true;

    if (!esp_now_is_peer_exist(lastPeer)) {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, lastPeer, 6);
        p.channel = BINCHAD_ESPNOW_CHANNEL;
        p.encrypt = false;
        esp_now_add_peer(&p);
    }
}

static void handlePacket(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != (int)sizeof(RemotePacket)) { g_rejected++; return; }

    RemotePacket p;
    memcpy(&p, data, sizeof(p));

    if (p.deviceId != BINCHAD_DEVICE_ID)       { g_rejected++; return; }
    if (p.version  != BINCHAD_PROTO_VERSION)   { g_rejected++; return; }
    if (p.type     != PKT_COMMAND)             { g_rejected++; return; }
    if (!binchad_verify(&p, sizeof(p)))        { g_rejected++; return; }

    rememberPeer(mac);
    g_lastPacketMs = millis();
    g_received++;

    const uint8_t next = (uint8_t)((rxHead + 1) % RX_RING);
    if (next == rxTail) return;      // full: drop the oldest-arriving new one
    rxCmd[rxHead] = p.command;
    rxSeq[rxHead] = p.sequence;
    rxHead = next;
}

// Arduino-ESP32 changed the ESP-NOW receive callback signature in core 3.0.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    handlePacket(info->src_addr, data, len);
}
#else
static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
    handlePacket(mac, data, len);
}
#endif

bool RemoteLink::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);

    // Pin the channel. Without this the two boards can land on different
    // channels and the link silently does nothing.
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(BINCHAD_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[remote] esp_now_init failed"));
        return false;
    }
    esp_now_register_recv_cb(onRecv);

    // Broadcast peer, so we can answer a remote we have never seen before.
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, BINCHAD_BROADCAST, 6);
    bcast.channel = BINCHAD_ESPNOW_CHANNEL;
    bcast.encrypt = false;
    esp_now_add_peer(&bcast);

    Serial.print(F("[remote] bin MAC: "));
    Serial.println(WiFi.macAddress());
    return true;
}

uint8_t RemoteLink::poll() {
    // Mirror the volatile counters into the plain members for the getters.
    _received     = g_received;
    _rejected     = g_rejected;
    _lastPacketMs = g_lastPacketMs;

    if (rxTail == rxHead) return CMD_NONE;

    const uint8_t cmd = rxCmd[rxTail];
    const uint8_t seq = rxSeq[rxTail];
    rxTail = (uint8_t)((rxTail + 1) % RX_RING);

    // Same keypress retransmitted for reliability - accept it once only.
    const uint32_t now = millis();
    if (seq == lastAcceptedSeq && cmd == lastAcceptedCmd &&
        (now - lastAcceptedMs) < REMOTE_REPEAT_MS) {
        return CMD_NONE;
    }
    lastAcceptedSeq = seq;
    lastAcceptedCmd = cmd;
    lastAcceptedMs  = now;
    _lastSequence   = seq;
    return cmd;
}

bool RemoteLink::isConnected() const {
    return _lastPacketMs != 0 && (millis() - _lastPacketMs) < REMOTE_TIMEOUT_MS;
}

void RemoteLink::update() {
    const bool now = isConnected();
    if (now && !_wasConnected)  _connectEvent = true;
    if (!now && _wasConnected)  _disconnectEvent = true;
    _wasConnected = now;
}

bool RemoteLink::consumeConnectEvent()    { bool e = _connectEvent;    _connectEvent = false;    return e; }
bool RemoteLink::consumeDisconnectEvent() { bool e = _disconnectEvent; _disconnectEvent = false; return e; }

void RemoteLink::sendAck(uint8_t ackSequence, uint8_t stateId, bool obeyed, uint8_t frustration) {
    BinStatusPacket s = {};
    s.deviceId    = BINCHAD_DEVICE_ID;
    s.version     = BINCHAD_PROTO_VERSION;
    s.type        = PKT_ACK;
    s.ackSequence = ackSequence;
    s.stateId     = stateId;
    s.obeyed      = obeyed ? 1 : 0;
    s.frustration = frustration;
    binchad_sign(&s, sizeof(s));

    const uint8_t *dest = havePeer ? lastPeer : BINCHAD_BROADCAST;
    esp_now_send(dest, (const uint8_t *)&s, sizeof(s));
}
