// =============================================================================
//  eventLog.h - the scrolling log the browser shows, and the serial trace.
//
//  Every transformation the personality engine performs is logged here. That
//  is the difference between a machine that is being funny and a machine that
//  is being broken: when someone says "it went the wrong way", the log says
//  whether that was a decision or a fault.
//
//  push() is called from both the main loop and the AsyncTCP callback task,
//  so the ring is spinlock-protected.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../config/settings.h"

struct LogEntry {
    uint32_t seq;        // monotonic; the browser is sent everything newer than it has
    uint32_t ms;
    char     code[20];   // USER_CONNECTED, COMMAND_MODIFIED, SAFETY_STOP, ...
    char     text[72];
};

class EventLog {
  public:
    void begin();
    void push(const char* code, const char* fmt, ...);

    uint8_t  count() const { return count_; }
    uint32_t lastSeq() const { return seq_; }

    // i = 0 is the oldest entry still held. Returns false if i is out of range.
    bool get(uint8_t i, LogEntry& out) const;

  private:
    LogEntry buf_[EVENT_LOG_SIZE];
    uint8_t  head_  = 0;
    uint8_t  count_ = 0;
    uint32_t seq_   = 0;
};

extern EventLog eventLog;
