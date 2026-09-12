#include "usbLink.h"
#include <Arduino.h>
#include "../config/settings.h"
#include "../core/types.h"
#include "webLayer.h"

UsbLink usbLink;

void UsbLink::begin() {
    len_ = 0;
#if TRASHBOT_USB_LINK
    Serial.println("USB link: sounds appear as 'SND {...}' lines; JSON lines in are commands");
#endif
}

uint8_t UsbLink::clientCount(uint32_t now) const {
    return (lastRx_ && (uint32_t)(now - lastRx_) < LINK_TIMEOUT_MS) ? 1 : 0;
}

void UsbLink::tick(uint32_t now) {
#if TRASHBOT_USB_LINK
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (len_) {
                line_[len_] = 0;
                if (line_[0] == '{') {
                    webLayer.ingest(CLIENT_ID_USB, line_, len_);
                    lastRx_ = now ? now : 1;
                }
                len_ = 0;
            }
        } else if (len_ < sizeof(line_) - 1) {
            line_[len_++] = c;
        } else {
            len_ = 0;                                    // overlong line: discard
        }
    }
#else
    (void)now;
#endif
}

void UsbLink::sendSound(const SoundMsg& m) {
#if TRASHBOT_USB_LINK
    Serial.printf("SND {\"type\":\"sound\",\"seq\":%lu,\"event\":\"%s\",\"clip\":\"%s\"}\n",
                  (unsigned long)m.seq, soundEventInfo(m.event).id, m.clip);
#else
    (void)m;
#endif
}
