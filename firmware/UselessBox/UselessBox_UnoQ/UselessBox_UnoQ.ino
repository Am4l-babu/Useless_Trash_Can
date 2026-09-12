#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>

//Board: Arduino UNO Q (arduino:zephyr:unoq)

//Connect servo signal to D11, +5V to 5V, GND to GND
//Connect the switch between D12 and GND (internal pull-up, no resistor needed)
#define SERVO_PIN 11
#define SWITCH_PIN 12

//Built-in LED, flashes when ready, on when in attitude mode
#define LED_BUILTIN_PIN LED_BUILTIN

//Defines the range of motion for the servo, starting(resting) and ending(hitting the switch)
#define POS_START 180
#define POS_END 45

//Active buzzer - just needs HIGH/LOW, it has its own oscillator
#define BUZZER_PIN 3

//Three HC-SR04 ultrasonic sensors, read round-robin (one per loop pass).
//Any of them seeing something closer than the threshold starts the spin.
#define SENSOR_COUNT 3
const int TRIG_PINS[SENSOR_COUNT] = { 2, A0, A2 };
const int ECHO_PINS[SENSOR_COUNT] = { 4, A1, A3 };
#define ECHO_TIMEOUT_US 12000UL   // ~2 m - keeps a no-echo read from stalling the loop
#define DISTANCE_THRESHOLD_CM 20
#define ROTATE_DURATION_MS 10000UL

//IR obstacle sensor - HIGH = no object detected ("open"), LOW = object detected.
//Flip this if your module's output logic is the opposite.
#define IR_PIN 13
#define IR_NO_OBJECT HIGH
#define BUZZER_DURATION_MS 10000UL

//L298N-style motor driver
#define ENA 5
#define ENB 6
#define IN1 7
#define IN2 8
#define IN3 9
#define IN4 10
#define MOTOR_SPEED 200   // 0-255

//0.96" 4-pin SSD1306 OLED on the SDA/SCL header pins (= A4/A5 on this board)
#define FRAME_MS 50

//Face geometry
#define EYE_L_X 40
#define EYE_R_X 88
#define EYE_CY 26
#define EYE_W 32
#define EYE_H 32

enum Mood { MOOD_RELAX, MOOD_DIZZY, MOOD_ANGRY };

#define OLED_BLACK 0
#define OLED_WHITE 1

// Minimal SSD1306 driver. Adafruit_SSD1306 doesn't compile against the Zephyr
// core, and U8g2's hardware-I2C path is hard-wired to Wire - but on the UNO Q
// the SDA/SCL header pins are i2c3, which the core exposes as Wire2, so neither
// library can actually reach the panel. Adafruit_GFX only needs drawPixel, so
// subclass it and drive the bus here.
class OledFace : public Adafruit_GFX {
public:
  OledFace() : Adafruit_GFX(128, 64) {}

  // Wire2 is the SDA/SCL header; the other buses are tried only as a fallback.
  bool begin() {
    TwoWire *buses[] = { &Wire2, &Wire1, &Wire };
    const uint8_t addrs[] = { 0x3C, 0x3D };
    for (uint8_t b = 0; b < 3; b++) {
      buses[b]->begin();
      for (uint8_t a = 0; a < 2; a++) {
        buses[b]->beginTransmission(addrs[a]);
        if (buses[b]->endTransmission() == 0) {
          wire = buses[b];
          addr = addrs[a];
          initPanel();
          return true;
        }
      }
    }
    return false;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    uint16_t i = x + (y / 8) * 128;
    uint8_t bit = 1 << (y & 7);
    if (color) buf[i] |= bit;
    else buf[i] &= ~bit;
  }

  void clear() { memset(buf, 0, sizeof(buf)); }

  void flush() {
    cmd(0x21); cmd(0); cmd(127);   // column range
    cmd(0x22); cmd(0); cmd(7);     // page range
    for (uint16_t i = 0; i < sizeof(buf); i += 128) {
      wire->beginTransmission(addr);
      wire->write((uint8_t)0x40);  // data stream
      wire->write(buf + i, 128);
      wire->endTransmission();
    }
  }

private:
  void cmd(uint8_t c) {
    wire->beginTransmission(addr);
    wire->write((uint8_t)0x00);    // command stream
    wire->write(c);
    wire->endTransmission();
  }

