#include "remoteUi.h"
#include "../config/pins.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

RemoteUi remoteUi;

#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
#define UI_FRAME_MS 60

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

// Statuses the remote may report. Exactly one of them is ever accurate,
// and not reliably that one.
static const char *const kFakeStatuses[] = {
    "OK", "SENT", "ACCEPTED", "PROBABLY", "DEFINITELY",
    "ERROR 0", "QUEUED", "TRUST ME"
};

bool RemoteUi::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("[ui] no OLED at 0x3C - continuing headless"));
        _ready = false;
        return false;
    }
    _ready = true;
    oled.setTextColor(SSD1306_WHITE);
    splash();
    return true;
}

void RemoteUi::splash() {
    if (!_ready) return;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println(F("BIN CONTROL SYSTEM"));
    oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    oled.setCursor(0, 16);
    oled.println(F("v0.0001"));
    oled.setCursor(0, 34);
    oled.println(F("PLEASE DO NOT"));
    oled.setCursor(0, 44);
    oled.println(F("TRUST."));
    oled.display();
    _statusUntil = millis() + 2500;
}

const char *RemoteUi::fakeStatus() {
    const uint8_t n = (uint8_t)(sizeof(kFakeStatuses) / sizeof(kFakeStatuses[0]));
    uint8_t i;
    do { i = (uint8_t)random(n); } while (i == _lastFake);
    _lastFake = i;
    return kFakeStatuses[i];
}

void RemoteUi::showSent(const char *cmdName) {
    strncpy(_cmd, cmdName, sizeof(_cmd) - 1);
    _cmd[sizeof(_cmd) - 1] = 0;
    strncpy(_status, "SENDING", sizeof(_status) - 1);
    _statusUntil = millis() + 1500;
    _lastDraw = 0;
}

void RemoteUi::showAck(const char *cmdName, bool obeyed, uint8_t frustration) {
    strncpy(_cmd, cmdName, sizeof(_cmd) - 1);
    _cmd[sizeof(_cmd) - 1] = 0;
    _frustration = frustration;

    // Report the truth about a quarter of the time. The rest is decoration.
    if (random(100) < 25) {
        strncpy(_status, obeyed ? "OBEYED" : "IGNORED", sizeof(_status) - 1);
    } else {
        strncpy(_status, fakeStatus(), sizeof(_status) - 1);
    }
    _status[sizeof(_status) - 1] = 0;
    _statusUntil = millis() + 2000;
    _lastDraw = 0;
}

void RemoteUi::showLinkLost() {
    strncpy(_status, "NO CARRIER", sizeof(_status) - 1);
    _statusUntil = millis() + 1500;
    _lastDraw = 0;
}

void RemoteUi::render() {
    if (!_ready) return;
    oled.clearDisplay();

    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print(F("BIN CONTROL  "));
    oled.print(_linked ? F("[LINK]") : F("[----]"));
    oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    oled.setCursor(0, 16);
    oled.print(F("CMD: "));
    oled.setTextSize(2);
    oled.setCursor(0, 26);
    oled.print(_cmd[0] ? _cmd : "----");

    oled.setTextSize(1);
    oled.setCursor(0, 46);
    oled.print(F("STATUS: "));
    oled.print(_status[0] ? _status : "IDLE");

    // A "compliance" meter that only ever goes down.
    const uint8_t pct = (_frustration > 12) ? 0 : (uint8_t)(100 - _frustration * 8);
    oled.setCursor(0, 56);
    oled.print(F("COMPLIANCE: "));
    oled.print(pct);
    oled.print('%');

    oled.display();
}

void RemoteUi::update() {
    if (!_ready) return;
    const uint32_t now = millis();
    if (now - _lastDraw < UI_FRAME_MS) return;
    _lastDraw = now;

    if (_statusUntil && now > _statusUntil) {
        _statusUntil = 0;
        _status[0] = 0;
    }
    render();
}
