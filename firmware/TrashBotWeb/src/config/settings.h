// =============================================================================
//  settings.h - every tunable number in the TRASHBOT web remote.
//
//  Values that must survive a reflash (motor calibration, Wi-Fi credentials)
//  are defaults only - the live copy lives in NVS and is edited from the
//  SETUP tab in the browser. Everything else is compile-time.
// =============================================================================
#pragma once
#include <stdint.h>

#define TRASHBOT_VERSION "0.2.0"
#define SERIAL_BAUD      115200

// ---------------------------------------------------------------------------
// NETWORK
//
//   STA first. If there are no stored credentials, or the join fails inside
//   WIFI_CONNECT_TIMEOUT_MS, the board brings up its own AP instead so the
//   remote still works in a room with no usable Wi-Fi. Either way the UI is
//   at http://trashcan.local (mDNS) or the raw IP printed on the serial port.
// ---------------------------------------------------------------------------
// Macros rather than constants: a string constant that a translation unit
// does not happen to use is an unused-variable warning, and this header is
// included everywhere.
#define WIFI_HOSTNAME "trashcan"

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
static const uint32_t WIFI_RETRY_MS           = 20000;   // STA retry while in AP fallback

// AP fallback. The password is not protecting anything valuable, but an open
// AP would let anyone in the room drive the motors, so it is set and WPA2.
// Change it from the SETUP tab; 8 characters is the WPA2 minimum.
#define AP_SSID_DEFAULT "TRASHBOT-SETUP"
#define AP_PASS_DEFAULT "uselessbin"

static const uint16_t HTTP_PORT               = 80;
static const uint16_t WS_MAX_FRAME_BYTES      = 512;     // anything larger is refused unparsed
static const uint8_t  WS_MAX_CLIENTS          = 4;

// ---------------------------------------------------------------------------
// MOTORS / L298N
//
//   Duty is 8-bit. MIN_DUTY exists because a loaded DC motor below roughly
//   25% duty will buzz and heat instead of turning: any non-zero request is
//   lifted to MIN_DUTY so "slow" means slow, not stalled.
//
//   DECEL_MULT is the one asymmetry that matters: stopping is always allowed
//   to outrun starting. Never set it below 1.
// ---------------------------------------------------------------------------
static const uint32_t MOTOR_PWM_FREQ_HZ       = 5000;    // inaudible, well inside L298N range
static const uint8_t  MOTOR_PWM_BITS          = 8;
static const uint8_t  MOTOR_DUTY_CEILING      = 255;     // absolute hardware limit
static const uint8_t  MOTOR_DEFAULT_MAX_DUTY  = 200;     // conservative until calibrated
static const uint8_t  MOTOR_DEFAULT_MIN_DUTY  = 70;
static const uint8_t  MOTOR_DEFAULT_ACCEL     = 12;      // duty per tick: 0 -> 200 in ~330 ms
static const uint8_t  MOTOR_LAZY_ACCEL        = 2;       // LAZY drive mode
static const uint8_t  MOTOR_DECEL_MULT        = 3;
static const uint16_t MOTOR_TICK_MS           = 20;      // ramp update period
static const uint16_t MOTOR_TEST_MAX_MS       = 2000;    // calibration jog auto-expires

// ---------------------------------------------------------------------------
// SAFETY
//
//   Three independent timeouts, shortest first. None of them can be extended,
//   suppressed or delayed by the personality engine.
//
//     DRIVE_TIMEOUT  no fresh drive intent      -> ramp target to zero
//     LINK_TIMEOUT   no inbound traffic at all  -> hard stop, no ramp
//     zero clients   browser gone               -> hard stop, immediately
//
//   The browser repeats a held joystick every 100 ms and pings every 400 ms,
//   so each timeout tolerates three or four lost messages before it fires.
// ---------------------------------------------------------------------------
static const uint16_t DRIVE_TIMEOUT_MS        = 450;
static const uint16_t LINK_TIMEOUT_MS         = 1500;
static const uint16_t TELEMETRY_MS            = 100;
static const uint16_t CLIENT_PING_MS          = 400;     // documented for the browser side

// ---------------------------------------------------------------------------
// PERSONALITY
//
//   DELAY_MAX_MS is the longest the bot may sit on a command before acting.
//   It applies only to starting or changing movement - never to stopping.
// ---------------------------------------------------------------------------
static const uint16_t TRAIT_TICK_MS           = 1000;
static const uint16_t DELAY_MIN_MS            = 200;
static const uint16_t DELAY_MAX_MS            = 2500;
static const uint32_t PANIC_DURATION_MS       = 9000;
static const uint32_t IDLE_BOREDOM_MS         = 5000;    // no commands -> boredom climbs
static const uint16_t REPEAT_WINDOW_MS        = 1200;    // mashing the same key counts as nagging
static const uint16_t APOLOGY_WINDOW_MS       = 6000;    // apologies inside this get cheaper

