// =============================================================================
//  l298n_motor_test.ino - standalone bring-up test for Arduino UNO Q + L298N.
//
//  Not part of the BIN-CHAD build (that uses servos, not a DC motor driver).
//  This is a bench sketch to confirm wiring before the L298N goes into any
//  larger project: each motor runs forward, stops, runs reverse, stops, then
//  both motors run together. Watch the motors and read the Serial monitor
//  (115200 baud) to see which stage is running.
//
//  Board: Arduino UNO Q (STM32U585 MCU side, Zephyr-based Arduino core).
//  This is NOT the ESP32 this sketch used to target - there is no ledc API
//  here. PWM is the standard Arduino analogWrite() on D5/D6.
//
//  Wiring (per the L298N -> UNO Q table):
//    ENA -> D5    (left  motor speed / PWM)
//    ENB -> D6    (right motor speed / PWM)
//    IN1 -> D7    (left  motor direction A)
//    IN2 -> D8    (left  motor direction B)
//    IN3 -> D9    (right motor direction A)
//    IN4 -> D10   (right motor direction B)
//    L298N GND -> UNO Q GND (common ground, mandatory)
//    L298N +12V -> external motor supply, NOT the UNO Q 5V/3.3V pin
//
//  Build/upload with Arduino IDE 2+ (Board: Arduino UNO Q), not PlatformIO -
//  there is no official PlatformIO board definition for UNO Q yet.
//
//  If a motor does not spin: swap its two OUTx wires to flip direction
//  rather than rewriting code, and double check the common ground first -
//  that is the single most common reason "nothing happens".
//
//  Known UNO Q quirk: analogWrite() only works reliably on up to 4 pins at
//  once (enabling PWM on more can hang the sketch). This test only ever
//  drives 2 PWM pins (D5, D6), so it stays well under that limit.
// =============================================================================

const int ENA = 5;
const int ENB = 6;
const int IN1 = 7;
const int IN2 = 8;
const int IN3 = 9;
const int IN4 = 10;

void motorA(int speed, bool forward) {
    digitalWrite(IN1, forward ? HIGH : LOW);
    digitalWrite(IN2, forward ? LOW  : HIGH);
    analogWrite(ENA, speed);
}

void motorB(int speed, bool forward) {
    digitalWrite(IN3, forward ? HIGH : LOW);
    digitalWrite(IN4, forward ? LOW  : HIGH);
    analogWrite(ENB, speed);
}

void stopAll() {
    analogWrite(ENA, 0);
    analogWrite(ENB, 0);
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== L298N motor bring-up test (UNO Q) ===");

    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);
    pinMode(ENA, OUTPUT);
    pinMode(ENB, OUTPUT);

    stopAll();
    delay(1000);
    Serial.println("Setup complete. Starting sequence in 2s...");
    delay(2000);
}

const int FULL_SPEED = 255;

void loop() {
    // Each motor alone at full duty, isolated - so a low-voltage motor is
    // tested without the other motor sharing (and sagging) the supply.
    Serial.println("--- Motor A (left) ONLY, full duty=255 (ENA=D5, IN1=D7, IN2=D8) ---");
    motorA(FULL_SPEED, true);
    delay(6000);
    stopAll();
    delay(1500);

    Serial.println("--- Motor B (right) ONLY, full duty=255 (ENB=D6, IN3=D9, IN4=D10) ---");
    motorB(FULL_SPEED, true);
    delay(6000);
    stopAll();

    Serial.println("--- Cycle done. Pausing 2s before repeat. ---");
    delay(2000);
}
