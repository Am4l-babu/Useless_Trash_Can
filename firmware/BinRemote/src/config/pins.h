// =============================================================================
//  pins.h - BIN CONTROL SYSTEM v0.0001 (the remote)
//  Target: ESP32-C3 SuperMini (any ESP32-C3 board with these pins exposed)
// =============================================================================
//
//  ESP32-C3 pins to treat with care:
//    GPIO 2  strapping - must be HIGH at boot        -> left unused
//    GPIO 8  strapping - must be HIGH at boot        -> onboard LED only
//    GPIO 9  strapping - LOW at boot = download mode -> BOOT button, read as
//                                                       the secret button
//    GPIO 20 / 21  UART0 RX/TX. Free for GPIO use because this build logs
//                  over native USB CDC. You MUST enable
//                  "USB CDC On Boot" in the Tools menu or you lose Serial.
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// Direct buttons - one GPIO each, INPUT_PULLUP, switch to GND
// ---------------------------------------------------------------------------
#define PIN_BTN_UP      0
#define PIN_BTN_DOWN    1
#define PIN_BTN_LEFT    3
#define PIN_BTN_RIGHT   7
#define PIN_BTN_OK     10
#define PIN_BTN_OPEN   20
#define PIN_BTN_CLOSE  21

#define PIN_BTN_SECRET  9   // the BOOT button. Do not hold it during reset.

// ---------------------------------------------------------------------------
// I2C OLED (SSD1306 128x64 or 128x32)
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA     5
#define PIN_I2C_SCL     6

// ---------------------------------------------------------------------------
// Onboard status LED. Most C3 SuperMini boards wire a WS2812 here; some
// wire a plain LED. Set REMOTE_LED_IS_NEOPIXEL to match your board.
// ---------------------------------------------------------------------------
#define PIN_STATUS_LED  8
#define REMOTE_LED_IS_NEOPIXEL 1

// ---------------------------------------------------------------------------
// AUX BUTTON LADDER - eight buttons on one ADC pin
//
//  3V3
//   |
//  [10k]  pull-up
//   |
//   +---- GPIO4 (ADC1_CH4)
//   |
//   +--[ 0R ]--o/o--GND   AI
//   +--[  1k ]--o/o--GND  ANGRY
//   +--[2.2k ]--o/o--GND  MOOD
//   +--[4.7k ]--o/o--GND  STOP
//   +--[ 10k ]--o/o--GND  NORMAL
//   +--[ 22k ]--o/o--GND  MUTE
//   +--[ 47k ]--o/o--GND  LIGHT
//   +--[100k ]--o/o--GND  DARK
//
//  Expected counts assume 12-bit ADC with 11 dB attenuation. Run the
//  CALIBRATE_LADDER build below and paste your own numbers in if the
//  readings drift - ADC1 on the C3 is not especially linear near the rails.
// ---------------------------------------------------------------------------
#define PIN_AUX_LADDER  4

// Set to 1, flash, and open the serial monitor to print raw ADC counts
// while pressing each aux button. Then set it back to 0.
#define CALIBRATE_LADDER 0

// Nominal centres, in ADC counts. Order matches kLadderCommands in the .ino.
#define LADDER_CENTERS  {   40,  372,  738, 1309, 2048, 2816, 3377, 3723 }
#define LADDER_WINDOW   140   // +/- counts accepted around a centre
#define LADDER_IDLE_MIN 3950  // above this, no aux button is pressed
