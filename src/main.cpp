#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 10
#define LED_COUNT 18
#define SENSE_PIN 9
#define PATT_PIN 8
#define DEBOUNCE_DELAY 10
#define CHASE_Y_OFFSET 32
#define RAINBOW_FADE_FRAME_DELAY 1
#define ANIM_RAINBOW_FRAME_TIME 10
#define ANIM_CHASE_FRAME_TIME 10

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
// state machine
anim_state state = DISCONNECTED;
anim_state startup = UP;
anim_pattern patt = SOLID;
ulong pressed_millis = 0;
ulong frame_millis = 0;
ulong chase_millis = 0;

// anims
int16_t rainbow_fpx_hue = 65535;
uint8_t chase_array[LED_COUNT] = { 0 };
uint8_t chase_x_offset = 255;
uint8_t chase_next_led = 0;
int startup_next_led = 0;
bool do_startup = true;

// debounce
bool pressed = false;
bool acted = false;

uint16_t frame_delay = 10.0f;
uint16_t frame_delay_rt = 10.0f;
uint16_t frame_delay_2 = 10.0f;
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
    frame_delay = transition_time(LED_COUNT, 0.5f);
    frame_delay_rt = transition_time(LED_COUNT * 2, 0.5f);
    frame_delay_2 = transition_time(LED_COUNT, 0.25f);
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
            if (millis() - frame_millis > frame_delay_rt ||
            startup == RAINBOW_IN && millis() - frame_millis > RAINBOW_FADE_FRAME_DELAY ||
            startup == RAINBOW_OUT && millis() - frame_millis > RAINBOW_FADE_FRAME_DELAY) {
                strip.clear();
                switch (startup) {
                    case UP:
                        if (startup_next_led > (LED_COUNT - 1)) {
                            startup_next_led--;
                            startup = DOWN;
                        } else {
                            strip.setPixelColor(startup_next_led++, 0xFFFFFF);
                            frame_millis = millis();
                            strip.show();
                        }
                        break;
                    case DOWN:
                        if (startup_next_led < 0) {
                            switch (patt) {
                                case CHASE:
                                case SOLID:
                                    do_startup = false;
                                    chase_next_led = 0;
                                    memset(chase_array, 0, LED_COUNT * sizeof(chase_array[0]));
                                    break;
                                default:
                                    startup = RAINBOW_IN;
                                    break;
                            }
                        } else {
                            strip.setPixelColor(--startup_next_led, 0xFFFFFF);
                            frame_millis = millis();
                            strip.show();
                        }
                        break;
                    case RAINBOW_IN:
                        if (startup_brightness < 255) {
                            strip.rainbow(rainbow_fpx_hue, 1, 255, ++startup_brightness);
                            frame_millis = millis();
                            strip.show();
                        } else {
                            do_startup = false;
                            // startup = RAINBOW_OUT;
                        }
                        break;
                    case RAINBOW_OUT:
                        if (startup_brightness > 0) {
                            strip.rainbow(rainbow_fpx_hue, 1, 255, --startup_brightness);
                            frame_millis = millis();
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
                    // update animation position
                    if (millis() - frame_millis > ANIM_RAINBOW_FRAME_TIME) {
                        frame_millis = millis();
                        // scale rainbow anim cycle to 255 steps, meaning to go around
                        // the entire hue circle in 255 steps, each step is 257 wide
                        // whole animation will take 255 * ANIM_RAINBOW_FRAME_TIME ms
                        rainbow_fpx_hue = rainbow_fpx_hue - 257 > 65535 ? 65535 : rainbow_fpx_hue - 257;
                    }
                    break;
                case CHASE:
                    // chase pattern startup anim
                    // update startup animation LED count
                    if (chase_next_led < LED_COUNT && (millis() - chase_millis > frame_delay_2)) {
                        chase_millis = millis();
                        chase_next_led++;
                    }
                    // draw output of sine8() between 0 and LED_COUNT, change offset for next draw
                    for (int i = 0; i < chase_next_led; i++) {
                        // restrict brightness range between 32 and 255
                        float scale = (255 - CHASE_Y_OFFSET) / 255.0;
                        chase_array[i] = scale * Adafruit_NeoPixel::sine8((5 * i) + chase_x_offset) + CHASE_Y_OFFSET;
                        // queue changes to lighting
                        strip.setPixelColor(i, Adafruit_NeoPixel::ColorHSV(5461, 255, chase_array[i]));
                    }
                    // update animation position
                    if (millis() - frame_millis > ANIM_CHASE_FRAME_TIME) {
                        frame_millis = millis();
                        // chase anim has 255 steps, sine8() between 32 and 255 is spread across
                        // 255 steps, whole animation will take 255 * ANIM_CHASE_FRAME_TIME ms
                        chase_x_offset--;
                    }
                    break;
                case SOLID:
                    // solid pattern startup anim
                    // update startup animation LED count
                    if (chase_next_led < LED_COUNT && (millis() - chase_millis > frame_delay_2)) {
                        chase_millis = millis();
                        chase_next_led++;
                    }
                    // only fill LEDs when chase_next_led > 0, since 0 fills all LEDs

                    if (chase_next_led) {
                        strip.fill(Adafruit_NeoPixel::ColorHSV(5461, 255, 255), 0, chase_next_led);
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
    startup_next_led = 0;
    do_startup = true;
    startup = UP;
    yield();
}