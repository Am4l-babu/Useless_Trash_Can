// =============================================================================
//  pins.h - GPIO map for TRASHBOT WEB REMOTE (plain ESP32 dev module).
//
//  This is NOT the BIN-CHAD pin map. BIN-CHAD is an ESP32-S3 driving servos;
//  this board is a classic ESP32 driving an L298N and two DC motors. The two
//  firmwares share no pins, no protocol and no wiring.
//
//  The assignment below is the one already proven on the bench by
//  tests/l298n_motor_test - do not renumber it without re-running that sketch.
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// L298N motor driver
//
//   ENA/ENB are PWM (speed). IN1..IN4 are plain digital (direction).
//   L298N GND -> ESP32 GND is mandatory; without the common ground the
//   driver sees no logic levels at all and "nothing happens".
//   L298N +12V -> external motor supply. Never the ESP32 5V/3V3 pin.
// ---------------------------------------------------------------------------
#define PIN_MOTOR_ENA 18   // left motor  speed  (PWM)
#define PIN_MOTOR_ENB 19   // right motor speed  (PWM)
#define PIN_MOTOR_IN1 27   // left motor  direction A
#define PIN_MOTOR_IN2 26   // left motor  direction B
#define PIN_MOTOR_IN3 25   // right motor direction A
#define PIN_MOTOR_IN4 33   // right motor direction B

// ---------------------------------------------------------------------------
// Vision link - UART2 to an optional camera board (ESP32-CAM, XIAO ESP32-S3
// Sense, anything that can print a line). Text protocol, see
// src/vision/vision.h. Cross the wires: camera board TX -> PIN_VISION_RX.
// Both boards must share GND. 3.3 V logic on both sides.
//
// GPIO 16/17 are free on a WROOM module. On a WROVER module they are the
// PSRAM bus and must be moved (13/14 are free).
// ---------------------------------------------------------------------------
#define PIN_VISION_RX 16   // <- camera board TX
#define PIN_VISION_TX 17   // -> camera board RX (unused by the current protocol)

// ---------------------------------------------------------------------------
// Reserved for later. Deliberately NOT #defined: an undefined pin cannot be
// driven by accident, and the firmware must build and run with none of this
// hardware fitted. Add the #define at the same time as the driver module.
//
//   lid servo ............ any PWM-capable GPIO (13/14/23 are free)
//   WS2812 data .......... 4
//   I2S audio ............ 21 / 22 / 32  (BCLK / LRC / DIN)
//   OLED I2C ............. 21 SDA / 22 SCL  (conflicts with I2S above - pick one)
//   limit switches ....... 34 / 35  (input-only pins, external pull-ups needed)
//
// GPIO 6-11 are the SPI flash and are never available.
// GPIO 34-39 are input-only and have no internal pull-ups.
// ---------------------------------------------------------------------------
