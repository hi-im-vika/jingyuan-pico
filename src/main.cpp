#include <Arduino.h>
#include <ADCInput.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

#define LED_PIN 5
//#define LED_COUNT 142   // 200 strip
//#define LED_COUNT 171 // 240 strip
#define LED_COUNT 32

#define SENSE_PIN 6
#define PATT_PIN 4

#define DEBOUNCE_DELAY 10
#define PULSE_Y_OFFSET 32
#define CHASE_Y_OFFSET 16
#define RAINBOW_FADE_FRAME_DELAY 1
#define ANIM_RAINBOW_FRAME_TIME 10
#define ANIM_PULSE_FRAME_TIME 10
#define ANIM_CHASE_FRAME_TIME 5
#define ANIM_SOUND_FRAME_TIME 5
#define ANIM_BREATHING_TIME 5
#define ANIM_KR_SIZE 10

enum anim_state {
    DISCONNECTED,
    UP,
    DOWN,
    RAINBOW_IN,
    RAINBOW_OUT,
    STOP
};

enum anim_pattern {
    SOLID,
    PULSE,
    CHASE,
    BREATHING,
    SOUND,
    RAINBOW,
    ANIM_COUNT
};

// globals
// state machine
anim_state state = DISCONNECTED;
anim_state startup = UP;
anim_pattern patt = SOLID;
unsigned long pressed_millis = 0;
unsigned long frame_millis = 0;
unsigned long pulse_millis = 0;

// anims
uint8_t led_buffer[LED_COUNT] = {0 };

int16_t rainbow_fpx_hue = 65535;
uint8_t pulse_x_offset = 255;
uint8_t pulse_next_led = 0;
int chase_x_offset = 0 + ANIM_KR_SIZE;
bool chase_rev = true;
int breathing_brightness = 255;
bool breathing_rev = false;

int startup_next_led = 0;
bool do_startup = true;

// debounce
bool pressed = false;
bool acted = false;

int raw_sens_val = 0;
int ctr_sens_val = raw_sens_val;
int sound_fill = 0;
int lvl = 10;

uint16_t frame_delay = 10.0f;
uint16_t frame_delay_rt = 10.0f;
uint16_t frame_delay_2 = 10.0f;
byte startup_brightness = 0;
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel onboard(1, 16, NEO_GRB + NEO_KHZ800);
ADCInput adc(A0);

void update_anim_chase();
void update_anim_sound();

// helper function
int transition_time(int led_count, float seconds) {
    return int(float((seconds / led_count) * 1000.0f));
}

void setup() {
    analogReadResolution(12);
    adc.setBuffers(4, 32);
    adc.begin(2000);
    pinMode(SENSE_PIN, INPUT_PULLUP);
    pinMode(PATT_PIN, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);
    strip.begin();  // INITIALIZE NeoPixel strip object (REQUIRED)
    strip.show();   // Turn OFF all pixels ASAP
    onboard.begin();
    onboard.fill(Adafruit_NeoPixel::ColorHSV(21845,255,1));
    onboard.show();
    EEPROM.begin(1);
    patt = (anim_pattern) EEPROM.read(0);
    if ((patt < 0) || (patt >= ANIM_COUNT)) patt = SOLID;
    frame_delay = transition_time(LED_COUNT, 0.5f);
    frame_delay_rt = transition_time(LED_COUNT * 2, 0.5f);
    frame_delay_2 = transition_time(LED_COUNT, 0.25f);
}

