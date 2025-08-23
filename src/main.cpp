#include <Arduino.h>
#include <FastLED.h>
#include <EEPROM.h>

#define LED_COUNT 142   // 200 strip
//#define LED_COUNT 171 // 240 strip
//#define LED_COUNT 32

#define LED_PIN 5
#define SENSE_PIN 6
#define PATT_PIN 4
#define MIC_PIN 26

#define PRIMARY_HUE 29

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

#define SOUND_DC_OFFSET  0              // DC offset in mic signal - if unusure, leave 0
#define SOUND_NOISE     30              // Noise/hum/interference in mic signal and increased value until it went quiet
#define SOUND_SAMPLES   60              // Length of buffer for dynamic level adjustment
#define SOUND_TOP (LED_COUNT + 2)       // Allow dot to go slightly off scale
#define SOUND_PEAK_FALL 5               // Rate of sound_peak falling dot

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
uint8_t led_buffer[LED_COUNT] = {0};

int16_t rainbow_fpx_hue = 65535;
uint8_t pulse_x_offset = 255;
uint8_t pulse_next_led = 0;
int chase_x_offset = 0 + ANIM_KR_SIZE;
bool chase_rev = true;
int breathing_brightness = 255;
bool breathing_rev = false;

uint8_t sound_peak = 0;                                              // Used for falling dot
uint8_t sound_dot_count = 0;                                              // Frame counter for delaying dot-falling speed
uint8_t sound_vol_count = 0;                                              // Frame counter for storing past volume data
int sound_vol[SOUND_SAMPLES];                                              // Collection of prior volume samples
int sound_lvl = 10;                                             // Current "dampened" audio level
int sound_min_lvl_avg = 0;                                              // For dynamic adjustment of graph low & high
int sound_max_lvl_avg = 2048;

uint8_t startup_next_led = 0;
bool do_startup = true;

// debounce
bool pressed = false;
bool acted = false;

uint16_t frame_delay = 10.0f;
uint16_t frame_delay_rt = 10.0f;
uint16_t frame_delay_2 = 10.0f;
uint8_t startup_brightness = 0;
CRGB strip[LED_COUNT];
//CRGB onboard[1];

void update_anim_rainbow();
void update_anim_pulse();
void update_anim_breathing();
void update_anim_chase();
void update_anim_sound();

// helper function
int transition_time(int led_count, float seconds) {
    return int(float((seconds / led_count) * 1000.0f));
}

void setup() {
    analogReadResolution(12);
    pinMode(SENSE_PIN, INPUT_PULLUP);
    pinMode(PATT_PIN, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);
    CFastLED::addLeds<NEOPIXEL, LED_PIN>(strip, LED_COUNT);
    FastLED.clear();
    FastLED.show();
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
//        onboard.fill(Adafruit_NeoPixel::ColorHSV(21845,255,1));
//        onboard.show();

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
                    EEPROM.write(0, patt);
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
                FastLED.clear();
                switch (startup) {
                    case UP:
                        if (startup_next_led >= LED_COUNT) {
                            startup_next_led = LED_COUNT - 2;   // set next LED idx to second last in strip
                            startup = DOWN;
                        } else {
                            strip[startup_next_led++] = CRGB::White;
                            frame_millis = millis();
                            FastLED.show();
                        }
                        break;
                    case DOWN:
                        if (startup_next_led >= 255) {      // idx count overflowed, reached bottom of strip
                            switch (patt) {
                                case RAINBOW:
                                    startup = RAINBOW_IN;
                                    break;
                                default:
                                    do_startup = false;
                                    pulse_next_led = 0;
                                    memset(led_buffer, 0, sizeof(led_buffer));
                                    break;
                            }
                        } else {
                            strip[startup_next_led--] = CRGB::White;
                            frame_millis = millis();
                            FastLED.show();
                        }
                        break;
                    case RAINBOW_IN:
                        if (startup_brightness < 255) {
                            fl::fill_rainbow_circular(strip, LED_COUNT, rainbow_fpx_hue);
                            FastLED.setBrightness(++startup_brightness);
                            frame_millis = millis();
                            FastLED.show();
                        } else {
                            do_startup = false;
                            // startup = RAINBOW_OUT;
                        }
                        break;
                    case RAINBOW_OUT:
                        if (startup_brightness > 0) {
                            fl::fill_rainbow_circular(strip, LED_COUNT, rainbow_fpx_hue);
                            FastLED.setBrightness(--startup_brightness);
                            frame_millis = millis();
                            FastLED.show();
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
            FastLED.clear();
            switch (patt) {
                case RAINBOW:
                    update_anim_rainbow();
                    break;
                case PULSE:
                    update_anim_pulse();
                    break;
                case BREATHING:
                    update_anim_breathing();
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
                        fl::fill_solid(strip, pulse_next_led, CHSV(PRIMARY_HUE, 255, 255));
                    }
                    break;
                default:
                    break;
            }
            FastLED.show();
            yield();
        }
    }

    // as soon as strip disconnects
