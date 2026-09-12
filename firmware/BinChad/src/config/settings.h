// =============================================================================
//  settings.h - every tunable number in BIN-CHAD lives here.
//  Calibration procedure: see docs -> BUILD_GUIDE.md section "Calibration".
// =============================================================================
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Build-time feature switches
// ---------------------------------------------------------------------------
#define AUDIO_NONE       0
#define AUDIO_I2S_WAV    1   // MAX98357A + WAV files in LittleFS  (recommended)
#define AUDIO_DFPLAYER   2   // DFPlayer Mini on UART              (fallback)

#ifndef AUDIO_BACKEND
#define AUDIO_BACKEND    AUDIO_I2S_WAV
#endif

#define USE_EYE_TILT     1   // 0 = pan-only eye (one servo)
#define USE_TOF_APPROACH 1   // 0 = single ToF build (throat only)
#define USE_IR_THROAT    1   // 0 = no IR break-beam backup
#define USE_NORMAL_LAMP  1   // 0 = unlit NORMAL MODE switch
#define SERIAL_BAUD      115200

// ---------------------------------------------------------------------------
// LID GEOMETRY & SERVO CALIBRATION
//
//   Measured on the reference build (see CAD_README.md "Torque budget"):
//     lid mass incl. hinge hardware ......... 180 g
//     hinge -> centre of gravity ............ 110 mm
//     static torque at hinge ................ 0.180*9.81*0.110 = 0.194 N.m
//                                             = 1.98 kgf.cm
//     linkage mechanical advantage .......... 1.0 (direct arm, 25 mm horn)
//     required with 2x safety margin ........ 4.0 kgf.cm
//     fitted servo MG996R @ 5 V ............. 9.4 kgf.cm  -> 4.7x margin
//
//   If you change the lid, redo the sum. Do not guess.
// ---------------------------------------------------------------------------
static const uint8_t  LID_ANGLE_CLOSED     = 12;    // deg, lid resting on stops
static const uint8_t  LID_ANGLE_OPEN       = 96;    // deg, lid fully back
static const uint8_t  LID_ANGLE_PEEK       = 34;    // deg, "curious" half-open
static const bool     LID_INVERT           = false; // flip if your linkage mirrors

static const uint16_t LID_MS_OPEN          = 380;   // full travel, opening
static const uint16_t LID_MS_CLOSE         = 900;   // full travel, soft-close
static const uint16_t LID_MS_SLAM          = 220;   // angry (still eased, still safe)
static const uint16_t LID_HOLD_OPEN_MS     = 3000;  // auto-close timeout
static const uint16_t LID_STEP_MS          = 15;    // servo update period (~66 Hz)
static const uint16_t LID_LIMIT_TIMEOUT_MS = 1600;  // no limit switch => LID ERROR
static const uint16_t LID_RETRY_PAUSE_MS   = 600;   // pause before obstruction retry
static const uint8_t  LID_MAX_RETRIES      = 2;

// Detach the lid servo when parked. Kills idle jitter and buzzing, and
// lets the lid be lifted by hand without fighting the gearbox.
static const uint16_t LID_IDLE_DETACH_MS   = 700;

// ---------------------------------------------------------------------------
// EYE
// ---------------------------------------------------------------------------
static const uint8_t  EYE_PAN_CENTER   = 90;
static const uint8_t  EYE_PAN_MIN      = 35;
static const uint8_t  EYE_PAN_MAX      = 145;
static const uint8_t  EYE_TILT_CENTER  = 90;
static const uint8_t  EYE_TILT_MIN     = 62;   // looking down, into the bin
static const uint8_t  EYE_TILT_MAX     = 118;  // looking up, at the user
static const uint16_t EYE_STEP_MS      = 18;
static const uint16_t EYE_MOVE_MS      = 260;  // default saccade duration
static const uint16_t EYE_IDLE_DETACH_MS = 900;

