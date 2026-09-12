#include "display.h"
#include "../config/pins.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

Display display;

#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
#define DISPLAY_FRAME_MS 40

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

bool Display::begin() {
    // Wire.begin() has already been called by Sensors::begin().
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("[display] SSD1306 not found at 0x3C"));
        _ready = false;
        return false;
    }
    _ready = true;
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.display();
    _nextBlink = millis() + 2000;
    return true;
}

void Display::drawCentered(const char *s, int16_t y, uint8_t size) {
    oled.setTextSize(size);
    int16_t x1, y1; uint16_t w, h;
    oled.getTextBounds(s, 0, y, &x1, &y1, &w, &h);
    int16_t x = (int16_t)((OLED_W - (int16_t)w) / 2);
    if (x < 0) x = 0;
    oled.setCursor(x, y);
    oled.print(s);
}

// ---------------------------------------------------------------------------
// The eye. openPx is the vertical half-height of the aperture: shrinking it
// gives blinks, angry squints and sleep, all from one primitive.
// ---------------------------------------------------------------------------
void Display::drawEye(int16_t cx, int16_t cy, uint8_t openPx, uint8_t pupilDx) {
    const int16_t rx = 30;
    if (openPx < 2) {
        oled.drawFastHLine((int16_t)(cx - rx), cy, rx * 2, SSD1306_WHITE);
        return;
    }
    // Outer aperture: two arcs approximated by a filled rounded rect.
    oled.fillRoundRect((int16_t)(cx - rx), (int16_t)(cy - openPx),
                       rx * 2, openPx * 2, (int16_t)(openPx > 8 ? 10 : openPx),
                       SSD1306_WHITE);
    // Pupil punched out of it.
    const int16_t pr = (int16_t)((openPx - 1) < 11 ? (openPx - 1) : 11);
    if (pr > 1) {
        oled.fillCircle((int16_t)(cx + (int8_t)pupilDx), cy, pr, SSD1306_BLACK);
        oled.fillCircle((int16_t)(cx + (int8_t)pupilDx + 3), (int16_t)(cy - 3),
                        2, SSD1306_WHITE);   // catchlight
    }
}

void Display::renderFace() {
    oled.clearDisplay();

    uint8_t open = 16;
    int8_t  brow = 0;

    switch (_expr) {
        case FACE_HAPPY:      open = 12; break;
        case FACE_ANGRY:      open = 8;  brow = 1; break;
        case FACE_CONFUSED:   open = 15; break;
        case FACE_SUSPICIOUS: open = 6;  break;
        case FACE_SLEEPY:     open = 3;  break;
        case FACE_SCANNING:   open = 16; break;
        case FACE_DEAD:       open = 0;  break;
        default:              open = 16; break;
    }

    // Blink handling (skipped when already mostly shut).
    const uint32_t now = millis();
    if (_expr != FACE_DEAD && _expr != FACE_SLEEPY) {
        if (_blinkStage == 0 && now > _nextBlink) { _blinkStage = 1; _nextBlink = now + 80; }
        else if (_blinkStage == 1 && now > _nextBlink) { _blinkStage = 2; _nextBlink = now + 70; }
        else if (_blinkStage == 2 && now > _nextBlink) {
            _blinkStage = 0;
            _nextBlink = now + random(1800, 5200);
        }
        if (_blinkStage == 1) open = 2;
    }

    // Idle pupil drift, plus a fast scan sweep in SCANNING.
    if (_expr == FACE_SCANNING) {
        _pupilDx = (int8_t)(12 * sinf((float)_frame * 0.25f));
    } else if (_expr == FACE_CONFUSED) {
        _pupilDx = (int8_t)(8 * sinf((float)_frame * 0.08f));
    } else if ((_frame % 40) == 0) {
        _pupilDx = (int8_t)random(-6, 7);
    }

    drawEye(OLED_W / 2, 34, open, (uint8_t)_pupilDx);

    if (brow) {   // angry brow
        oled.fillTriangle(30, 12, 94, 12, 62, 24, SSD1306_WHITE);
        oled.fillTriangle(30, 12, 94, 12, 62, 22, SSD1306_BLACK);
        oled.drawLine(30, 10, 94, 20, SSD1306_WHITE);
    }
    if (_expr == FACE_SLEEPY) {
        oled.setTextSize(1);
        oled.setCursor(96, 6);
        oled.print(F("z"));
        oled.setCursor(104, 2);
        oled.print(F("Z"));
    }
    oled.display();
}