void loop() {
    // while strip is connected
    while (digitalRead(SENSE_PIN) == LOW) {
        // turn on debug led when strip connected
//        digitalWrite(LED_BUILTIN, HIGH);
        onboard.fill(Adafruit_NeoPixel::ColorHSV(21845,255,1));
        onboard.show();

        // debounce tomfoolery
        if (digitalRead(PATT_PIN) == LOW && !pressed) {
            pressed = true;
            pressed_millis = millis();
        }

        // switch anim
        if (pressed) {
            if (millis() - pressed_millis > DEBOUNCE_DELAY) {
                if (digitalRead(PATT_PIN) == LOW && acted == false) {
                    switch (patt) {
                        case SOLID:
                            patt = PULSE;
                            break;
                        case PULSE:
                            patt = CHASE;
                            break;
                        case CHASE:
                            patt = BREATHING;
                            break;
                        case BREATHING:
                            patt = SOUND;
                            break;
                        case SOUND:
                            patt = RAINBOW;
                            break;
                        case RAINBOW:
                            patt = SOLID;
                            break;
                        default:
                            break;
                    }
                    acted = true;
                    EEPROM.write(0,patt);
                    EEPROM.commit();
                } else if (digitalRead(PATT_PIN) == HIGH && acted == true) {
                    pressed = false;
                    acted = false;
                }
            }
        }

        // if strip was disconnected
        if (do_startup) {
            if (millis() - frame_millis > frame_delay ||
                    (startup == RAINBOW_IN && millis() - frame_millis > RAINBOW_FADE_FRAME_DELAY) ||
                    (startup == RAINBOW_OUT && millis() - frame_millis > RAINBOW_FADE_FRAME_DELAY)) {
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
                                case RAINBOW:
                                    startup = RAINBOW_IN;
                                    break;
                                default:
                                    do_startup = false;
                                    pulse_next_led = 0;
                                    memset(led_buffer, 0, LED_COUNT * sizeof(led_buffer[0]));
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
                case PULSE:
                    // chase pattern startup anim
                    // update startup animation LED count
                    if (pulse_next_led < LED_COUNT && (millis() - pulse_millis > frame_delay_2)) {
                        pulse_millis = millis();
                        pulse_next_led++;
                    }
                    // draw output of sine8() between 0 and LED_COUNT, change offset for next draw
                    for (int i = 0; i < pulse_next_led; i++) {
                        // restrict brightness range between 32 and 255
                        float scale = (255 - PULSE_Y_OFFSET) / 255.0;
                        led_buffer[i] = scale * Adafruit_NeoPixel::sine8((5 * i) + pulse_x_offset) + PULSE_Y_OFFSET;
                        // queue changes to lighting
                        strip.setPixelColor(i, Adafruit_NeoPixel::ColorHSV(5461, 255, led_buffer[i]));
                    }
                    // update animation position
                    if (millis() - frame_millis > ANIM_PULSE_FRAME_TIME) {
                        frame_millis = millis();
                        // chase anim has 255 steps, sine8() between 32 and 255 is spread across
                        // 255 steps, whole animation will take 255 * ANIM_PULSE_FRAME_TIME ms
                        pulse_x_offset--;
                    }
                    break;
                case BREATHING:
                    // chase pattern startup anim
                    // update startup animation LED count
                    if (pulse_next_led < LED_COUNT && (millis() - pulse_millis > frame_delay_2)) {
                        pulse_millis = millis();
                        pulse_next_led++;
                    }
                    // draw output of sine8() between 0 and LED_COUNT, change offset for next draw
                    for (int i = 0; i < pulse_next_led; i++) {
                        strip.setPixelColor(i, Adafruit_NeoPixel::ColorHSV(5461, 255, breathing_brightness));
//                        if (breathing_brightness >= 0 && breathing_brightness < 254) {
//                            strip.setPixelColor(i, Adafruit_NeoPixel::ColorHSV(5461, 255, 200));
//                        }
                    }
                    // update animation position
                    if (millis() - frame_millis > ANIM_BREATHING_TIME) {
                        frame_millis = millis();
                        if (breathing_rev) {
                            if (breathing_brightness > 254) {
                                breathing_brightness--;
                                breathing_rev = false;
                            } else {
                                breathing_brightness++;
                            }
                        } else {
                            if (breathing_brightness < 1) {
                                breathing_brightness = 0;
                                breathing_rev = true;
                            } else {
                                --breathing_brightness;
                            }
                        }
                    }
                    break;
                case CHASE:
                    update_anim_chase();
                    break;
                case SOUND:
                    update_anim_sound();
                    break;
                case SOLID:
                    // solid pattern startup anim
                    // update startup animation LED count
                    if (pulse_next_led < LED_COUNT && (millis() - pulse_millis > frame_delay_2)) {
                        pulse_millis = millis();
                        pulse_next_led++;
                    }
                    // only fill LEDs when pulse_next_led > 0, since 0 fills all LEDs

                    if (pulse_next_led) {
                        strip.fill(Adafruit_NeoPixel::ColorHSV(5461, 255, 255), 0, pulse_next_led);
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