// ---------------------------------------------------------------------------
// USELESS FINGER
// The signature mechanism. Timings are deliberately theatrical: the pause
// before the hatch opens is what makes the audience laugh.
// ---------------------------------------------------------------------------
static const uint8_t  DOOR_ANGLE_SHUT    = 8;
static const uint8_t  DOOR_ANGLE_OPEN    = 92;
static const uint8_t  FINGER_ANGLE_HOME  = 18;
static const uint8_t  FINGER_ANGLE_PRESS = 112;  // overshoots the switch on purpose
static const uint16_t FINGER_DOOR_MS     = 260;
static const uint16_t FINGER_EXTEND_MS   = 420;
static const uint16_t FINGER_RETRACT_MS  = 520;
static const uint16_t FINGER_DWELL_MS    = 260;  // held on the switch
static const uint16_t FINGER_PAUSE_MS    = 900;  // the dramatic beat, hatch open
static const uint16_t FINGER_STEP_MS     = 15;

// ---------------------------------------------------------------------------
// SENSORS
// ---------------------------------------------------------------------------
static const uint16_t TOF_THROAT_CLEAR_MM   = 260;  // > this = nothing in the throat
static const uint16_t TOF_THROAT_OBJECT_MM  = 200;  // < this = something is there
static const uint16_t TOF_APPROACH_NEAR_MM  = 900;  // person within a metre
static const uint16_t TOF_APPROACH_FAR_MM   = 1400; // hysteresis, person left
static const uint16_t TOF_INVALID_MM        = 8190; // driver's out-of-range marker
static const uint16_t TOF_NO_SAMPLE         = 0xFFFF; // "measurement not ready", != out of range
static const uint8_t  TOF_CONFIRM_SAMPLES   = 2;    // debounce, in samples
static const uint16_t SENSOR_POLL_MS        = 33;   // ~30 Hz
static const uint8_t  SENSOR_FAIL_LIMIT     = 12;   // consecutive errors -> degraded
static const uint16_t SWITCH_DEBOUNCE_MS    = 35;

// Safety window: anything closer than this while the lid is moving down
// is treated as a hand. Non-negotiable, see personality/behaviour overrides.
static const uint16_t SAFETY_HAND_MM        = 130;

// ---------------------------------------------------------------------------
// TIMING / BEHAVIOUR
// ---------------------------------------------------------------------------
static const uint16_t THROW_WINDOW_MS       = 2600;  // wait for the object to arrive
static const uint16_t MISS_SILENCE_MS       = 2000;  // the judgemental pause
static const uint16_t REACTION_MIN_MS       = 900;
static const uint32_t SLEEP_AFTER_MS        = 90000UL;
static const uint32_t ANGRY_TIMEOUT_MS      = 20000UL;
static const uint16_t NORMAL_MODE_GRACE_MS  = 4500;  // how long "normal" lasts
static const uint16_t SELFTEST_STEP_MS      = 420;

// Escalation thresholds (see personality.cpp)
static const uint8_t  MISS_ANNOY_AT         = 3;
static const uint8_t  MISS_CONCERN_AT       = 5;
static const uint8_t  MISS_ANGRY_AT         = 10;
static const uint8_t  REMOTE_FRUSTRATION_MAX= 12;

// ---------------------------------------------------------------------------
// LEDS
// ---------------------------------------------------------------------------
static const uint16_t LED_COUNT      = 12;
static const uint8_t  LED_BRIGHTNESS = 90;    // keep the 5 V rail honest
static const uint8_t  LED_BRIGHT_MAX = 255;   // the "DARK" button joke
static const uint16_t LED_FRAME_MS   = 25;    // 40 fps

// ---------------------------------------------------------------------------
// AUDIO
// ---------------------------------------------------------------------------
static const uint8_t  AUDIO_VOLUME_DEFAULT = 18;  // 0..30
static const uint8_t  AUDIO_VOLUME_MAX     = 30;  // the "MUTE" button joke
static const uint16_t AUDIO_CHUNK_BYTES    = 512; // per loop tick, keeps loop free

// ---------------------------------------------------------------------------
// REMOTE
// ---------------------------------------------------------------------------
static const uint32_t REMOTE_TIMEOUT_MS  = 4000;  // no packet -> remoteConnected=false
static const uint16_t REMOTE_REPEAT_MS   = 250;   // ignore duplicate sequence inside this
// The device id itself lives in remote/protocol.h - one definition, shared
// by both firmwares, so the two can never disagree about it.
