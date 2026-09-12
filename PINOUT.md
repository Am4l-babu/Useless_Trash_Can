# PINOUT

Exact GPIO assignments for both boards. These tables are the authority; they
match `firmware/BinChad/src/config/pins.h` and
`firmware/BinRemote/src/config/pins.h` line for line. If you change a pin,
change it in the header **and** here.

---

## 1. Main controller — ESP32-S3-DevKitC-1

### 1.1 Pins you must not use

| GPIO | Why |
|---|---|
| 0 | Strapping pin (BOOT button). Pulling it low at reset enters download mode. |
| 3 | Strapping pin (JTAG source select). |
| 19, 20 | Native USB D− / D+. Using them kills USB-CDC serial and uploads. |
| 26–32 | SPI flash. Not brought out to the header on most boards for a reason. |
| 33–37 | Octal PSRAM on **N8R8 / N16R8** modules. Free on N8R2 (quad/no PSRAM). Assume they are taken unless you have checked your exact module. |
| 39–42 | JTAG. Usable, but you lose hardware debugging. |
| 43, 44 | UART0 TX/RX — the serial monitor. |
| 45, 46 | Strapping pins (boot mode / VSPI). |

Everything assigned below comes from the always-safe set:
**1, 2, 4–18, 21, 38, 47, 48**.

### 1.2 Assignments

| GPIO | Direction | Net | Connects to | Notes |
|---:|---|---|---|---|
| 2 | OUT | `NORMAL_LAMP` | MOSFET gate → lamp in the NORMAL MODE switch | Logic-level N-FET, lamp on the 5 V rail. Do **not** drive the lamp directly. |
| 4 | OUT (PWM) | `SERVO_LID` | MG996R signal | 50 Hz, 500–2500 µs. Servo power from the servo rail, **not** the ESP32. |
| 5 | OUT (PWM) | `SERVO_EYE_PAN` | SG90/MG90S signal | |
| 6 | OUT (PWM) | `SERVO_EYE_TILT` | SG90/MG90S signal | Omit if `USE_EYE_TILT 0`. |
| 7 | OUT (PWM) | `SERVO_FINGER` | SG90/MG90S signal | The finger. |
| 8 | I/O | `I2C_SDA` | VL53L0X ×2, SSD1306 | 4.7 kΩ pull-up to 3V3. Most breakouts already have one — do not stack four of them. |
| 9 | OUT | `I2C_SCL` | VL53L0X ×2, SSD1306 | 4.7 kΩ pull-up to 3V3. |
| 10 | IN pull-up | `LIMIT_CLOSED` | Lid closed microswitch → GND | Active LOW. Wire to the **NC-to-GND** contact so a broken wire reads "not closed" and faults safe. |
| 11 | IN pull-up | `LIMIT_OPEN` | Lid open microswitch → GND | Active LOW. |
| 12 | IN pull-up | `SWITCH_NORMAL` | NORMAL MODE latching switch → GND | Active LOW = ON. |
| 13 | IN pull-up | `HIDDEN_TRIGGER` | Concealed demo-rescue button → GND | Active LOW. See `HACKATHON_DEMO.md` §5. |
| 14 | IN pull-up | `IR_THROAT` | IR break-beam receiver / proximity module OUT | Active LOW = blocked. Optional (`USE_IR_THROAT`). |
| 15 | OUT (PWM) | `SERVO_FINGER_DOOR` | SG90 signal | The hatch. |
| 16 | OUT | `LED_DATA` | WS2812B DIN | Through 330 Ω, as close to the first pixel as possible. |
| 17 | OUT | `I2S_BCLK` | MAX98357A BCLK | Shared with DFPlayer TX if `AUDIO_BACKEND = AUDIO_DFPLAYER`. |
| 18 | OUT | `I2S_LRCLK` | MAX98357A LRC | Shared with DFPlayer RX in the DFPlayer build. |
| 21 | OUT | `I2S_DOUT` | MAX98357A DIN | |
| 38 | OUT | `SERVO_POWER_EN` | P-FET gate driver / relay coil driver | **HIGH = servo rail live.** Firmware pulls it LOW on `Lid::emergencyStop()`. Defaults LOW at reset, which is the safe state. |
| 47 | OUT | `TOF_THROAT_XSHUT` | VL53L0X #1 XSHUT | Held LOW at boot, then released to re-address to 0x30. |
| 48 | OUT | `TOF_APPROACH_XSHUT` | VL53L0X #2 XSHUT | Released second, re-addressed to 0x31. |

### 1.3 I²C address map

