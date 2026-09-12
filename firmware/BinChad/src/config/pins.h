// =============================================================================
//  pins.h - BIN-CHAD main controller GPIO map
//  Target: ESP32-S3-DevKitC-1 (N8R2 / N16R8), 44-pin module
// =============================================================================
//
//  RESERVED / DO NOT USE on ESP32-S3-DevKitC-1:
//    GPIO 0        strapping (BOOT button)
//    GPIO 3        strapping (JTAG source select)
//    GPIO 19 / 20  native USB D- / D+
//    GPIO 26..32   SPI flash
//    GPIO 33..37   octal PSRAM (N8R8/N16R8 boards only - free on N8R2)
//    GPIO 39..42   JTAG
//    GPIO 43 / 44  UART0 TX/RX (serial monitor)
//    GPIO 45 / 46  strapping (VSPI / boot mode)
//
//  Everything below is chosen from the always-safe set:
//    1,2,4..18,21,38,47,48
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// I2C bus 0 - VL53L0X x2 + SSD1306 OLED
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA            8
#define PIN_I2C_SCL            9

// ToF XSHUT lines. Both sensors boot at 0x29; we hold them in reset and
// bring them up one at a time to re-address. See sensors.cpp.
#define PIN_TOF_THROAT_XSHUT   47   // looks down the bin opening
#define PIN_TOF_APPROACH_XSHUT 48   // looks out at the room

// ---------------------------------------------------------------------------
// Servos (all on the switched 5 V servo rail, never on the ESP32 5 V pin)
// ---------------------------------------------------------------------------
#define PIN_SERVO_LID           4   // MG996R / DS3218, lid linkage
#define PIN_SERVO_EYE_PAN       5   // SG90 / MG90S
#define PIN_SERVO_EYE_TILT      6   // SG90 / MG90S (optional, see USE_EYE_TILT)
#define PIN_SERVO_FINGER        7   // SG90 / MG90S, the accusatory finger
#define PIN_SERVO_FINGER_DOOR  15   // SG90, hidden hatch

// ---------------------------------------------------------------------------
// Digital inputs (all INPUT_PULLUP, switches close to GND)
// ---------------------------------------------------------------------------
#define PIN_LIMIT_LID_CLOSED   10   // microswitch, LOW = lid home
#define PIN_LIMIT_LID_OPEN     11   // microswitch, LOW = lid fully open
#define PIN_SWITCH_NORMAL_MODE 12   // big illuminated latching switch, LOW = ON
#define PIN_HIDDEN_TRIGGER     13   // concealed demo-rescue button, LOW = pressed
#define PIN_IR_THROAT          14   // optional IR break-beam / proximity, LOW = blocked

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------
#define PIN_LED_DATA           16   // WS2812B DIN (through 330R, level-shift if flaky)
#define PIN_SERVO_POWER_EN     38   // HIGH = servo rail MOSFET on (soft E-stop)
#define PIN_NORMAL_LAMP         2   // lamp inside the NORMAL MODE switch (via MOSFET)

// ---------------------------------------------------------------------------
// I2S audio - MAX98357A
// ---------------------------------------------------------------------------
#define PIN_I2S_BCLK           17
#define PIN_I2S_LRCLK          18
#define PIN_I2S_DOUT           21

// ---------------------------------------------------------------------------
// DFPlayer Mini alternative (only used when AUDIO_BACKEND == AUDIO_DFPLAYER).
// Shares no pins with the I2S amp so both can be stuffed on one board.
// ---------------------------------------------------------------------------
#define PIN_DFPLAYER_TX        17   // ESP32 TX -> DFPlayer RX (through 1k)
#define PIN_DFPLAYER_RX        18   // ESP32 RX <- DFPlayer TX
