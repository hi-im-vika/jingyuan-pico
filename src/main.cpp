#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 10
#define LED_COUNT 18
#define SENSE_PIN 9
#define PATT_PIN 8
#define DEBOUNCE_DELAY 10
#define CHASE_Y_OFFSET 32

enum anim_state {
    DISCONNECTED,
    UP,
    DOWN,
    RAINBOW_IN,
    RAINBOW_OUT,
    STOP
};

enum anim_pattern {
    RAINBOW,
    CHASE,
    SOLID
};

// globals
anim_state state = DISCONNECTED;
anim_state startup = UP;
volatile anim_pattern patt = SOLID;
long pressed_millis = 0;
int16_t rainbow_fpx_hue = 65535;
uint8_t chase_sine_pos = 255;
uint8_t chase_array[LED_COUNT] = { 0 };
uint8_t chase_x_offset = 255;
uint8_t chase_cnt = 0;
int next_led = 0;
bool do_startup = true;
bool pressed = false;
bool acted = false;
int next_delay = 10.0f;
byte startup_brightness = 0;
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// helper function
int transition_time(int led_count, float seconds) {
    return int(float((seconds / led_count) * 1000.0f));
}

void setup() {
    pinMode(SENSE_PIN, INPUT_PULLUP);
    pinMode(PATT_PIN, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);
    strip.begin();  // INITIALIZE NeoPixel strip object (REQUIRED)
    strip.show();   // Turn OFF all pixels ASAP
    next_delay = transition_time(LED_COUNT, 0.5f);
}

void loop() {
    // while strip is connected
    while (digitalRead(SENSE_PIN) == LOW) {
        // turn on debug led when strip connected
        digitalWrite(LED_BUILTIN, HIGH);

        // debounce tomfoolery
        if (digitalRead(PATT_PIN) == LOW && !pressed) {
            pressed = true;
            pressed_millis = millis();
        }

        if (pressed) {
            if (millis() - pressed_millis > DEBOUNCE_DELAY) {
                if (digitalRead(PATT_PIN) == LOW && acted == false) {
                    switch (patt) {
                        case SOLID:
                            patt = CHASE;
                            break;
                        case CHASE:
                            patt = RAINBOW;
                            break;
                        case RAINBOW:
                            patt = SOLID;
                            break;
                        default:
                            break;
                    }
                    acted = true;
                } else if (digitalRead(PATT_PIN) == HIGH && acted == true) {
                    pressed = false;
                    acted = false;
                }
            }
        }

        // if strip was disconnected
        if (do_startup) {
            if (!(millis() % next_delay) || startup == RAINBOW_IN || startup == RAINBOW_OUT) {
                strip.clear();
                switch (startup) {
                    case UP:
                        if (next_led > (LED_COUNT - 1)) {
                            next_led--;
                            startup = DOWN;
                        } else {
                            strip.setPixelColor(next_led++, 0xFFFFFF);
                            strip.show();
                        }
                        break;
                    case DOWN:
                        if (next_led < 0) {
                            switch (patt) {
                                case CHASE:
                                case SOLID:
                                    do_startup = false;
                                    chase_cnt = 0;
                                    memset(chase_array, 0, LED_COUNT * sizeof(chase_array[0]));
                                    break;
                                default:
                                    startup = RAINBOW_IN;
                                    break;
                            }
                        } else {
                            strip.setPixelColor(--next_led, 0xFFFFFF);
                            strip.show();
                        }
                        break;
                    case RAINBOW_IN:
                        if (startup_brightness < 255) {
                            strip.rainbow(rainbow_fpx_hue, 1, 255, ++startup_brightness);
                            strip.show();
                        } else {
                            do_startup = false;
                            // startup = RAINBOW_OUT;
                        }
                        break;
                    case RAINBOW_OUT:
                        if (startup_brightness > 0) {
                            strip.rainbow(rainbow_fpx_hue, 1, 255, --startup_brightness);
                            strip.show();
                        } else {
                            do_startup = false;
                        }
                        break;
                    default:
                        break;
                }
            }
        } else {
            // otherwise, do normal anims
            strip.clear();
            switch (patt) {
                case RAINBOW:
                    strip.rainbow(rainbow_fpx_hue);
                    rainbow_fpx_hue = rainbow_fpx_hue - 64 > 65535 ? 65535 : rainbow_fpx_hue - 64;
                    break;
                case CHASE:
                    if (chase_cnt < LED_COUNT && !(millis() % transition_time(LED_COUNT, 0.25f))) {
                        chase_cnt++;
                    }
                    for (int i = 0; i < chase_cnt; i++) {
                        // restrict brightness range between 32 and 255
                        float scale = (255 - CHASE_Y_OFFSET) / 255.0;
                        chase_array[i] = scale * Adafruit_NeoPixel::sine8((5 * i) + chase_x_offset) + CHASE_Y_OFFSET;
                        // queue changes to lighting
                        strip.setPixelColor(i, Adafruit_NeoPixel::ColorHSV(5461, 255, chase_array[i]));
                    }
                    chase_x_offset--;
                    break;
                case SOLID:
                    if (chase_cnt < LED_COUNT && !(millis() % transition_time(LED_COUNT, 0.25f))) {
                        chase_cnt++;
                    }
                    memset(chase_array, 255, LED_COUNT * sizeof(chase_array[0]));
                    for (int i = 0; i < chase_cnt; i++) {
                        strip.setPixelColor(i, Adafruit_NeoPixel::ColorHSV(5461, 255, chase_array[i]));
                    }
                    break;
                default:
                    break;
            }
            strip.show();
            yield();
        }
    }

    // as soon as strip disconnects
    digitalWrite(LED_BUILTIN, LOW);
    startup_brightness = 0;
    next_led = 0;
    do_startup = true;
    startup = UP;
    yield();
}