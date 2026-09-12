// =============================================================================
//  protocol.h - ESP-NOW link between BIN CONTROL SYSTEM v0.0001 and BIN-CHAD.
//
//  THIS FILE IS SHARED. It exists twice, byte-identical:
//      firmware/BinChad/src/remote/protocol.h
//      firmware/BinRemote/src/config/protocol.h
//  If you edit one, copy it over the other, or the checksum will reject
//  every packet and you will spend an hour blaming the antenna.
//
//  Full documentation: REMOTE_PROTOCOL.md
// =============================================================================
#pragma once
#include <stdint.h>

#define BINCHAD_PROTO_VERSION 1

// Device id, shared by both ends. A packet with any other id is ignored,
// so two BIN-CHADs can run in the same room by giving each pair its own id.
#define BINCHAD_DEVICE_ID 0x2A

// ---------------------------------------------------------------------------
// Commands. Values are frozen - the remote and bin must agree.
// ---------------------------------------------------------------------------
enum RemoteCommand : uint8_t {
    CMD_NONE      = 0x00,
    CMD_UP        = 0x01,
    CMD_DOWN      = 0x02,
    CMD_LEFT      = 0x03,
    CMD_RIGHT     = 0x04,
    CMD_OK        = 0x05,
    CMD_OPEN      = 0x06,
    CMD_CLOSE     = 0x07,
    CMD_STOP      = 0x08,
    CMD_AI        = 0x09,
    CMD_ANGRY     = 0x0A,
    CMD_MOOD      = 0x0B,
    CMD_NORMAL    = 0x0C,
    CMD_MUTE      = 0x0D,
    CMD_LIGHT     = 0x0E,
    CMD_DARK      = 0x0F,
    CMD_SECRET    = 0x10,   // the boot-button easter egg on the remote
    CMD_PING      = 0x1F,   // keepalive, no behaviour attached
    CMD_COUNT_    = 0x20
};

// ---------------------------------------------------------------------------
// Packet types
// ---------------------------------------------------------------------------
enum PacketType : uint8_t {
    PKT_COMMAND = 0x01,   // remote -> bin
    PKT_ACK     = 0x02,   // bin -> remote, "received, and I have opinions"
    PKT_STATUS  = 0x03    // bin -> remote, periodic
};

// ---------------------------------------------------------------------------
// Remote -> Bin. Kept to 8 bytes so it fits comfortably inside one ESP-NOW
// frame with room to grow (ESP-NOW allows 250).
// ---------------------------------------------------------------------------
struct __attribute__((packed)) RemotePacket {
    uint8_t deviceId;   // must equal BINCHAD_DEVICE_ID
    uint8_t version;    // BINCHAD_PROTO_VERSION
    uint8_t type;       // PacketType
    uint8_t command;    // RemoteCommand
    uint8_t sequence;   // increments per keypress, wraps at 255
    uint8_t repeat;     // 0 = first send, 1..n = retransmit of same sequence
    uint8_t flags;      // bit0 = held down, bit1 = low battery
    uint8_t checksum;   // see binchad_checksum()
};

#define PKT_FLAG_HELD    0x01
#define PKT_FLAG_LOWBATT 0x02

// ---------------------------------------------------------------------------
// Bin -> Remote. The remote uses this to display a status that is, in the
// finest tradition of this project, frequently a lie (see remote firmware).
// ---------------------------------------------------------------------------
struct __attribute__((packed)) BinStatusPacket {
    uint8_t deviceId;
    uint8_t version;
    uint8_t type;        // PKT_ACK or PKT_STATUS
    uint8_t ackSequence; // which command this answers (0xFF for unsolicited)
    uint8_t stateId;     // PersonalityState as uint8_t
    uint8_t obeyed;      // 1 = bin actually did what was asked (rare)
    uint8_t frustration; // 0..REMOTE_FRUSTRATION_MAX
    uint8_t checksum;
};

// ---------------------------------------------------------------------------
// Checksum: XOR of every byte except the last, then inverted so that an
// all-zero packet (the classic symptom of a dead link) fails validation.
// ---------------------------------------------------------------------------
static inline uint8_t binchad_checksum(const uint8_t *bytes, uint8_t len) {
    uint8_t x = 0;
    for (uint8_t i = 0; i < len - 1; ++i) x ^= bytes[i];
    return (uint8_t)(~x);
}

static inline void binchad_sign(void *pkt, uint8_t len) {
    uint8_t *b = (uint8_t *)pkt;
    b[len - 1] = binchad_checksum(b, len);
}

static inline bool binchad_verify(const void *pkt, uint8_t len) {
    const uint8_t *b = (const uint8_t *)pkt;
    return b[len - 1] == binchad_checksum(b, len);
}

// ---------------------------------------------------------------------------
// Broadcast address. Used when no peer has been paired yet, so the demo
// works even if you forgot to write down the bin's MAC.
// ---------------------------------------------------------------------------
static const uint8_t BINCHAD_BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// Both devices must sit on the same channel when not associated with an AP.
#define BINCHAD_ESPNOW_CHANNEL 1

static inline const char *binchad_cmd_name(uint8_t c) {
    switch (c) {
        case CMD_UP:     return "UP";
        case CMD_DOWN:   return "DOWN";
        case CMD_LEFT:   return "LEFT";
        case CMD_RIGHT:  return "RIGHT";
        case CMD_OK:     return "OK";
        case CMD_OPEN:   return "OPEN";
        case CMD_CLOSE:  return "CLOSE";
        case CMD_STOP:   return "STOP";
        case CMD_AI:     return "AI";
        case CMD_ANGRY:  return "ANGRY";
        case CMD_MOOD:   return "MOOD";
        case CMD_NORMAL: return "NORMAL";
        case CMD_MUTE:   return "MUTE";
        case CMD_LIGHT:  return "LIGHT";
        case CMD_DARK:   return "DARK";
        case CMD_SECRET: return "SECRET";
        case CMD_PING:   return "PING";
        default:         return "?";
    }
}
