// =============================================================================
//  remoteUi.h - the remote's own display and status LED.
//
//  The remote reports the status of every command it sends. That status is
//  frequently untrue. This is deliberate and is documented on the remote's
//  own front panel, which reads "PLEASE DO NOT TRUST."
// =============================================================================
#pragma once
#include <Arduino.h>

class RemoteUi {
public:
    bool begin();
    void update();

    void splash();
    void showSent(const char *cmdName);
    void showAck(const char *cmdName, bool obeyed, uint8_t frustration);
    void showLinkLost();

    void setLinked(bool linked) { _linked = linked; }

private:
    void render();
    const char *fakeStatus();

    bool     _ready   = false;
    bool     _linked  = false;
    char     _cmd[12] = {0};
    char     _status[16] = {0};
    uint8_t  _frustration = 0;
    uint32_t _lastDraw = 0;
    uint32_t _statusUntil = 0;
    uint8_t  _lastFake = 0xFF;
};

extern RemoteUi remoteUi;