// The NORMAL MODE gag. The green button grants normal behaviour for a random
// interval in this range, then "mechanical intervention" takes it away again.
// The mode selector's NORMAL entry is a separate, genuine, permanent mode.
static const uint16_t NORMAL_GRACE_MIN_MS     = 600;
static const uint16_t NORMAL_GRACE_MAX_MS     = 4500;

// ---------------------------------------------------------------------------
// EVENT LOG
// ---------------------------------------------------------------------------
static const uint8_t  EVENT_LOG_SIZE          = 48;      // ring, oldest dropped
static const uint8_t  EVENT_BACKLOG_ON_JOIN   = 16;      // replayed to a new browser

// ---------------------------------------------------------------------------
// SOUND
//
//   The bin has no speaker. The phone that has the dashboard open is the
//   speaker: the firmware only ever sends a tiny "play this" message, and the
//   browser plays a clip it has already fetched. Clips are uploaded from the
//   AUDIO tab and live on LittleFS under SND_DIR so every phone gets the same
//   library; the event -> clip map lives next to them as JSON.
//
//   Cooldowns are per event, so a held joystick does not fire "obeyed" ten
//   times a second. The emergency stop bypasses every cooldown.
// ---------------------------------------------------------------------------
#define SND_DIR       "/audio"
#define SND_MAP_PATH  "/audio/map.json"
static const uint8_t  SND_NAME_MAX            = 40;      // clip filename, including NUL
static const uint32_t SND_MAX_CLIP_BYTES      = 512UL * 1024;
static const uint16_t SND_EVENT_COOLDOWN_MS   = 1500;    // same event, again
static const uint16_t SND_GLOBAL_GAP_MS       = 250;     // any event after any other
static const uint16_t SND_AUDIO_READY_TTL_MS  = 10000;   // a phone that said "sound on" counts for this long

// ---------------------------------------------------------------------------
// VISION
//
//   Detection does not run on this board. It runs in the phone's browser
//   (TensorFlow.js on the phone camera or on ESP32-CAM snapshots) or on a
//   separate camera board, and the RESULT is reported here over the
//   WebSocket, over HTTP, or as a text line on UART2. This module only keeps
//   the state and decides when something new has happened.
// ---------------------------------------------------------------------------
#define TRASHBOT_VISION_UART 1                            // listen on PIN_VISION_RX/TX
static const uint32_t VISION_UART_BAUD        = 115200;
static const uint16_t VISION_PERSON_HOLD_MS   = 2500;    // absent this long before "lost"
static const uint16_t VISION_OBJECT_MEMORY_MS = 8000;    // seen again inside this is not "new"
static const uint16_t VISION_SOURCE_TIMEOUT_MS= 5000;    // no report -> source considered gone

// ---------------------------------------------------------------------------
// TRIGGER LINKS
//
//   Three ways the "play this" message can reach the phone: the WebSocket it
//   already has, a BLE notification, and a line on the USB serial port. All
//   three carry the same sequence number so a phone on two links plays once.
//   BLE and USB also accept the same JSON commands the WebSocket does.
// ---------------------------------------------------------------------------
#define TRASHBOT_BLE       1
#define TRASHBOT_USB_LINK  1
#define BLE_DEVICE_NAME    "TRASHBOT"
#define BLE_SVC_UUID       "7a5b0001-8e6f-4c1d-9b2a-3f4e5d6c7b8a"
#define BLE_SOUND_UUID     "7a5b0002-8e6f-4c1d-9b2a-3f4e5d6c7b8a"   // notify: "seq;eventId;clip"
#define BLE_CMD_UUID       "7a5b0003-8e6f-4c1d-9b2a-3f4e5d6c7b8a"   // write:  same JSON as the WebSocket
static const uint16_t USB_LINE_MAX            = 256;

// ---------------------------------------------------------------------------
// HARDWARE INVENTORY
//
//   Fitted or not. The UI reports anything false as NOT INSTALLED and shows
//   its on-screen state as SIMULATED. Nothing here is auto-detected, because
//   none of this hardware can actually be detected on these pins - see
//   HardwareStatus in the README.
// ---------------------------------------------------------------------------
static const bool HW_LID_SERVO   = false;
static const bool HW_CAMERA      = false;   // no camera on THIS board - vision arrives from the phone or a cam board
static const bool HW_MICROPHONE  = false;
static const bool HW_LEDS        = false;
static const bool HW_SPEAKER     = false;   // no speaker on THIS board - the phone plays the sounds
static const bool HW_DISPLAY     = false;
static const bool HW_TOF         = false;
static const bool HW_ENCODERS    = false;
static const bool HW_BATTERY_ADC = false;   // no divider fitted -> no battery percentage
static const bool HW_FINGER      = false;   // the NORMAL MODE defeat mechanism