//    digitalWrite(LED_BUILTIN, LOW);
//    onboard.fill(Adafruit_NeoPixel::ColorHSV(0,255,1));
//    onboard.show();
    startup_brightness = 0;
    startup_next_led = 0;
    do_startup = true;
    startup = UP;
    yield();
}

void update_anim_rainbow() {
    fl::fill_rainbow_circular(strip, LED_COUNT, rainbow_fpx_hue);
    // update animation position
    if (millis() - frame_millis > ANIM_RAINBOW_FRAME_TIME) {
        frame_millis = millis();
        // scale rainbow anim cycle to 255 steps, meaning to go around
        // the entire hue circle in 255 steps, each step is 257 wide
        // whole animation will take 255 * ANIM_RAINBOW_FRAME_TIME ms
        rainbow_fpx_hue = rainbow_fpx_hue - 1 > 255 ? 255 : rainbow_fpx_hue - 1;
    }
}

void update_anim_pulse() {
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
        led_buffer[i] = scale * sin8((5 * i) + pulse_x_offset) + PULSE_Y_OFFSET;
        // queue changes to lighting
        strip[i] = CHSV(PRIMARY_HUE, 255, led_buffer[i]);
    }
    // update animation position
    if (millis() - frame_millis > ANIM_PULSE_FRAME_TIME) {
        frame_millis = millis();
        // chase anim has 255 steps, sine8() between 32 and 255 is spread across
        // 255 steps, whole animation will take 255 * ANIM_PULSE_FRAME_TIME ms
        pulse_x_offset--;
    }
}

