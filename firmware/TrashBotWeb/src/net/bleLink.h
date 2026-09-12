// =============================================================================
//  bleLink.h - the "play this" message over Bluetooth Low Energy, and the
//  same JSON commands the WebSocket takes, written to a characteristic.
//
//  One service, two characteristics:
//
//    BLE_SOUND_UUID  NOTIFY  "seq;eventId;clip"   e.g. "12;3;hi_chellam.wav"
//    BLE_CMD_UUID    WRITE   {"type":"drive","dir":"forward","speed":60}
//
//  The sound payload is deliberately not JSON: a phone that never negotiated
//  a bigger MTU sees only the first 20 bytes, and "seq;eventId" fits in that
//  with room to spare. The browser resolves eventId -> clip from the map it
//  already has if the clip name got cut off.
//
//  Writes are handled in the NimBLE host task and go through exactly the
//  same validate-and-queue path as WebSocket frames. A BLE phone counts as a
//  client for the safety layer, and its silence trips the link timeout like
//  anyone else's.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../sound/soundBank.h"

class BleLink {
  public:
    void    begin();
    void    tick(uint32_t now);
    void    sendSound(const SoundMsg& m);
    uint8_t clientCount() const;
    bool    enabled() const;
    bool    ready() const { return ready_; }

  private:
    bool ready_ = false;
};

extern BleLink bleLink;
