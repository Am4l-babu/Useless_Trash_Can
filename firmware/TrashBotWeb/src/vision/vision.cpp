#include "vision.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "../config/pins.h"
#include "../core/eventLog.h"
#include "../sound/soundBank.h"
#include "../personality/personality.h"

Vision vision;

const char* visionSourceName(uint8_t s) {
    switch (s) {
        case VSRC_PHONE: return "phone";
        case VSRC_CAM:   return "cam";
        case VSRC_UART:  return "uart";
        case VSRC_HTTP:  return "http";
        default:         return "none";
    }
}

uint8_t parseVisionSource(const char* s) {
    if (!s) return VSRC_HTTP;
    if (!strcmp(s, "phone")) return VSRC_PHONE;
    if (!strcmp(s, "cam"))   return VSRC_CAM;
    if (!strcmp(s, "uart"))  return VSRC_UART;
    return VSRC_HTTP;
}

// ---------------------------------------------------------------------------
void Vision::begin() {
    memset(recent_, 0, sizeof(recent_));
    memset(recentMs_, 0, sizeof(recentMs_));
#if TRASHBOT_VISION_UART
    Serial2.begin(VISION_UART_BAUD, SERIAL_8N1, PIN_VISION_RX, PIN_VISION_TX);
    Serial2.setTimeout(0);
#endif
}

bool Vision::live(uint32_t now) const {
    return lastReport_ && (uint32_t)(now - lastReport_) < VISION_SOURCE_TIMEOUT_MS;
}

// Has this object been announced inside VISION_OBJECT_MEMORY_MS? If not,
// record it and say so. The ring is tiny on purpose: it holds "things on the
// table right now", not a history.
bool Vision::remember(const char* name, uint32_t now) {
    if (!name || !name[0]) return false;
    uint8_t oldest = 0;
    for (uint8_t i = 0; i < RECENT; ++i) {
        if (recent_[i][0] && strcmp(recent_[i], name) == 0) {
            bool fresh = (uint32_t)(now - recentMs_[i]) >= VISION_OBJECT_MEMORY_MS;
            recentMs_[i] = now;
            return fresh;
        }
        if (recentMs_[i] < recentMs_[oldest]) oldest = i;
    }
    strlcpy(recent_[oldest], name, sizeof(recent_[oldest]));
    recentMs_[oldest] = now;
    return true;
}

// ---------------------------------------------------------------------------
void Vision::report(const VisionReport& r, uint32_t now) {
    if (source_ != r.source && r.source != VSRC_NONE) {
        eventLog.push("VISION", "reports now arriving from %s", visionSourceName(r.source));
    }
    source_     = r.source;
    persons_    = r.persons;
    conf_       = r.conf > 100 ? 100 : r.conf;
    lastReport_ = now ? now : 1;
    strlcpy(objects_, r.objects, sizeof(objects_));

    // People. Arrival is the event; standing still is not.
    if (persons_ > 0) {
        lastPersonSeen_ = lastReport_;
        if (!present_) {
            present_ = true;
            ++humansSeen_;
            eventLog.push("HUMAN_DETECTED", "%u person%s via %s (%u%%)",
                          persons_, persons_ == 1 ? "" : "s", visionSourceName(source_), conf_);
            soundBank.trigger(SND_HUMAN_DETECTED, now);
            personality.noteHuman(now);
        }
    }

    // Objects. Walk the csv; each name that is not in recent memory is news.
    char buf[INBOUND_TEXT_MAX];
    strlcpy(buf, objects_, sizeof(buf));
    bool news = false;
    char* save = nullptr;
    for (char* tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(nullptr, ",", &save)) {
        while (*tok == ' ') ++tok;
        if (!*tok || strcmp(tok, "person") == 0) continue;   // people are counted, not listed
        if (remember(tok, now)) {
            ++objectsSeen_;
            eventLog.push("OBJECT_DETECTED", "%s via %s (%u%%)", tok, visionSourceName(source_), conf_);
            news = true;
        }
    }
    if (news) {
        soundBank.trigger(SND_OBJECT_DETECTED, now);
        personality.noteObject(now);
    }
}

// ---------------------------------------------------------------------------
void Vision::tick(uint32_t now) {
#if TRASHBOT_VISION_UART
    pollUart(now);
#endif

    if (present_ && (uint32_t)(now - lastPersonSeen_) >= VISION_PERSON_HOLD_MS) {
        present_ = false;
        eventLog.push("HUMAN_LOST", "nobody in view for %u ms", (unsigned)VISION_PERSON_HOLD_MS);
        soundBank.trigger(SND_HUMAN_LOST, now);
    }

    if (source_ != VSRC_NONE && !live(now)) {
        eventLog.push("VISION", "%s stopped reporting", visionSourceName(source_));
        source_  = VSRC_NONE;
        persons_ = 0;
        objects_[0] = 0;
        // present_ falls out via the hold timer above, using the last sighting.
    }
}

// ---------------------------------------------------------------------------
// UART: one line per report, no blocking, no waiting for a terminator.
//
//   VISION persons=1 objects=cup,bottle conf=83
//
// Keys may come in any order; unknown keys are ignored; a line that does not
// start with VISION is a debug print from the camera board and is dropped.
// ---------------------------------------------------------------------------
void Vision::pollUart(uint32_t now) {
    while (Serial2.available()) {
        char c = (char)Serial2.read();
        if (c == '\n' || c == '\r') {
            if (lineLen_) {
                line_[lineLen_] = 0;
                parseUartLine(line_, now);
                lineLen_ = 0;
            }
        } else if (lineLen_ < sizeof(line_) - 1) {
            line_[lineLen_++] = c;
        } else {
            lineLen_ = 0;                                // overlong: discard the line
        }
    }
}

void Vision::parseUartLine(char* line, uint32_t now) {
    if (strncmp(line, "VISION", 6) != 0) return;
    VisionReport r{};
    r.source = VSRC_UART;
    char* save = nullptr;
    for (char* tok = strtok_r(line + 6, " ", &save); tok; tok = strtok_r(nullptr, " ", &save)) {
        char* eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        const char* val = eq + 1;
        if (!strcmp(tok, "persons"))      r.persons = (uint8_t)constrain(atoi(val), 0, 255);
        else if (!strcmp(tok, "conf"))    r.conf    = (uint8_t)constrain(atoi(val), 0, 100);
        else if (!strcmp(tok, "objects")) strlcpy(r.objects, val, sizeof(r.objects));
    }
    report(r, now);
}