void update_anim_breathing() {
    // chase pattern startup anim
    // update startup animation LED count
    if (pulse_next_led < LED_COUNT && (millis() - pulse_millis > frame_delay_2)) {
        pulse_millis = millis();
        pulse_next_led++;
    }
    // draw output of sine8() between 0 and LED_COUNT, change offset for next draw
    for (int i = 0; i < pulse_next_led; i++) {
        fl::fill_solid(strip, pulse_next_led, CHSV(PRIMARY_HUE, 255, breathing_brightness));
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
}

// based on neopixel sound reactive pendant and https://github.com/atuline/FastLED-SoundReactive
void update_anim_sound() {

    uint8_t i;
    uint16_t minLvl, maxLvl;
    int n, height;

    n = analogRead(MIC_PIN);                                    // Raw reading from mic
    n = abs(n - 2048 - SOUND_DC_OFFSET);                               // Center on zero

    n = (n <= SOUND_NOISE) ? 0 : (n - SOUND_NOISE);                         // Remove noise/hum
    sound_lvl = ((sound_lvl * 7) + n) >> 3;                                 // "Dampened" reading (else looks twitchy)

    // Calculate bar height based on dynamic min/max levels (fixed point):
    height = SOUND_TOP * (sound_lvl - sound_min_lvl_avg) / (long) (sound_max_lvl_avg - sound_min_lvl_avg);

    if (height < 0L) height = 0;                          // Clip output
    else if (height > SOUND_TOP) height = SOUND_TOP;
    if (height > sound_peak) sound_peak = height;                     // Keep 'sound_peak' dot at top


    // Color pixels based on rainbow gradient
    for (i = 0; i < LED_COUNT; i++) {
        if (i >= height) strip[i].setRGB(0, 0, 0);
        else strip[i] = CHSV(PRIMARY_HUE, 255, 255);
    }

    // Draw sound_peak dot
    if (sound_peak > 0 && sound_peak <= LED_COUNT - 1) strip[sound_peak] = CHSV(0, 0, 255);

// Every few frames, make the sound_peak pixel drop by 1:

    if (++sound_dot_count >= SOUND_PEAK_FALL) {                            // fall rate
        if (sound_peak > 0) sound_peak--;
        sound_dot_count = 0;
    }

    sound_vol[sound_vol_count] = n;                                          // Save sample for dynamic leveling
    if (++sound_vol_count >= SOUND_SAMPLES) sound_vol_count = 0;                    // Advance/rollover sample counter

    // Get volume range of prior frames
    minLvl = maxLvl = sound_vol[0];
    for (i = 1; i < SOUND_SAMPLES; i++) {
        if (sound_vol[i] < minLvl) minLvl = sound_vol[i];
        else if (sound_vol[i] > maxLvl) maxLvl = sound_vol[i];
    }
    // minLvl and maxLvl indicate the volume range over prior frames, used
    // for vertically scaling the output graph (so it looks interesting
    // regardless of volume level).  If they're too close together though
    // (e.g. at very low volume levels) the graph becomes super coarse
    // and 'jumpy'...so keep some minimum distance between them (this
    // also lets the graph go to zero when no sound is playing):
    if ((maxLvl - minLvl) < SOUND_TOP) maxLvl = minLvl + SOUND_TOP;
    sound_min_lvl_avg = (sound_min_lvl_avg * 63 + minLvl) >> 6;                 // Dampen min/max levels
    sound_max_lvl_avg = (sound_max_lvl_avg * 63 + maxLvl) >> 6;                 // (fake rolling average)

}

void update_anim_chase() {
    // chase pattern startup anim
    // update startup animation LED count
    if (pulse_next_led < LED_COUNT && (millis() - pulse_millis > frame_delay_2)) {
        pulse_millis = millis();
        pulse_next_led++;
    }
    // draw output of sine8() between 0 and LED_COUNT, change offset for next draw
    memset(led_buffer, 0, sizeof(led_buffer));
    if (chase_x_offset >= 0 && chase_x_offset < LED_COUNT) {
        for (int i = 0; i < ANIM_KR_SIZE; i++) {
            if (chase_x_offset - i >= 0) led_buffer[chase_x_offset - i] = 127;
        }
        led_buffer[chase_x_offset] = 255;
        for (int i = 0; i < ANIM_KR_SIZE; i++) {
            if (chase_x_offset + i <= LED_COUNT - 1) led_buffer[chase_x_offset + i] = 127;
        }

    }
    for (int i = 0; i < pulse_next_led; i++) {
        // queue changes to lighting
        strip[i] = CHSV(PRIMARY_HUE, 255, led_buffer[i]);
    }
    // update animation position
    if (millis() - frame_millis > ANIM_CHASE_FRAME_TIME) {
        frame_millis = millis();
        if (chase_rev) {
            if (chase_x_offset > (LED_COUNT - 2) - ANIM_KR_SIZE) {
                chase_x_offset = (LED_COUNT - 1) - ANIM_KR_SIZE;
                chase_rev = false;
            } else {
                chase_x_offset++;
            }
        } else {
            if (chase_x_offset < 1 + ANIM_KR_SIZE) {
                chase_x_offset = 1 + ANIM_KR_SIZE;
                chase_rev = true;
            } else {
                --chase_x_offset;
            }
        }
    }
}