#include <Arduino.h>

bool led_state = LOW;
bool blinked = false;

void setup() {
// write your initialization code here
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, led_state);
}

void loop() {
// write your code here
    if (!(millis() % 1000) && !blinked) {
        blinked = true;
        led_state = led_state == LOW ? HIGH : LOW;
        digitalWrite(LED_BUILTIN, led_state);
    } else if ((millis() % 1000) && blinked){
        blinked = false;
    }
    yield();
}