| Address | Device | Set by |
|---|---|---|
| 0x29 | VL53L0X (factory default) | Transient only — both sensors are moved off it at boot. |
| 0x30 | VL53L0X — throat | `Sensors::begin()` |
| 0x31 | VL53L0X — approach | `Sensors::begin()` |
| 0x3C | SSD1306 OLED | Module solder jumper. If yours is 0x3D, change `OLED_ADDR` in `display.cpp`. |

The XSHUT sequencing matters: both sensors boot at 0x29, so they are held in
reset, brought up one at a time and re-addressed. If XSHUT is left floating
the second sensor will never be found, and `healthSummary()` reports
`PARTIAL`.

---

## 2. Remote — ESP32-C3 SuperMini

### 2.1 Pins you must treat carefully

| GPIO | Why |
|---|---|
| 2 | Strapping — must be HIGH at reset. **Left unused.** |
| 8 | Strapping — must be HIGH at reset. Used only for the onboard status LED (output, idles as the LED driver wants). |
| 9 | Strapping — LOW at reset enters download mode. Used as the SECRET button. Do not hold it while resetting. |
| 20, 21 | UART0 RX/TX. Free here **only because the build logs over native USB CDC** — you must set *USB CDC On Boot = Enabled*. |

### 2.2 Assignments

| GPIO | Direction | Function | Command sent |
|---:|---|---|---|
| 0 | IN pull-up | Button — UP | `CMD_UP` |
| 1 | IN pull-up | Button — DOWN | `CMD_DOWN` |
| 3 | IN pull-up | Button — LEFT | `CMD_LEFT` |
| 4 | IN (ADC1_CH4) | **Aux resistor ladder**, 8 buttons | see below |
| 5 | I/O | I²C SDA — OLED | — |
| 6 | OUT | I²C SCL — OLED | — |
| 7 | IN pull-up | Button — RIGHT | `CMD_RIGHT` |
| 8 | OUT | Onboard status LED (WS2812 on most boards) | — |
| 9 | IN pull-up | BOOT button — SECRET | `CMD_SECRET` |
| 10 | IN pull-up | Button — OK | `CMD_OK` |
| 20 | IN pull-up | Button — OPEN | `CMD_OPEN` |
| 21 | IN pull-up | Button — CLOSE | `CMD_CLOSE` |

### 2.3 Aux button ladder

Eight buttons on one ADC pin. 10 kΩ pull-up to 3V3; each button pulls the node
to GND through a different resistor.

```
3V3 ──[10k]──┬── GPIO4 (ADC1_CH4)
             ├──[   0R ]──o/o── GND   AI
             ├──[   1k ]──o/o── GND   ANGRY
             ├──[ 2.2k ]──o/o── GND   MOOD
             ├──[ 4.7k ]──o/o── GND   STOP
             ├──[  10k ]──o/o── GND   NORMAL
             ├──[  22k ]──o/o── GND   MUTE
             ├──[  47k ]──o/o── GND   LIGHT
             └──[ 100k ]──o/o── GND   DARK
```

| Button | R to GND | V at node | Expected ADC (12-bit, 11 dB) |
|---|---:|---:|---:|
| AI | 0 Ω | 0.00 V | ~40 |
| ANGRY | 1 kΩ | 0.30 V | ~372 |
| MOOD | 2.2 kΩ | 0.60 V | ~738 |
| STOP | 4.7 kΩ | 1.06 V | ~1309 |
| NORMAL | 10 kΩ | 1.65 V | ~2048 |
| MUTE | 22 kΩ | 2.27 V | ~2816 |
| LIGHT | 47 kΩ | 2.72 V | ~3377 |
| DARK | 100 kΩ | 3.00 V | ~3723 |
| *(none)* | ∞ | 3.30 V | ~4095 |

ADC1 on the C3 is not especially linear near the rails, so **measure your own
values**: set `CALIBRATE_LADDER 1` in `pins.h`, flash, open the serial
monitor, press each button, and paste the readings into `LADDER_CENTERS`.
`LADDER_WINDOW` (±140 counts) is the accept band around each centre; readings
between bands decode as "nothing pressed", which is also what you get when two
buttons are pressed at once.

---

## 3. Quick continuity checklist

Before first power-on, with the supply disconnected:

- [ ] No continuity between 5 V and GND anywhere.
- [ ] No continuity between 3V3 and GND.
- [ ] Every servo ground returns to the star ground point, not through the ESP32 board.
- [ ] SDA and SCL each read ~4.7 kΩ to 3V3 and open to GND.
- [ ] Both limit switches read open at rest and short to GND when pressed.
- [ ] GPIO38 gate driver holds the servo rail **off** with the ESP32 unpowered.
