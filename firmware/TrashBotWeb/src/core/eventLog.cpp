#include "eventLog.h"
#include <Arduino.h>
#include <stdarg.h>

EventLog eventLog;

static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void EventLog::begin() {
    head_ = 0;
    count_ = 0;
    seq_ = 0;
}

void EventLog::push(const char* code, const char* fmt, ...) {
    char text[72];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    uint32_t ms = millis();

    portENTER_CRITICAL(&logMux);
    LogEntry& e = buf_[head_];
    e.seq = ++seq_;
    e.ms  = ms;
    strncpy(e.code, code, sizeof(e.code) - 1);
    e.code[sizeof(e.code) - 1] = '\0';
    strncpy(e.text, text, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
    head_ = (uint8_t)((head_ + 1) % EVENT_LOG_SIZE);
    if (count_ < EVENT_LOG_SIZE) count_++;
    portEXIT_CRITICAL(&logMux);

    Serial.printf("[%8lu] %-18s %s\n", (unsigned long)ms, code, text);
}

bool EventLog::get(uint8_t i, LogEntry& out) const {
    bool ok = false;
    portENTER_CRITICAL(&logMux);
    if (i < count_) {
        // head_ points at the next write slot, so the oldest live entry is
        // head_ - count_ once the ring has wrapped.
        uint8_t idx = (uint8_t)((head_ + EVENT_LOG_SIZE - count_ + i) % EVENT_LOG_SIZE);
        out = buf_[idx];
        ok = true;
    }
    portEXIT_CRITICAL(&logMux);
    return ok;
}
