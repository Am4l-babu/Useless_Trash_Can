#include <ESP32Servo.h>

//Board: ESP32 Dev Module (esp32:esp32:esp32)

//Connect servo signal to D2, +5V to 5V/VIN, GND to GND
//Connect the switch between D4 and GND (internal pull-up, no resistor needed)
#define SERVO_PIN 2
#define SWITCH_PIN 4

//Built-in LED. Most ESP32 dev boards don't have one wired to a GPIO by default;
//set to -1 to disable, or change to your board's onboard LED pin.
#define LED_BUILTIN_PIN 2
#define USE_LED false

//Defines the range of motion for the servo, starting(resting) and ending(hitting the switch)
#define POS_START 180
#define POS_END 45

Servo myservo;
int pos = POS_START;
int target = POS_START;
int seq = 0;
int rapidCount = -1;
long lastInput = 0;
bool goRandom;

void setup() {
  Serial.begin(115200);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  if (USE_LED) pinMode(LED_BUILTIN_PIN, OUTPUT);

  myservo.setPeriodHertz(50);
  myservo.attach(SERVO_PIN, 500, 2400);

  if (USE_LED) digitalWrite(LED_BUILTIN_PIN, HIGH);
  gotoPercent(50, 1, 0);  // do a small initialization to show it's ready
  gotoPercent(0, 1, 0);
  if (USE_LED) digitalWrite(LED_BUILTIN_PIN, LOW);

  int switchState = digitalRead(SWITCH_PIN);
  if (switchState == LOW) { //Reads the switch at startup to set random
    goRandom = false;
  } else {
    goRandom = true;
  }

  randomSeed(analogRead(34)); //Seed random from a floating ADC-capable input pin
}

void loop() {
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
  if (USE_LED) {
    if (millis() - lastInput < 10000 && rapidCount >= 3) {  //illuminate the LED when it's mad or about to be mad
      digitalWrite(LED_BUILTIN_PIN, HIGH);
    } else {
      digitalWrite(LED_BUILTIN_PIN, LOW);
    }
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
