// =============================================================================
//  webLayer.h - HTTP server, WebSocket, and the JSON protocol.
//
//  The WebSocket callback runs in the AsyncTCP task, NOT in loop(). It is
//  therefore allowed to do exactly three things: reject oversized frames,
//  parse and validate JSON, and push a fixed-size struct into a spinlocked
//  ring. Nothing here touches a motor. Everything that acts on a command
//  happens in loop(), via Pipeline.
//
//  The one exception is the emergency stop, which sets the latch directly on
//  arrival so that it does not wait even one loop iteration.
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../core/types.h"
#include "../command/pipeline.h"
#include "../sound/soundBank.h"

class WebLayer {
  public:
    // Mounts LittleFS. Called from setup() before anything that needs files
    // (the sound bank reads its map from there). Safe to call once.
    bool mountFs();

    void begin();
    void tick(uint32_t now);

    // Drained by loop(). Returns false when the queue is empty.
    bool popInbound(InboundMsg& out);

    // One JSON text -> validate -> queue. This is the path every link uses:
    // the WebSocket callback, a BLE write, and a line on the USB serial port.
    // Safe from any task. `data` need not be NUL-terminated.
    void ingest(uint32_t clientId, const char* data, size_t len);

    void broadcastTelemetry(uint32_t now);
    void broadcastNewEvents();
    void broadcastConfig();
    void broadcastAudioChanged();       // "refetch /api/audio"
    void sendSound(const SoundMsg& m);  // the "play this" message, WebSocket flavour

    // A browser says "sound is unlocked here" inside its pings. That is the
    // only way this board can know it has a speaker.
    void noteAudioReady(uint32_t now) { lastAudioReady_ = now ? now : 1; }
    bool audioReady(uint32_t now) const;

    uint8_t clientCount() const;
    bool    filesystemReady() const { return fsReady_; }

    // Deferred so the HTTP handler can answer before the board goes away.
    void requestReboot(uint32_t inMs);

  private:
    bool     fsMounted_ = false;
    bool     fsReady_ = false;
    uint32_t lastTelemetry_ = 0;
    uint32_t lastEventSeq_  = 0;
    uint32_t rebootAt_      = 0;
    uint32_t lastAudioReady_ = 0;
};

extern WebLayer webLayer;

// Handed to Pipeline::begin(); called from loop() only.
void webSendCommandReport(const CommandReport& r);
