// =============================================================================
//  display.h - 0.96" SSD1306, used as BIN-CHAD's face and status screen.
//
//  Three render modes:
//    FACE      an animated eye that blinks, narrows and looks around
//    TEXT      up to three centred lines, for the one-liners
//    PROGRESS  a labelled bar, for the fake AI and fake firmware updates
//
//  Everything is non-blocking. Text is posted with a duration; when it
//  expires the display falls back to FACE on its own.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../config/settings.h"

enum FaceExpression : uint8_t {
    FACE_NEUTRAL,
    FACE_HAPPY,
    FACE_ANGRY,
    FACE_CONFUSED,
    FACE_SUSPICIOUS,
    FACE_SLEEPY,
    FACE_SCANNING,
    FACE_DEAD
};

class Display {
public:
    bool begin();
    void update();

    // ---- face -------------------------------------------------------------
    void setExpression(FaceExpression e);
    FaceExpression expression() const { return _expr; }
    void showFace();                       // return to face mode now

    // ---- text -------------------------------------------------------------
    // durationMs = 0 means "hold until something else is posted".
    void say(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr,
             uint16_t durationMs = 2200);
    void sayBig(const char *text, uint16_t durationMs = 2000);

    // ---- progress ---------------------------------------------------------
    void showProgress(const char *label, uint8_t percent);

    // ---- boot / self-test -------------------------------------------------
    void bootScreen();
    void selfTestLine(const char *item, bool ok);
    void selfTestDone(uint8_t uselessnessPercent);

    bool isAvailable() const { return _ready; }
    bool isBusy() const { return _holdUntil != 0 && millis() < _holdUntil; }

private:
    void renderFace();
    void renderText();
    void renderProgress();
    void drawEye(int16_t cx, int16_t cy, uint8_t openPx, uint8_t pupilDx);
    void drawCentered(const char *s, int16_t y, uint8_t size);

    enum Mode : uint8_t { MODE_FACE, MODE_TEXT, MODE_PROGRESS, MODE_SELFTEST };

    bool     _ready     = false;
    Mode     _mode      = MODE_FACE;
    FaceExpression _expr = FACE_NEUTRAL;

    char     _l1[22] = {0}, _l2[22] = {0}, _l3[22] = {0};
    bool     _big    = false;
    uint32_t _holdUntil = 0;
    uint32_t _lastDraw  = 0;
    uint32_t _frame     = 0;

    char     _progLabel[22] = {0};
    uint8_t  _progPercent   = 0;

    // self-test scroll state
    int8_t   _selfTestRow = 0;

    uint32_t _nextBlink = 0;
    uint8_t  _blinkStage = 0;
    int8_t   _pupilDx    = 0;
};

extern Display display;