  void initPanel() {
    static const uint8_t seq[] = {
      0xAE,              // display off
      0xD5, 0x80,        // clock divide
      0xA8, 0x3F,        // 64 rows
      0xD3, 0x00,        // no vertical offset
      0x40,              // start line 0
      0x8D, 0x14,        // charge pump on - without this the panel stays dark
      0x20, 0x00,        // horizontal addressing
      0xA1, 0xC8,        // flip both axes so the face isn't upside down
      0xDA, 0x12,        // alternating COM pins
      0x81, 0xCF,        // contrast
      0xD9, 0xF1,
      0xDB, 0x40,
      0xA4,              // follow RAM
      0xA6,              // non-inverted
      0x2E,              // scrolling off
      0xAF               // display on
    };
    for (uint8_t i = 0; i < sizeof(seq); i++) cmd(seq[i]);
  }

  TwoWire *wire = nullptr;
  uint8_t addr = 0x3C;
  uint8_t buf[1024];
};

OledFace face;
bool hasDisplay = false;
unsigned long nextFrame = 0;
bool blinking = false;
unsigned long blinkAt = 0;

Servo myservo;
int pos = POS_START;
int target = POS_START;
int seq = 0;
int rapidCount = -1;
long lastInput = 0;
bool goRandom;

int sensorIndex = 0;
bool rotating = false;
unsigned long rotateStopAt = 0;
bool buzzing = false;
unsigned long buzzStopAt = 0;

void setup() {
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN_PIN, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  for (int i = 0; i < SENSOR_COUNT; i++) {
    pinMode(TRIG_PINS[i], OUTPUT);
    digitalWrite(TRIG_PINS[i], LOW);
    pinMode(ECHO_PINS[i], INPUT);
  }

  pinMode(IR_PIN, INPUT);

  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  motorsStop();

  // Finds its own bus and address; false only means nothing answered, which
  // shouldn't take the rest of the box down with it.
  hasDisplay = face.begin();
  if (hasDisplay) {
    face.clear();
    face.flush();
  }

  myservo.attach(SERVO_PIN);

  digitalWrite(LED_BUILTIN_PIN, HIGH);  // flash the led to show it's ready.
  gotoPercent(50, 1, 0);  // do a small initialization to show it's ready
  gotoPercent(0, 1, 0);
  digitalWrite(LED_BUILTIN_PIN, LOW);

  int switchState = digitalRead(SWITCH_PIN);
  if (switchState == LOW) { //Reads the switch at startup to set random
    goRandom = false;
  } else {
    goRandom = true;
  }

  randomSeed(micros()); //A0 is a TRIG pin now, so seed off the clock instead
}

void loop() {
  handleProximityRotate();
  handleIrBuzzer();
  drawFace();

  int switchState = digitalRead(SWITCH_PIN);
  if (switchState == LOW) {
    if ((millis() - lastInput) < 10000) {  // if the last switch ended less than 10 seconds ago, increase the number of rapid switches
      rapidCount++;
    } else {
      rapidCount = 0;
    }
    lastInput = millis();  // set the last switch to now
    if (rapidCount < 4) {   // if there have been fewer than 4 switches in a row with 10 seconds between them, just do the basic sequence.
      seq = 0;
    } else {
      if (goRandom == true) {
        seq = int(random(1, 10));
      } else {
        seq++;
        if (seq > 9) { seq = 1; }
      }
    }
    if (seq == 0) Seq00();
    if (seq == 1) Seq01();
    if (seq == 2) Seq02();
    if (seq == 3) Seq03();
    if (seq == 4) Seq04();
    if (seq == 5) Seq05();
    if (seq == 6) Seq06();
    if (seq == 7) Seq07();
    if (seq == 8) Seq08();
    if (seq == 9) Seq09();
    lastInput = millis();  // set the last switch to now
  } else {
    delay(50);
  }
  if (millis() - lastInput < 10000 && rapidCount >= 3) {  //illuminate the builtin LED when it's mad or about to be mad
    digitalWrite(LED_BUILTIN_PIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN_PIN, LOW);
  }
}

