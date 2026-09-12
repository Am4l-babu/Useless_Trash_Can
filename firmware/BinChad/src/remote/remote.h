// =============================================================================
//  remote.h - ESP-NOW receiver, bin side.
//
//  Design notes:
//    * The receive callback runs in Wi-Fi task context, so it does the
//      absolute minimum: validate, push into a ring buffer, return. All
//      interpretation happens in poll() on the main loop.
//    * Duplicate suppression is by (sequence, command) within
//      REMOTE_REPEAT_MS, so the remote can retransmit for reliability
//      without the bin reacting twice.
//    * Losing the remote is not an error. The bin keeps working; it just
//      becomes smug about it.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "protocol.h"
#include "../config/settings.h"

class RemoteLink {
public:
    bool begin();

    // Returns CMD_NONE when nothing is waiting. Call every loop.
    uint8_t poll();

    // Sequence number of the packet the last poll() returned. Send this
    // back in the ACK so the remote can match answer to question.
    uint8_t lastSequence() const { return _lastSequence; }

    // True while packets have arrived inside REMOTE_TIMEOUT_MS.
    bool isConnected() const;

    // One-shot edges, for the "REMOTE LOST / Good." messages.
    bool consumeConnectEvent();
    bool consumeDisconnectEvent();

    uint32_t packetsReceived() const { return _received; }
    uint32_t packetsRejected() const { return _rejected; }
    uint32_t lastPacketMs()    const { return _lastPacketMs; }

    // Tell the remote what happened. obeyed is almost always false.
    void sendAck(uint8_t ackSequence, uint8_t stateId, bool obeyed, uint8_t frustration);

    void update();   // maintains the connected/disconnected edges

private:
    uint32_t _received     = 0;
    uint32_t _rejected     = 0;
    uint32_t _lastPacketMs = 0;
    uint8_t  _lastSequence = 0xFF;
    bool     _wasConnected = false;
    bool     _connectEvent = false;
    bool     _disconnectEvent = false;
};

extern RemoteLink remoteLink;