void Display::renderText() {
    oled.clearDisplay();
    if (_big) {
        drawCentered(_l1, 24, 2);
    } else {
        const bool three = _l3[0] != 0;
        if (three) {
            drawCentered(_l1, 8,  1);
            drawCentered(_l2, 28, 1);
            drawCentered(_l3, 48, 1);
        } else if (_l2[0]) {
            drawCentered(_l1, 16, 1);
            drawCentered(_l2, 36, 1);
        } else {
            drawCentered(_l1, 28, 1);
        }
    }
    oled.display();
}

void Display::renderProgress() {
    oled.clearDisplay();
    drawCentered(_progLabel, 10, 1);
    oled.drawRect(12, 30, 104, 14, SSD1306_WHITE);
    const int16_t w = (int16_t)((100L * _progPercent) / 100);   // 100 px of travel
    oled.fillRect(14, 32, w, 10, SSD1306_WHITE);
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", _progPercent);
    drawCentered(pct, 50, 1);
    oled.display();
}

void Display::setExpression(FaceExpression e) { _expr = e; }

void Display::showFace() {
    _mode = MODE_FACE;
    _holdUntil = 0;
}

void Display::say(const char *l1, const char *l2, const char *l3, uint16_t durationMs) {
    if (!_ready) return;
    strncpy(_l1, l1 ? l1 : "", sizeof(_l1) - 1); _l1[sizeof(_l1) - 1] = 0;
    strncpy(_l2, l2 ? l2 : "", sizeof(_l2) - 1); _l2[sizeof(_l2) - 1] = 0;
    strncpy(_l3, l3 ? l3 : "", sizeof(_l3) - 1); _l3[sizeof(_l3) - 1] = 0;
    _big  = false;
    _mode = MODE_TEXT;
    _holdUntil = durationMs ? (millis() + durationMs) : 0;
    _lastDraw = 0;   // force an immediate redraw
}

void Display::sayBig(const char *text, uint16_t durationMs) {
    say(text, nullptr, nullptr, durationMs);
    _big = true;
}

void Display::showProgress(const char *label, uint8_t percent) {
    if (!_ready) return;
    strncpy(_progLabel, label ? label : "", sizeof(_progLabel) - 1);
    _progLabel[sizeof(_progLabel) - 1] = 0;
    _progPercent = percent > 100 ? 100 : percent;
    _mode = MODE_PROGRESS;
    _holdUntil = 0;
    _lastDraw = 0;
}

void Display::bootScreen() {
    if (!_ready) return;
    oled.clearDisplay();
    drawCentered("BIN-CHAD", 6, 2);
    drawCentered("booting...", 30, 1);
    drawCentered("v0.0001", 50, 1);
    oled.display();
    _mode = MODE_SELFTEST;
    _selfTestRow = 0;
}

void Display::selfTestLine(const char *item, bool ok) {
    if (!_ready) return;
    if (_selfTestRow == 0) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setCursor(0, 0);
        oled.print(F("BOOTING..."));
    }
    const int16_t y = (int16_t)(12 + _selfTestRow * 9);
    if (y > 55) {   // scrolled off; start a fresh page
        oled.clearDisplay();
        _selfTestRow = 0;
        oled.setTextSize(1);
        oled.setCursor(0, 0);
        oled.print(F("BOOTING..."));
    }
    oled.setTextSize(1);
    oled.setCursor(0, (int16_t)(12 + _selfTestRow * 9));
    char line[24];
    snprintf(line, sizeof(line), "%-10s%s", item, ok ? "OK" : "FAIL");
    // dot-leader, because it looks more official than it has any right to
    for (char *p = line; *p; ++p) if (*p == ' ') *p = '.';
    oled.print(line);
    oled.display();
    _selfTestRow++;
}

void Display::selfTestDone(uint8_t uselessnessPercent) {
    if (!_ready) return;
    oled.clearDisplay();
    drawCentered("USELESSNESS", 14, 1);
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", uselessnessPercent);
    drawCentered(pct, 30, 2);
    oled.display();
    _mode = MODE_TEXT;
    _holdUntil = millis() + 1500;
}

void Display::update() {
    if (!_ready) return;
    const uint32_t now = millis();
    if (now - _lastDraw < DISPLAY_FRAME_MS) return;
    _lastDraw = now;
    _frame++;

    if (_mode == MODE_SELFTEST) return;     // driven by selfTestLine()

    if (_holdUntil && now > _holdUntil) {
        _holdUntil = 0;
        _mode = MODE_FACE;
    }

    switch (_mode) {
        case MODE_TEXT:     renderText();     break;
        case MODE_PROGRESS: renderProgress(); break;
        case MODE_FACE:
        default:            renderFace();     break;
    }
}