void Seq00() {
  gotoPercent(100, 3, 0);
  gotoPercent(0, 3, 0);
}

void Seq01() {
  gotoPercent(100, 3, 0);
  gotoPercent(0, 1, 0);
}

void Seq02() {
  gotoPercent(100, 1, 2000);
  gotoPercent(0, 7, 0);
}

void Seq03() {
  gotoPercent(100, 10, 0);
  gotoPercent(75, 1, 1000);
  gotoPercent(100, 10, 0);
  gotoPercent(75, 10, 0);
  gotoPercent(100, 10, 0);
  gotoPercent(0, 5, 0);
}

void Seq04() {
  gotoPercent(25, 1, 500);
  gotoPercent(50, 1, 500);
  gotoPercent(75, 1, 2000);
  gotoPercent(100, 10, 0);
  gotoPercent(0, 5, 0);
}

void Seq05() {
  gotoPercent(50, 5, 0);
  gotoPercent(0, 1, 300);
  gotoPercent(75, 5, 0);
  gotoPercent(0, 1, 300);
  gotoPercent(80, 5, 1000);
  gotoPercent(70, 5, 0);
  gotoPercent(100, 10, 0);
  gotoPercent(0, 3, 0);
}

void Seq06() {
  gotoPercent(100, 3, 0);
  gotoPercent(80, 1, 500);
  gotoPercent(100, 8, 500);
  gotoPercent(80, 1, 500);
  gotoPercent(100, 8, 500);
  gotoPercent(80, 1, 500);
  gotoPercent(50, 1, 500);
  gotoPercent(25, 1, 500);
  gotoPercent(0, 1, 0);
}

void Seq07() {
  gotoPercent(100, 2, 0);
  for (int i = 0; i < 10; i++) {
    gotoPercent(80, 3, 0);
    gotoPercent(100, 6, 0);
  }
  gotoPercent(100, 6, 2000);
  gotoPercent(50, 1, 500);
  gotoPercent(0, 1, 0);
}

void Seq08() {
  for (int i = 30; i <= 100; i += 10) {
    gotoPercent(i, 7, 0);
    gotoPercent(i - 30, 4, 0);
  }
  gotoPercent(0, 5, 0);
}

void Seq09() {
  for (int i = 0; i < random(10, 20); i++) {
    gotoPercent(random(0, 80), random(1, 10), random(0, 500));
  }
  gotoPercent(100, random(1, 10), random(0, 500));
  gotoPercent(0, random(1, 10), 0);
}

// Reads one HC-SR04. Returns distance in cm, or -1 if nothing echoed back (out of range).
long readDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2;
}

// Any sensor seeing something within DISTANCE_THRESHOLD_CM -> spin in place (CCW)
// for ROTATE_DURATION_MS. One sensor is read per pass so three pulseIn timeouts
// never stack up in a single loop. Doesn't retrigger while already spinning.
void handleProximityRotate() {
  if (rotating) {
    if (millis() >= rotateStopAt) {
      motorsStop();
      rotating = false;
    }
    return;
  }

  long distance = readDistanceCM(TRIG_PINS[sensorIndex], ECHO_PINS[sensorIndex]);
  sensorIndex = (sensorIndex + 1) % SENSOR_COUNT;

  if (distance > 0 && distance < DISTANCE_THRESHOLD_CM) {
    motorsRotateCCW();
    rotating = true;
    rotateStopAt = millis() + ROTATE_DURATION_MS;
  }
}

// IR reports no object ("open") -> buzzer on for BUZZER_DURATION_MS.
void handleIrBuzzer() {
  if (buzzing) {
    if (millis() >= buzzStopAt) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzing = false;
    }
    return;
  }

  if (digitalRead(IR_PIN) == IR_NO_OBJECT) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzing = true;
    buzzStopAt = millis() + BUZZER_DURATION_MS;
  }
}

