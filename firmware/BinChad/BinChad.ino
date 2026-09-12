// =============================================================================
//  BIN-CHAD  -  The Uncooperative Waste Management System
//  Main controller firmware for ESP32-S3.
//
//  Board:   ESP32S3 Dev Module
//  Core:    Arduino-ESP32 3.x   https://github.com/espressif/arduino-esp32
//  Docs:    https://docs.espressif.com/projects/arduino-esp32/en/latest/
//
//  Required Arduino IDE settings (Tools menu):
//      USB CDC On Boot .......... Enabled      (so Serial works over USB-C)
//      Flash Size ............... 8MB or 16MB, to match your board
//      Partition Scheme ......... Default 4MB with spiffs, or any scheme
//                                 that includes a LittleFS partition
//      PSRAM .................... match your module (OPI for N16R8)
//      Upload Mode .............. UART0 / Hardware CDC
//
//  Libraries (Library Manager):
//      ESP32Servo            https://github.com/madhephaestus/ESP32Servo
//      Adafruit VL53L0X      https://github.com/adafruit/Adafruit_VL53L0X
//      Adafruit SSD1306      https://github.com/adafruit/Adafruit_SSD1306
//      Adafruit GFX Library  https://github.com/adafruit/Adafruit-GFX-Library
//      Adafruit NeoPixel     https://github.com/adafruit/Adafruit_NeoPixel
//
//  Sound files: upload the contents of data/ to LittleFS with the
//  "ESP32 LittleFS Data Upload" tool, or arduino-cli. See FIRMWARE_README.md.
//
//  The architecture rule for this sketch: loop() calls update() on every
//  subsystem, every pass, and nothing in the entire codebase calls delay()
//  after setup() has returned. If you add a delay() here, the lid stops
//  checking for fingers, which is the one thing it must never stop doing.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>

#include "src/config/pins.h"
#include "src/config/settings.h"
#include "src/hardware/sensors.h"
#include "src/hardware/lid.h"
#include "src/hardware/eye.h"
#include "src/hardware/leds.h"
#include "src/hardware/audio.h"
#include "src/hardware/uselessFinger.h"
#include "src/ui/display.h"
#include "src/remote/remote.h"
#include "src/personality/personality.h"
#include "src/behavior/behaviorManager.h"

static uint32_t lastHeartbeat = 0;

// A seed that differs per board but is stable across reboots of the same
// board: the demo replays identically, two bins in the same room do not.
static uint32_t makeSeed() {
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    uint32_t s = 0x811C9DC5;
    for (int i = 0; i < 6; ++i) { s ^= mac[i]; s *= 16777619UL; }
    return s ? s : 0x1BADB002;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(150);                       // let USB CDC enumerate, boot only
    Serial.println();
    Serial.println(F("=== BIN-CHAD v0.0001 ==="));

#if USE_NORMAL_LAMP
    pinMode(PIN_NORMAL_LAMP, OUTPUT);
    digitalWrite(PIN_NORMAL_LAMP, LOW);
#endif

    // Order matters:
    //   sensors first  - it owns Wire.begin(), and the lid needs the limit
    //                    switches to be readable before it homes.
    //   display second - so the boot screen is up before the slow bits.
    //   lid last       - it is the only subsystem that can hurt anyone, so
    //                    it comes up once everything that can stop it is
    //                    already running.
    const bool okSensors = sensors.begin();
    const bool okDisplay = display.begin();
    const bool okLeds    = leds.begin();
    const bool okAudio   = audio.begin();
    const bool okEye     = eye.begin();
    const bool okFinger  = finger.begin();
    const bool okRemote  = remoteLink.begin();
    const bool okLid     = lid.begin();

    Serial.printf("sensors:%d display:%d leds:%d audio:%d eye:%d finger:%d remote:%d lid:%d\r\n",
                  okSensors, okDisplay, okLeds, okAudio, okEye, okFinger, okRemote, okLid);

    personality.begin(makeSeed());
    behavior.begin();

    lastHeartbeat = millis();
}

void loop() {
    // Perception before actuation, always: the lid's obstruction check reads
    // sensor state that must have been refreshed this same pass.
    sensors.update();

    lid.update();
    eye.update();
    finger.update();
    leds.update();
    audio.update();
    display.update();

    remoteLink.update();
    personality.update();
    behavior.update();

    // Heartbeat on the serial console. Useful during the build, and the
    // fastest way to tell a hung loop from a hung mechanism at a hackathon.
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        Serial.printf("[hb] %s/%s lid=%u ang=%u frus=%u thr=%umm app=%umm cyc=%lu rx=%lu\r\n",
                      behavior.stateName(),
                      personality.stateName(),
                      (unsigned)lid.state(),
                      personality.anger(),
                      personality.frustration(),
                      sensors.throatMm(),
                      sensors.approachMm(),
                      (unsigned long)lid.cycles(),
                      (unsigned long)remoteLink.packetsReceived());
    }
}
