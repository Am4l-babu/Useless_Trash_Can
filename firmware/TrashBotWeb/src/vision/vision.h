// =============================================================================
//  vision.h - what the camera saw, and whether that is news.
//
//  No detection runs here. This board has no camera and a classic ESP32 could
//  not run a detector if it had one. Detection runs somewhere else and the
//  result arrives as a report:
//
//    - WebSocket  {"type":"vision","persons":1,"objects":"cup,bottle",
//                  "conf":83,"src":"phone"}     from the VISION tab, which runs
//                                              TensorFlow.js on the phone's
//                                              camera or on ESP32-CAM snapshots
//    - HTTP       POST /api/vision  (form)     from any camera board on Wi-Fi
//    - UART2      "VISION persons=1 objects=cup,bottle conf=83\n"
//                                              from a camera board on two wires
//
//  This module keeps the latest state, times out a source that goes quiet,
//  and turns raw reports into events: a person ARRIVING is news, a person
//  still standing there is not; a cup seen for the first time in a while is
//  news, the same cup ten frames later is not. The events trigger sounds and
//  nudge the personality. Nothing here can move a motor.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../config/settings.h"
#include "../core/types.h"

enum VisionSource : uint8_t {
    VSRC_NONE = 0,
    VSRC_PHONE,     // the phone's own camera, detection in the browser
    VSRC_CAM,       // ESP32-CAM snapshots, detection in the browser
    VSRC_UART,      // a camera board on PIN_VISION_RX
    VSRC_HTTP,      // a camera board posting to /api/vision
    VSRC_COUNT
};

const char* visionSourceName(uint8_t s);
uint8_t     parseVisionSource(const char* s);   // VSRC_HTTP if unknown

struct VisionReport {
    uint8_t source;
    uint8_t persons;
    uint8_t conf;                    // 0..100
    char    objects[INBOUND_TEXT_MAX];
};

class Vision {
  public:
    void begin();
    void report(const VisionReport& r, uint32_t now);   // loop() only
    void tick(uint32_t now);                            // loop() only

    bool        live(uint32_t now) const;               // a source reported recently
    uint8_t     source() const      { return source_; }
    const char* sourceName() const  { return visionSourceName(source_); }
    bool        personPresent() const { return present_; }
    uint8_t     persons() const     { return persons_; }
    uint8_t     conf() const        { return conf_; }
    const char* objects() const     { return objects_; }
    uint32_t    ageMs(uint32_t now) const { return lastReport_ ? now - lastReport_ : 0xFFFFFFFF; }
    uint32_t    humansSeen() const  { return humansSeen_; }
    uint32_t    objectsSeen() const { return objectsSeen_; }

  private:
    void pollUart(uint32_t now);
    void parseUartLine(char* line, uint32_t now);
    bool remember(const char* name, uint32_t now);      // true if this is news

    uint8_t  source_   = VSRC_NONE;
    uint8_t  persons_  = 0;
    uint8_t  conf_     = 0;
    char     objects_[INBOUND_TEXT_MAX] = {0};
    uint32_t lastReport_ = 0;

    bool     present_ = false;
    uint32_t lastPersonSeen_ = 0;
    uint32_t humansSeen_ = 0;
    uint32_t objectsSeen_ = 0;

    // Recently announced objects, so the same cup is not news every frame.
    static const uint8_t RECENT = 8;
    char     recent_[RECENT][20];
    uint32_t recentMs_[RECENT];

    char     line_[96];
    uint8_t  lineLen_ = 0;
};

extern Vision vision;
