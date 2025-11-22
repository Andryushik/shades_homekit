// Board pin assignments
// On-board indicator (active LOW on most boards)
#define LED_PIN 2 // D4 (GPIO2)

// Buttons (wired with INPUT_PULLUP; pressed == LOW)
#define BUTTON_UP_PIN 4   // D2 (GPIO4)
#define BUTTON_DOWN_PIN 0 // D3 (GPIO0) — avoid holding while powering up

// Motor ULN2003 driver coil pins (AccelStepper 4-wire HALF4WIRE)
// Wiring: IN1 -> D7 (GPIO13), IN2 -> D6 (GPIO12), IN3 -> D5 (GPIO14), IN4 -> D1 (GPIO5)
#define IN1 13 // D7 (GPIO13)
#define IN2 12 // D6 (GPIO12)
#define IN3 14 // D5 (GPIO14)
#define IN4 5  // D1 (GPIO5)
