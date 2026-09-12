// =============================================================================
// obstacle_avoid_test.ino
// Arduino UNO Q + L298N + HC-SR04
//
// Behavior:
//   - If an object is closer than OBSTACLE_CM:
//       Robot spins in the opposite direction at FULL SPEED.
//   - If the path is clear:
//       Motors stop.
//
// Board:
//   Arduino UNO Q
//
// L298N:
//   ENA -> D5
//   ENB -> D6
//   IN1 -> D7
//   IN2 -> D8
//   IN3 -> D9
//   IN4 -> D10
//   GND -> UNO Q GND
//   +12V -> external motor supply
//
// HC-SR04:
//   TRIG -> D2
//   ECHO -> D3 through voltage divider
//   VCC  -> 5V
//   GND  -> GND
//
// IMPORTANT:
//   HC-SR04 ECHO is typically 5V.
//   Use a voltage divider before connecting ECHO to UNO Q D3.
//
// Full-speed PWM:
//   0   = stopped
//   128 = approximately 50%
//   255 = 100% PWM
// =============================================================================


const int ENA = 5;
const int ENB = 6;

const int IN1 = 7;
const int IN2 = 8;

const int IN3 = 9;
const int IN4 = 10;

const int TRIG_PIN = 2;
const int ECHO_PIN = 3;


// -----------------------------------------------------------------------------
// Settings
// -----------------------------------------------------------------------------

const float OBSTACLE_CM = 20.0;

// FULL SPEED
const int SPIN_DUTY = 255;

const unsigned long ECHO_TIMEOUT_US = 30000;


// -----------------------------------------------------------------------------
// LEFT MOTOR
// -----------------------------------------------------------------------------

void motorLeft(int speed, bool forward) {

    if (forward) {
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
    } 
    else {
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
    }

    analogWrite(ENA, speed);
}


// -----------------------------------------------------------------------------
// RIGHT MOTOR
// -----------------------------------------------------------------------------

void motorRight(int speed, bool forward) {

    if (forward) {
        digitalWrite(IN3, HIGH);
        digitalWrite(IN4, LOW);
    } 
    else {
        digitalWrite(IN3, LOW);
        digitalWrite(IN4, HIGH);
    }

    analogWrite(ENB, speed);
}


// -----------------------------------------------------------------------------
// STOP BOTH MOTORS
// -----------------------------------------------------------------------------

void stopAll() {

    analogWrite(ENA, 0);
    analogWrite(ENB, 0);

    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);

    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
}


// -----------------------------------------------------------------------------
// SPIN IN OPPOSITE DIRECTION
//
// Previous:
//   Left  = Forward
//   Right = Reverse
//
// Now:
//   Left  = Reverse
//   Right = Forward
//
// This reverses the direction of rotation.
// -----------------------------------------------------------------------------

void spinInPlace() {

    motorLeft(SPIN_DUTY, false);   // LEFT MOTOR REVERSE
    motorRight(SPIN_DUTY, true);   // RIGHT MOTOR FORWARD
}


// -----------------------------------------------------------------------------
// READ HC-SR04 DISTANCE
//
// Returns:
//   distance in cm
//   -1 if no echo is received
// -----------------------------------------------------------------------------

float readDistanceCm() {

    // Make sure trigger starts LOW
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);

    // 10 microsecond trigger pulse
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // Measure echo pulse
    unsigned long duration =
        pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);

    // No echo
    if (duration == 0) {
        return -1.0;
    }

    // Convert microseconds to centimeters
    float distance =
        duration * 0.0343f / 2.0f;

    return distance;
}


// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------

void setup() {

    Serial.begin(115200);

    delay(300);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" UNO Q OBSTACLE AVOIDANCE TEST");
    Serial.println(" FULL-SPEED REVERSE SPIN");
    Serial.println("========================================");


    // Motor direction pins
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);

    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);


    // Motor PWM pins
    pinMode(ENA, OUTPUT);
    pinMode(ENB, OUTPUT);


    // Ultrasonic
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    digitalWrite(TRIG_PIN, LOW);


    // Start safely stopped
    stopAll();


    Serial.println("Setup complete.");
    Serial.println("Motor PWM = 255 (FULL SPEED)");
    Serial.println("Obstacle threshold = 20 cm");
    Serial.println();
}


// -----------------------------------------------------------------------------
// MAIN LOOP
// -----------------------------------------------------------------------------

bool spinning = false;


void loop() {

    float distance = readDistanceCm();


    // -------------------------------------------------------------------------
    // Display distance
    // -------------------------------------------------------------------------

    if (distance < 0) {

        Serial.println(
            "Distance: OUT OF RANGE / NO ECHO"
        );

    } 
    else {

        Serial.print("Distance: ");
        Serial.print(distance);
        Serial.println(" cm");
    }


    // -------------------------------------------------------------------------
    // Determine whether obstacle exists
    // -------------------------------------------------------------------------

    bool blocked =
        (distance > 0 && distance < OBSTACLE_CM);


    // -------------------------------------------------------------------------
    // State change
    // -------------------------------------------------------------------------

    if (blocked && !spinning) {

        Serial.println(
            ">>> OBSTACLE DETECTED!"
        );

        Serial.println(
            ">>> SPINNING OPPOSITE DIRECTION"
        );

        Serial.println(
            ">>> MOTOR SPEED = 255 / FULL SPEED"
        );

        spinning = true;
    }


    else if (!blocked && spinning) {

        Serial.println(
            ">>> PATH CLEAR"
        );

        Serial.println(
            ">>> STOPPING MOTORS"
        );

        spinning = false;
    }


    // -------------------------------------------------------------------------
    // Motor control
    // -------------------------------------------------------------------------

    if (spinning) {

        spinInPlace();

    } 
    else {

        stopAll();
    }


    // Small delay between ultrasonic measurements
    delay(60);
}