void motorsStop() {
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// Motor A reverse + Motor B forward = rotate in place. If your box spins CW
// instead of CCW, swap the IN1/IN2 pair (or IN3/IN4 - whichever is wrong).
void motorsRotateCCW() {
  analogWrite(ENA, MOTOR_SPEED);
  analogWrite(ENB, MOTOR_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

// Angry beats dizzy beats relax, so the loudest thing on the box is what the face shows.
void drawFace() {
  if (!hasDisplay) return;
  if (millis() < nextFrame) return;
  nextFrame = millis() + FRAME_MS;

  Mood mood = MOOD_RELAX;
  if (buzzing) mood = MOOD_ANGRY;
  else if (rotating) mood = MOOD_DIZZY;

  face.clear();
  if (mood == MOOD_ANGRY) drawAngry();
  else if (mood == MOOD_DIZZY) drawDizzy();
  else drawRelax();
  face.flush();
}

void drawRelax() {
  unsigned long now = millis();
  if (!blinking && now >= blinkAt) {
    blinking = true;
    blinkAt = now + 120;                   // eyelid stays shut this long
  } else if (blinking && now >= blinkAt) {
    blinking = false;
    blinkAt = now + random(2000, 5000);    // then wait a while before the next one
  }

  int h = blinking ? 5 : EYE_H;
  int gaze = (int)(6 * sin(now / 900.0));  // slow idle drift, left and right

  drawEye(EYE_L_X + gaze, h);
  drawEye(EYE_R_X + gaze, h);

  // gentle smile
  face.drawLine(48, 50, 64, 56, OLED_WHITE);
  face.drawLine(64, 56, 80, 50, OLED_WHITE);
}

void drawDizzy() {
  float phase = millis() / 130.0;
  drawSpiral(EYE_L_X, EYE_CY, phase);
  drawSpiral(EYE_R_X, EYE_CY, -phase);   // counter-rotating sells the wooziness

  // woozy wavy mouth
  for (int x = 44; x <= 84; x++) {
    int y = 53 + (int)(3 * sin((x + millis() / 60.0) / 4.0));
    face.drawPixel(x, y, OLED_WHITE);
  }
}

void drawAngry() {
  int shake = (millis() / 60) % 2 ? 2 : -2;

  drawEye(EYE_L_X + shake, EYE_H);
  drawEye(EYE_R_X + shake, EYE_H);

  // Carve the brows back out in black: each wedge is flat on the outer edge and
  // dips toward the nose, which is what reads as "angry" rather than "surprised".
  int top = EYE_CY - EYE_H / 2;
  int lx = EYE_L_X + shake - EYE_W / 2;
  int rx = EYE_R_X + shake - EYE_W / 2;
  face.fillTriangle(lx, top, lx + EYE_W, top, lx + EYE_W, top + 15, OLED_BLACK);
  face.fillTriangle(rx, top, rx + EYE_W, top, rx, top + 15, OLED_BLACK);

  // frown
  face.drawLine(48, 56, 64, 50, OLED_WHITE);
  face.drawLine(64, 50, 80, 56, OLED_WHITE);
}

void drawEye(int cx, int h) {
  int r = h / 2 < 8 ? h / 2 : 8;
  face.fillRoundRect(cx - EYE_W / 2, EYE_CY - h / 2, EYE_W, h, r, OLED_WHITE);
}

void drawSpiral(int cx, int cy, float phase) {
  for (float a = 0; a < 15.7; a += 0.12) {   // 2.5 turns
    float r = a * 1.05;
    int x = cx + (int)(cos(a + phase) * r);
    int y = cy + (int)(sin(a + phase) * r);
    face.drawPixel(x, y, OLED_WHITE);
    face.drawPixel(x, y + 1, OLED_WHITE);
  }
}

// Required parameters:
//   targetPC   - Target percent (as an integer) 0-100, 0 is starting position, 100 is hitting the switch
//   steps      - Degrees per 10 ms update
//   afterDelay - Adds a delay after completing the move
void gotoPercent(int targetPC, int steps, int afterDelay) {
  target = POS_START + ((POS_END - POS_START) * targetPC * .01);
  while (pos != target) {
    if (target < pos) {
      if (pos - target < steps) {
        pos = target;
      } else {
        pos -= steps;
      }
    } else {
      if (target - pos < steps) {
        pos = target;
      } else {
        pos += steps;
      }
    }
    myservo.write(pos);
    delay(10);
  }
  if (afterDelay > 0) delay(afterDelay);
}
