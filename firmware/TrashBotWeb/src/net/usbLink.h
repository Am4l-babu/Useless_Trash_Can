// =============================================================================
//  usbLink.h - the "play this" message on the USB serial port, and JSON
//  commands typed (or piped) back in.
//
//  Outbound, one line per sound, on the same port as the boot log:
//
//    SND {"type":"sound","seq":12,"event":"human_detected","clip":"hi.wav"}
//
//  Anything not prefixed SND is ordinary logging and a reader should skip it.
//
//  Inbound: a line starting with '{' is handed to the same validator the
//  WebSocket uses, so a laptop on Web Serial, or a phone on WebUSB, can drive
//  the bin and hear it with no Wi-Fi at all. The link-timeout rule applies:
//  a serial client that stops talking loses the motors like anyone else.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../sound/soundBank.h"

class UsbLink {
  public:
    void begin();
    void tick(uint32_t now);
    void sendSound(const SoundMsg& m);

    // A serial peer cannot be detected, but one that has sent a JSON line
    // inside the link timeout observably exists. That is what counts as a
    // client for the safety layer's NO CLIENT rule.
    uint8_t clientCount(uint32_t now) const;

  private:
    char     line_[256];
    uint16_t len_ = 0;
    uint32_t lastRx_ = 0;
};

extern UsbLink usbLink;
